#include "Dx12Renderer.h"

HWND Dx12Renderer::m_mainWindowHWND = nullptr;
Dx12Renderer* Dx12Renderer::mApp = nullptr;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int nShowCmd) {

#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    Dx12Renderer renderer(hInstance);

    try {
        if (!renderer.Initialise(hInstance, nShowCmd)) return 0;
        return renderer.Run();
    }
    catch (DxException& e) {
        if (e.errorCode == DXGI_ERROR_DEVICE_REMOVED || e.errorCode == DXGI_ERROR_DEVICE_RESET) {
            renderer.DeviceRemovedReason();
        }
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

Dx12Renderer::Dx12Renderer(HINSTANCE hInstance) : mAppInst(hInstance)
{
    //assert(mApp == nullptr);
    //mApp = this;
}

Dx12Renderer::~Dx12Renderer()
{
    if (mD3DDevice != nullptr) FlushCommandQueue();
}

Dx12Renderer* Dx12Renderer::GetApp()
{
    return mApp;
}

HINSTANCE Dx12Renderer::AppInst() const
{
    return mAppInst;
}

HWND Dx12Renderer::MainWnd() const
{
    return m_mainWindowHWND;
}

bool Dx12Renderer::Get4xMsaaState() const
{
    return m4xMSAAState;
}

void Dx12Renderer::Set4xMsaaState(bool value)
{
    if (m4xMSAAState != value) {
        m4xMSAAState = value;
        CreateSwapChain();
        OnResize();
    }
}

bool Dx12Renderer::Initialise(HINSTANCE hInstance, int nShowCmd)
{
    if(!InitWinApp(hInstance, nShowCmd)) return false;
    if(!InitialiseDirect3D()) return false;
    OnResize();

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildBoxGeometry();
    BuildRenderItems();
    BuildFrameResources();
    BuildDescriptorHeaps();
    BuildConstantBuffers();
    BuildPSO();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    FlushCommandQueue();

    return true;
}

void Dx12Renderer::DeviceRemovedReason()
{
    HRESULT reason = mD3DDevice->GetDeviceRemovedReason();
#if defined(_DEBUG)
    wchar_t outString[100];
    size_t size = 100;
    swprintf_s(outString, size, L"Device removed! DXGI_ERROR code: 0x%X\n", reason);
    OutputDebugStringW(outString);
#endif
}

void Dx12Renderer::SetRaytracingState(bool state)
{
    mRaytracing = state;
}

void Dx12Renderer::CheckRaytracingSupport()
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 raytraceFeaturesOptions = {};
    ThrowIfFailed(mD3DDevice->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS5,
        &raytraceFeaturesOptions,
        sizeof(raytraceFeaturesOptions)));
    assert(raytraceFeaturesOptions.RaytracingTier > 0 && "Raytracing is Unsupported on this Device");
}

bool Dx12Renderer::InitialiseDirect3D()
{

    //1 - Creating Device
    #if defined(DEBUG) || defined(_DEBUG) 
    {
        ComPtr<ID3D12Debug> debugController;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
        debugController->EnableDebugLayer();
    }
    #endif

    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mDXGIFactory)));

    HRESULT hardwareResult = D3D12CreateDevice(
        nullptr,
        D3D_FEATURE_LEVEL_12_1,
        IID_PPV_ARGS(&mD3DDevice));

    if (FAILED(hardwareResult)) {
        ComPtr<IDXGIAdapter> pWarpAdapter;
        ThrowIfFailed(mDXGIFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));
        ThrowIfFailed(D3D12CreateDevice(pWarpAdapter.Get(),
            D3D_FEATURE_LEVEL_12_1,
            IID_PPV_ARGS(&mD3DDevice)));
    }

    //2 - Creating Fence and Descriptor Sizes
    ThrowIfFailed(mD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
    mRTVDescriptorSize = mD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDSVDescriptorSize = mD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCBVSRVUAVDescriptorSize = mD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    //Raytracing PreChecks
    CheckRaytracingSupport();

    //3 - Check 4x MSAA quality support ??not necessary??
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLvls;
    msQualityLvls.Format = mBackBufferFormat;
    msQualityLvls.SampleCount = 4;
    msQualityLvls.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    msQualityLvls.NumQualityLevels = 0;
    ThrowIfFailed(mD3DDevice->CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
        &msQualityLvls,
        sizeof(msQualityLvls)));
    m4xMSAAQuality = msQualityLvls.NumQualityLevels;
    assert(m4xMSAAQuality > 0 && "Unexpected MSAA quality level.");

    //4 - Create Command Queue and Command List
    CreateCommandObjects();

    //5 - DescribeAndCreateSwapChain
    CreateSwapChain();

    //6 - Create Descriptor Heaps
    CreateRTVAndDSVDescriptorHeaps();
   
    //mCommandList->RSSetViewports(1, &vp);
    //mCommandList->RSSetScissorRects(1, &mScissorRect);
    return true;
}

