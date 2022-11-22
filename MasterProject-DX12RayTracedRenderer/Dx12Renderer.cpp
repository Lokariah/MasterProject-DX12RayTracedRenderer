#include "Dx12Renderer.h"

using Microsoft::WRL::ComPtr;

HWND Dx12Renderer::m_mainWindowHWND = nullptr;

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

bool Dx12Renderer::Initialise(HINSTANCE hInstance, int nShowCmd)
{
    if(!InitWinApp(hInstance, nShowCmd)) return false;
    if(!InitialiseDirect3D()) return false;
    OnResize();

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
    BuildDescriptorHeaps();
    BuildConstantBuffers();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildBoxGeometry();
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
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&mD3DDevice));

    if (FAILED(hardwareResult)) {
        ComPtr<IDXGIAdapter> pWarpAdapter;
        ThrowIfFailed(mDXGIFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));
        ThrowIfFailed(D3D12CreateDevice(pWarpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&mD3DDevice)));
    }

    //2 - Creating Fence and Descriptor Sizes
    ThrowIfFailed(mD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
    mRTVDescriptorSize = mD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDSVDescriptorSize = mD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCBVSRVUAVDescriptorSize = mD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

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
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

    mCommandList->RSSetViewports(1, &vp);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::Orchid, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    auto backBufferView = CurrentBackBufferView();
    auto depthStencilView = DepthStencilView();
    mCommandList->OMSetRenderTargets(1, &backBufferView, true, &depthStencilView);

    ID3D12DescriptorHeap* descriptorHeaps[] = { mCBVHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    
    mCommandList->IASetVertexBuffers(0, 1, &mBoxGeometry->VertexBufferView());
    mCommandList->IASetIndexBuffer(&mBoxGeometry->IndexBufferView());
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    mCommandList->SetGraphicsRootDescriptorTable(0, mCBVHeap->GetGPUDescriptorHandleForHeapStart());

    mCommandList->DrawIndexedInstanced(mBoxGeometry->drawArgs["box"].IndexCount, 1, 0, 0, 0);

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;

    FlushCommandQueue();
}

void Dx12Renderer::Update(const Timer gameTimer)
{
    float x = mRadius * sinf(mPhi) * cosf(mTheta);
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
    mObjectCB->CopyData(0, modelConstants);
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
    mScissorRect = { 0, 0, mClientWidth / 2, mClientHeight / 2 };

    DirectX::XMMATRIX p = DirectX::XMMatrixPerspectiveFovLH(0.25f * DirectX::XM_PI, GetAspectRatio(), 1.0f, 1000.0f);
    DirectX::XMStoreFloat4x4(&mProj, p);
}

void Dx12Renderer::FlushCommandQueue()
{
    mCurrFence++;
    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrFence));
    if (mFence->GetCompletedValue() < mCurrFence) {
        HANDLE eventHandle = CreateEventEx(nullptr, NULL, CREATE_EVENT_INITIAL_SET, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

void Dx12Renderer::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = 1;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(mD3DDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCBVHeap)));
}

void Dx12Renderer::BuildConstantBuffers()
{
    mObjectCB = std::make_unique<UploadBuffer<constants>>(mD3DDevice.Get(), 1, true);

    UINT objCBByteSize = Utility::CalcConstantBufferByteSize(sizeof(constants));

    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
    int boxCBufIndex = 0;
    cbAddress += boxCBufIndex * objCBByteSize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = Utility::CalcConstantBufferByteSize(sizeof(constants));

    mD3DDevice->CreateConstantBufferView(&cbvDesc, mCBVHeap->GetCPUDescriptorHandleForHeapStart());
}

void Dx12Renderer::BuildRootSignature()
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[1];
    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
    if (errorBlob != nullptr)OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    ThrowIfFailed(hr);

    ThrowIfFailed(mD3DDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)));
}

void Dx12Renderer::BuildShadersAndInputLayout()
{
    HRESULT hr = S_OK;

    mvsByteCode = Utility::CompileShader(L"Shaders\\Colour.hlsl", nullptr, "VS", "vs_5_0");
    mpsByteCode = Utility::CompileShader(L"Shaders\\Colour.hlsl", nullptr, "PS", "ps_5_0");

    mInputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        { "COLOUR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
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
    
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

    D3D12_SUBRESOURCE_DATA subResourceData = {initData, byteSize, subResourceData.RowPitch};

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST);
    mCommandList->ResourceBarrier(1, &barrier);
    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
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
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