void Dx12Renderer::Draw(const Timer gameTimer)
{
    auto cmdListAllocation = mCurrFrameResource->cmdListAllocator;
    ThrowIfFailed(cmdListAllocation->Reset());

    if (bIsWireframe) { 
        ThrowIfFailed(mCommandList->Reset(cmdListAllocation.Get(), mPsos["opaque_wireframe"].Get()));
    }
    else {
        ThrowIfFailed(mCommandList->Reset(cmdListAllocation.Get(), mPsos["opaque"].Get()));
    }

    mCommandList->RSSetViewports(1, &vp);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrier);

    if (!mRaytracing) {
        mCommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::Orchid, 0, nullptr);
        mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
        auto backBufferView = CurrentBackBufferView();
        auto depthStencilView = DepthStencilView();
        mCommandList->OMSetRenderTargets(1, &backBufferView, true, &depthStencilView);


        ID3D12DescriptorHeap* descriptorHeaps[] = { mCBVHeap.Get() };
        mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

        int passCbvIndex = mPassCBVOffset + mCurrFrameResourceIndex;
        auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCBVHeap->GetGPUDescriptorHandleForHeapStart());
        passCbvHandle.Offset(passCbvIndex, mCBVSRVUAVDescriptorSize);

        //auto vertexBufferView = mBoxGeometry->VertexBufferView();
        //auto indexBufferView = mBoxGeometry->IndexBufferView();
        //mCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
        //mCommandList->IASetIndexBuffer(&indexBufferView);
        //mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);
        DrawRenderItems(mCommandList.Get(), mOpaqueRendItems);
        //mCommandList->DrawIndexedInstanced(mBoxGeometry->drawArgs["box"].IndexCount, 1, 0, 0, 0);
    }
    else {
        mCommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::Sienna, 0, nullptr);
        mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
        auto backBufferView = CurrentBackBufferView();
        auto depthStencilView = DepthStencilView();
        mCommandList->OMSetRenderTargets(1, &backBufferView, true, &depthStencilView);
    }

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(mVSync, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;

    mCurrFrameResource->fence = ++mCurrFence;
    mCommandQueue->Signal(mFence.Get(), mCurrFence);
    //FlushCommandQueue();
}

void Dx12Renderer::UpdateCamera(const Timer gameTimer)
{
    mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
    mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
    mEyePos.y = mRadius * cosf(mPhi);

    DirectX::XMVECTOR pos = DirectX::XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
    DirectX::XMVECTOR target = DirectX::XMVectorZero();
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(pos, target, up);
    DirectX::XMStoreFloat4x4(&mView, view);
}

void Dx12Renderer::UpdateObjectsCB(const Timer gameTimer)
{
    auto currObjCB = mCurrFrameResource->objectCB.get();
    for (auto& e : mAllRendItems) {
        if (e->numFramesDirty > 0) {
            DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&e->world);
            ObjectsConsts objConsts;
            DirectX::XMStoreFloat4x4(&objConsts.world, DirectX::XMMatrixTranspose(world));
            currObjCB->CopyData(e->objCBIndex, objConsts);
            e->numFramesDirty--;
        }
    }
}

void Dx12Renderer::UpdateMainPassCB(const Timer gameTimer)
{
    DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&mView);
    DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(&mProj);

    DirectX::XMMATRIX viewProj =    DirectX::XMMatrixMultiply(view, proj);
    auto viewMatrixDeterminant = DirectX::XMMatrixDeterminant(view);
    DirectX::XMMATRIX invView =     DirectX::XMMatrixInverse(&viewMatrixDeterminant,view);
    auto projMatrixDeterminant = DirectX::XMMatrixDeterminant(proj);
    DirectX::XMMATRIX invProj =     DirectX::XMMatrixInverse(&projMatrixDeterminant, proj);
    auto viewProjMatrixDeterminant = DirectX::XMMatrixDeterminant(viewProj);
    DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(&viewProjMatrixDeterminant, viewProj);;
    
    DirectX::XMStoreFloat4x4(&mMainPassCB.view, DirectX::XMMatrixTranspose(view));
    DirectX::XMStoreFloat4x4(&mMainPassCB.invView, DirectX::XMMatrixTranspose(invView));
    DirectX::XMStoreFloat4x4(&mMainPassCB.proj, DirectX::XMMatrixTranspose(proj));
    DirectX::XMStoreFloat4x4(&mMainPassCB.invProj, DirectX::XMMatrixTranspose(invProj));
    DirectX::XMStoreFloat4x4(&mMainPassCB.viewProj, DirectX::XMMatrixTranspose(viewProj));
    DirectX::XMStoreFloat4x4(&mMainPassCB.invViewProj, DirectX::XMMatrixTranspose(invViewProj));
    mMainPassCB.eyePos = mEyePos;
    mMainPassCB.renderTargetSize = DirectX::XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.invRenderTargetSize = DirectX::XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.nearZ = 1.0f;
    mMainPassCB.farZ = 1000.0f;
    mMainPassCB.totalTime = gameTimer.TotalTime();
    mMainPassCB.frameTime = gameTimer.FrameTime();

    auto currPassCB = mCurrFrameResource->passCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void Dx12Renderer::Update(const Timer gameTimer)
{
    UpdateCamera(gameTimer);
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if (mCurrFrameResource->fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->fence) {
        //HANDLE eventHandle = CreateEventEx(nullptr, NULL, CREATE_EVENT_INITIAL_SET, EVENT_ALL_ACCESS);
        HANDLE eventHandle = CreateEventEx(nullptr, 0, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
    UpdateObjectsCB(gameTimer);
    UpdateMainPassCB(gameTimer);

   /* float x = mRadius * sinf(mPhi) * cosf(mTheta);
    float z = mRadius * sinf(mPhi) * sinf(mTheta);
    float y = mRadius * cosf(mPhi);

    DirectX::XMVECTOR pos = DirectX::XMVectorSet(x, y, z, 1.0f);
    DirectX::XMVECTOR target = DirectX::XMVectorZero();
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(pos, target, up);
    DirectX::XMStoreFloat4x4(&mView, view);

    DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&mWorld);
    DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(&mProj);
    DirectX::XMMATRIX worldViewProj = world * view * proj;

    constants modelConstants;
    DirectX::XMStoreFloat4x4(&modelConstants.worldViewProj, DirectX::XMMatrixTranspose(worldViewProj));
    mObjectCB->CopyData(0, modelConstants);*/
}

void Dx12Renderer::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(mD3DDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));
    ThrowIfFailed(mD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));
    ThrowIfFailed(mD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mDirectCmdListAlloc.Get(), nullptr, IID_PPV_ARGS(mCommandList.GetAddressOf())));
    mCommandList->Close();
}

void Dx12Renderer::CreateSwapChain()
{
    mSwapChain.Reset();

    DXGI_SWAP_CHAIN_DESC scd;
    scd.BufferDesc.Width = mClientWidth;
    scd.BufferDesc.Height = mClientHeight;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferDesc.Format = mBackBufferFormat;
    scd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    scd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    scd.SampleDesc.Count = m4xMSAAState ? 4 : 1;
    scd.SampleDesc.Quality = m4xMSAAState ? (m4xMSAAQuality - 1) : 0;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
    scd.OutputWindow = m_mainWindowHWND;
    scd.Windowed = true;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    ThrowIfFailed(mDXGIFactory->CreateSwapChain(mCommandQueue.Get(), &scd, mSwapChain.GetAddressOf()));
}

void Dx12Renderer::CreateRTVAndDSVDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SWAP_CHAIN_BUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(mD3DDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(mRTVHeap.GetAddressOf())));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(mD3DDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(mDSVHeap.GetAddressOf())));
}

void Dx12Renderer::OnResize()
{
    assert(mD3DDevice);
    assert(mSwapChain);
    assert(mDirectCmdListAlloc);
    FlushCommandQueue();

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    for (int i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i) mSwapChainBuffer[i].Reset();
    mDepthStencilBuffer.Reset();

    ThrowIfFailed(mSwapChain->ResizeBuffers(SWAP_CHAIN_BUFFER_COUNT, mClientWidth, mClientHeight, mBackBufferFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
    mCurrBackBuffer = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRTVHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++) {
        ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
        mD3DDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(1, mRTVDescriptorSize);
    }

    //8 - Create the Depth/Stencil Buffer and View
    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = mClientWidth;
    depthStencilDesc.Height = mClientHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = mDepthStencilFormat;
    depthStencilDesc.SampleDesc.Count = m4xMSAAState ? 4 : 1;
    depthStencilDesc.SampleDesc.Quality = m4xMSAAQuality ? (m4xMSAAQuality - 1) : 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = mDepthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;
    auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(mD3DDevice->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = mDepthStencilFormat;
    dsvDesc.Texture2D.MipSlice = 0;
    mD3DDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    mCommandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    FlushCommandQueue();

    //9 - Set the Viewport
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(mClientWidth);
    vp.Height = static_cast<float>(mClientHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    //10 - Set the Scissor Rectangles
    mScissorRect = { 0, 0, mClientWidth /*/ 2*/, mClientHeight /*/ 2 */};

    DirectX::XMMATRIX p = DirectX::XMMatrixPerspectiveFovLH(0.25f * DirectX::XM_PI, GetAspectRatio(), 1.0f, 1000.0f);
    DirectX::XMStoreFloat4x4(&mProj, p);
}

void Dx12Renderer::FlushCommandQueue()
{
    mCurrFence++;
    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrFence));
    if (mFence->GetCompletedValue() < mCurrFence) {
        HANDLE eventHandle = CreateEventEx(nullptr, 0, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

void Dx12Renderer::BuildDescriptorHeaps()
{
    UINT objCount = (UINT)mOpaqueRendItems.size();
    UINT numDescriptors = (objCount + 1) * gNumFrameResources;
    mPassCBVOffset = objCount * gNumFrameResources;

    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = numDescriptors;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(mD3DDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCBVHeap)));
}

void Dx12Renderer::BuildConstantBuffers()
{
    UINT objCBByteSize = Utility::CalcConstantBufferByteSize(sizeof(ObjectsConsts));
    UINT objCount = (UINT)mOpaqueRendItems.size();

    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
        auto objectCB = mFrameResources[frameIndex]->objectCB->Resource();
        for (UINT i = 0; i < objCount; ++i) {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();
            cbAddress += i * objCBByteSize;
            int heapIndex = frameIndex * objCount + i;// +1;
            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCBVHeap->GetCPUDescriptorHandleForHeapStart());
            handle.Offset(heapIndex, mCBVSRVUAVDescriptorSize);

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
            cbvDesc.BufferLocation = cbAddress;
            cbvDesc.SizeInBytes = objCBByteSize;
            mD3DDevice->CreateConstantBufferView(&cbvDesc, handle);
        }
    }

    UINT passCBByteSize = Utility::CalcConstantBufferByteSize(sizeof(PassConsts));

    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
        auto passCB = mFrameResources[frameIndex]->passCB->Resource();
        D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();
        int heapIndex = mPassCBVOffset + frameIndex;
        auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCBVHeap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(heapIndex, mCBVSRVUAVDescriptorSize);

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
        cbvDesc.BufferLocation = cbAddress;
        cbvDesc.SizeInBytes = passCBByteSize;
        mD3DDevice->CreateConstantBufferView(&cbvDesc, handle);
    }

   /* mObjectCB = std::make_unique<UploadBuffer<constants>>(mD3DDevice.Get(), 1, true);

    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
    int boxCBufIndex = 0;
    cbAddress += boxCBufIndex * objCBByteSize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = objCBByteSize;

    mD3DDevice->CreateConstantBufferView(&cbvDesc, mCBVHeap->GetCPUDescriptorHandleForHeapStart());*/
}

void Dx12Renderer::BuildRootSignature()
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    CD3DX12_DESCRIPTOR_RANGE cbvTable0;
    CD3DX12_DESCRIPTOR_RANGE cbvTable1;
    cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
    slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
    if (errorBlob != nullptr)OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    ThrowIfFailed(hr);

    ThrowIfFailed(mD3DDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void Dx12Renderer::BuildShadersAndInputLayout()
{
    HRESULT hr = S_OK;

    mShaders["standardVS"] = Utility::CompileShader(L"cube_vs.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = Utility::CompileShader(L"cube_ps.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        { "COLOUR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
}

void Dx12Renderer::BuildBoxGeometry()
{
    std::array<vertexConsts, 8> vertices = {
        vertexConsts({DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::White)}),
        vertexConsts({DirectX::XMFLOAT3(-1.0f, +1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Black)}),
        vertexConsts({DirectX::XMFLOAT3(+1.0f, +1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Red)}),
        vertexConsts({DirectX::XMFLOAT3(+1.0f, -1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Green)}),
        vertexConsts({DirectX::XMFLOAT3(-1.0f, -1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Blue)}),
        vertexConsts({DirectX::XMFLOAT3(-1.0f, +1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Yellow)}),
        vertexConsts({DirectX::XMFLOAT3(+1.0f, +1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Cyan)}),
        vertexConsts({DirectX::XMFLOAT3(+1.0f, -1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Magenta)})};

    std::array<std::uint16_t, 36> indices = {
        0, 1, 2,
        0, 2, 3,

        4, 6, 5,
        4, 7, 6,

        4, 5, 1,
        4, 1, 0,

        3, 2, 6,
        3, 6, 7,

        1, 5, 6,
        1, 6, 2,

        4, 0, 3,
        4, 3, 7
    };

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(vertexConsts);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    mBoxGeometry = std::make_unique<MeshGeometry>();
    mBoxGeometry->name = "shapeGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeometry->vertexBufferCPU));
    CopyMemory(mBoxGeometry->vertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeometry->indexBufferCPU));
    CopyMemory(mBoxGeometry->indexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    mBoxGeometry->vertexBufferGPU = CreateDefaultBuffer(mD3DDevice.Get(), mCommandList.Get(), vertices.data(), vbByteSize, mBoxGeometry->vertexBufferUploader);
    mBoxGeometry->indexBufferGPU  = CreateDefaultBuffer(mD3DDevice.Get(), mCommandList.Get(), indices.data(),  ibByteSize, mBoxGeometry->indexBufferUploader);

    mBoxGeometry->vertexByteStride = sizeof(vertexConsts);
    mBoxGeometry->vertexBufferByteSize = vbByteSize;
    mBoxGeometry->indexFormat = DXGI_FORMAT_R16_UINT;
    mBoxGeometry->indexBufferByteSize = ibByteSize;

    SubmeshGeometry subMesh;
    subMesh.IndexCount = (UINT)indices.size();
    subMesh.StartIndexLocation = 0;
    subMesh.BaseVertexLocation = 0;

    mBoxGeometry->drawArgs["box"] = subMesh;
    mGeos[mBoxGeometry->name] = std::move(mBoxGeometry);
}

void Dx12Renderer::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    opaquePsoDesc.pRootSignature = mRootSignature.Get();
    opaquePsoDesc.VS = {
        /*reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize()*/
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS = {
        /*reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
        mpsByteCode->GetBufferSize()*/
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMSAAState ? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = m4xMSAAState ? (m4xMSAAQuality - 1) : 0;
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(mD3DDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPsos["opaque"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
    opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(mD3DDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPsos["opaque_wireframe"])));
}

void Dx12Renderer::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i) {
        mFrameResources.push_back(std::make_unique<FrameResource>(mD3DDevice.Get(), 1, (UINT)mAllRendItems.size()));
    }
}

void Dx12Renderer::BuildRenderItems()
{
    auto boxRendItem = std::make_unique<RenderItem>();
    DirectX::XMStoreFloat4x4(&boxRendItem->world, DirectX::XMMatrixScaling(1.0f, 1.0f, 1.0f) * DirectX::XMMatrixTranslation(0.0f, 0.0f, 0.0f));
    boxRendItem->objCBIndex = 0;
    boxRendItem->meshGeo = mGeos["shapeGeo"].get();
    boxRendItem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRendItem->indexCount = boxRendItem->meshGeo->drawArgs["box"].IndexCount;
    boxRendItem->startIndexLocation = boxRendItem->meshGeo->drawArgs["box"].StartIndexLocation;
    boxRendItem->baseVertexLocation = boxRendItem->meshGeo->drawArgs["box"].BaseVertexLocation;
    mAllRendItems.push_back(std::move(boxRendItem));

    for (auto& e : mAllRendItems) mOpaqueRendItems.push_back(e.get());
}

void Dx12Renderer::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& rendItems)
{
    UINT objCBByteSize = Utility::CalcConstantBufferByteSize(sizeof(ObjectsConsts));
    auto objectCB = mCurrFrameResource->objectCB->Resource();

    for (size_t i = 0; i < rendItems.size(); ++i) {
        auto ri = rendItems[i];
        auto meshGeoVertexBufferView = ri->meshGeo->VertexBufferView();
        cmdList->IASetVertexBuffers(0, 1, &meshGeoVertexBufferView);
        auto meshGeoIndexBufferView = ri->meshGeo->IndexBufferView();
        cmdList->IASetIndexBuffer(&meshGeoIndexBufferView);
        cmdList->IASetPrimitiveTopology(ri->primitiveType);

        UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRendItems.size() + ri->objCBIndex;
        auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCBVHeap->GetGPUDescriptorHandleForHeapStart());
        cbvHandle.Offset(cbvIndex, mCBVSRVUAVDescriptorSize);

        cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);
        cmdList->DrawIndexedInstanced(ri->indexCount, 1, ri->startIndexLocation, ri->baseVertexLocation, 0);
    }
}

float Dx12Renderer::GetAspectRatio()
{
    return static_cast<float>(mClientWidth) / mClientHeight;
}

ID3D12Resource* Dx12Renderer::CurrentBackBuffer() const
{
    return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12Renderer::CurrentBackBufferView() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        mRTVHeap->GetCPUDescriptorHandleForHeapStart(),
        mCurrBackBuffer,
        mRTVDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12Renderer::DepthStencilView() const
{
    return mDSVHeap->GetCPUDescriptorHandleForHeapStart();
}

ComPtr<ID3D12Resource> Dx12Renderer::CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer)
{
    ComPtr<ID3D12Resource> defaultBuffer;
    auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

    D3D12_SUBRESOURCE_DATA subResourceData = {initData, byteSize, subResourceData.RowPitch};

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST);
    mCommandList->ResourceBarrier(1, &barrier);
    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    mCommandList->ResourceBarrier(1, &barrier);

    return defaultBuffer;
}

void Dx12Renderer::CalculateFrameStats()
{
    static int frameCount = 0;
    static float timeElapsed = 0.0f;

    frameCount++;

    if ((mGameTimer.TotalTime() - timeElapsed) >= 1.0f) {
        float fps = float(frameCount);
        float mspf = 1000.0f / fps;

        std::wstring fpsStr = std::to_wstring(fps);
        std::wstring mspfStr = std::to_wstring(mspf);
        std::wstring windowText = mMainWndCaption + L"  fps: " + fpsStr + L"    mspf: " + mspfStr;

        SetWindowText(m_mainWindowHWND, windowText.c_str());
        frameCount = 0;
        timeElapsed += 1.0f;
    }
}

bool Dx12Renderer::InitWinApp(HINSTANCE hInstance, int show)
{
    WNDCLASS wndClass;
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc = WndProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = hInstance;
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wndClass.lpszMenuName = 0;
    wndClass.lpszClassName = L"MastersProjectRendererClass";

    if (!RegisterClass(&wndClass)) {
        MessageBox(0, L"RegisterClass FAILED", 0, 0);
        return false;
    }

    m_mainWindowHWND = CreateWindow(
        wndClass.lpszClassName,
        mMainWndCaption.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        hInstance,
        0);

    if (m_mainWindowHWND == 0) {
        MessageBox(0, L"CreateWindow FAILED", 0, 0);
        return false;
    }

    ShowWindow(m_mainWindowHWND, show);
    UpdateWindow(m_mainWindowHWND);
    return true;
}

int Dx12Renderer::Run()
{
    MSG msg = {0};
    mGameTimer.Reset();

    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            //MessageBox(0, L"GetMessage FAILED", L"Error", MB_OK);
            //break;
        }
        else {
            mGameTimer.Tick();
            CalculateFrameStats();
            Update(mGameTimer);
            Draw(mGameTimer);
        }
    }
    return (int)msg.wParam;
}

LRESULT Dx12Renderer::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
        MessageBox(0, L"Test", 0, 0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(m_mainWindowHWND);
        
        //else if (wParam == VK_SPACE) SetRaytracingState(!mRaytracing);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
