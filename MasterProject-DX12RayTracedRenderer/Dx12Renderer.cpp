#include "Dx12Renderer.h"
#include "Input.h"

using namespace Dx12MasterProject;

HWND Dx12Renderer::m_mainWindowHWND = nullptr;
Dx12Renderer* Dx12Renderer::mApp = nullptr;
Dx12Renderer* renderer;
static dxc::DxcDllSupport gDxcDllHelper;

static const D3D12_HEAP_PROPERTIES kUploadHeapProps = {
    D3D12_HEAP_TYPE_UPLOAD,
    D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    D3D12_MEMORY_POOL_UNKNOWN,
    0,
    0
};

static const D3D12_HEAP_PROPERTIES kDefaultHeapProps = {
    D3D12_HEAP_TYPE_DEFAULT,
    D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    D3D12_MEMORY_POOL_UNKNOWN,
    0,
    0
};

static const WCHAR* RAY_GEN_SHADER = L"RayGen";
static const WCHAR* MISS_SHADER = L"Miss";
static const WCHAR* CLOSEST_HIT_SHADER = L"Hit";
static const WCHAR* HIT_GROUP = L"HitGroup";

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int nShowCmd) {

#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    renderer = new Dx12Renderer(hInstance);

    try {
        if (!renderer->Initialise(hInstance, nShowCmd)) return 0;
        return renderer->Run();
    }
    catch (DxException& e) {
        if (e.errorCode == DXGI_ERROR_DEVICE_REMOVED || e.errorCode == DXGI_ERROR_DEVICE_RESET) {
            renderer->DeviceRemovedReason();
        }
        delete renderer;
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

Dx12Renderer::Dx12Renderer(HINSTANCE hInstance) : mAppInst(hInstance)
{

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
    InitInput();

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
    
    if (mRaytracing) {
        CreateAccelerationStructures();
        CreateRtPipelineState();
        CreateShaderResources();
        CreateShaderTable();
        BuildFrameResourcesRT();
    }
    else {
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

        DirectX::XMFLOAT3 temp = { 1.0f, 0.0f, -10.0f };
        DirectX::XMFLOAT3 temp2 = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 temp3 = { 0.0f, 1.0f, 0.0f };

        mainCamera.SetPos(temp);
        mainCamera.LookAt(temp, temp2, temp3);
        mainCamera.UpdateViewMatrix();
    }

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

    ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    ThrowIfFailed(mD3DDevice->QueryInterface(IID_PPV_ARGS(&dred)));

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT dredAutoBreadcrumbsOutput;
    D3D12_DRED_PAGE_FAULT_OUTPUT dredPageFaultOutput;
    ThrowIfFailed(dred->GetAutoBreadcrumbsOutput(&dredAutoBreadcrumbsOutput));
    ThrowIfFailed(dred->GetPageFaultAllocationOutput(&dredPageFaultOutput));
    int i = 6;
#endif
}

ID3D12RootSignature* Dx12Renderer::CreateRootSignature(ID3D12Device5* device, const D3D12_ROOT_SIGNATURE_DESC& desc)
{
    ID3DBlob* sigBlob;
    ID3DBlob* errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
    ThrowIfFailed(hr);

    ID3D12RootSignature* rootSig;
    ThrowIfFailed(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSig)));
    return rootSig;
}
void Dx12Renderer::ToggleRaytracingState() {
    SetRaytracingState(!mRaytracing);

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

void Dx12Renderer::CreateAccelerationStructures()
{
    mVertexBuffer = CreateTriangleVB(mD3DDevice.Get());
    AccelerationStructBuffers bottomLevelBuffers = CreateBottomLevelAS(mD3DDevice.Get(), mCommandList.Get(), mVertexBuffer.Get());
    AccelerationStructBuffers topLevelBuffers = CreateTopLevelAS(mD3DDevice.Get(), mCommandList.Get(), bottomLevelBuffers.pResult.Get(), mTlasSize);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    FlushCommandQueue();

    // Store the AS buffers. The rest of the buffers will be released once we exit the function
    mTopLvlAS = topLevelBuffers.pResult;
    mBotLvlAS = bottomLevelBuffers.pResult;
}

void Dx12Renderer::CreateRtPipelineState()
{
    // Need 10 subobjects:
    //  1 for the DXIL library
    //  1 for hit-group
    //  2 for RayGen root-signature (root-signature and the subobject association)
    //  2 for the root-signature shared between miss and hit shaders (signature and association)
    //  2 for shader config (shared between all programs. 1 for the config, 1 for association)
    //  1 for pipeline config
    //  1 for the global root signature
    std::array<D3D12_STATE_SUBOBJECT, 10> subObj;
    uint32_t index = 0;

    // Create the DXIL library
    DxilLibrary dxilLib = CreateDxilLibrary();
    subObj[index++] = dxilLib.stateSubobj; // 0 Library

    HitProgram hitProgram(nullptr, CLOSEST_HIT_SHADER, HIT_GROUP);
    subObj[index++] = hitProgram.subObj; // 1 Hit Group

    // Create the ray-gen root-signature and association
    LocalRootSig rgsRootSignature(mD3DDevice.Get(), CreateRayGenRootDesc().desc);
    subObj[index] = rgsRootSignature.subObj; // 2 RayGen Root Sig

    uint32_t rgsRootIndex = index++; // 2
    ExportAssociation rgsRootAssociation(&RAY_GEN_SHADER, 1, &(subObj[rgsRootIndex]));
    subObj[index++] = rgsRootAssociation.subObj; // 3 Associate Root Sig to RGS

    // Create the miss- and hit-programs root-signature and association
    D3D12_ROOT_SIGNATURE_DESC emptyDesc = {};
    emptyDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    LocalRootSig hitMissRootSignature(mD3DDevice.Get(), emptyDesc);
    subObj[index] = hitMissRootSignature.subObj; // 4 Root Sig to be shared between Miss and CHS

    uint32_t hitMissRootIndex = index++; // 4
    const WCHAR* missHitExportName[] = { MISS_SHADER, CLOSEST_HIT_SHADER };
    ExportAssociation missHitRootAssociation(missHitExportName, (sizeof(missHitExportName) / sizeof(missHitExportName[0])), &(subObj[hitMissRootIndex]));
    subObj[index++] = missHitRootAssociation.subObj; // 5 Associate Root Sig to Miss and CHS

    // Bind the payload size to the programs
    ShaderConfig shaderConfig(sizeof(float) * 2, sizeof(float) * 3);
    subObj[index] = shaderConfig.subObj; // 6 Shader Config

    uint32_t shaderConfigIndex = index++; // 6
    const WCHAR* shaderExports[] = { MISS_SHADER, CLOSEST_HIT_SHADER, RAY_GEN_SHADER };
    ExportAssociation configAssociation(shaderExports, (sizeof(shaderExports) / sizeof(shaderExports[0])), &(subObj[shaderConfigIndex]));
    subObj[index++] = configAssociation.subObj; // 7 Associate Shader Config to Miss, CHS, RGS

    // Create the pipeline config
    PipelineConfig config(1);
    subObj[index++] = config.subObj; // 8

    // Create the global root signature and store the empty signature
    GlobalRootSig root(mD3DDevice.Get(), {});
    mEmptyRootSig = root.rootSig;
    subObj[index++] = root.subObj; // 9

    // Create the state
    D3D12_STATE_OBJECT_DESC desc;
    desc.NumSubobjects = index; // 10
    desc.pSubobjects = subObj.data();
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

    ThrowIfFailed(mD3DDevice->CreateStateObject(&desc, IID_PPV_ARGS(&mPipelineState)));
}

void Dx12Renderer::CreateShaderTable()
{
    /** The shader-table layout is as follows:
       Entry 0 - Ray-gen program
       Entry 1 - Miss program
       Entry 2 - Hit program
       All entries in the shader-table must have the same size, so we will choose it base on the largest required entry.
       The ray-gen program requires the largest entry - sizeof(program identifier) + 8 bytes for a descriptor-table.
       The entry size must be aligned up to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT
   */

   // Calculate the size and create the buffer
    mShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    mShaderTableEntrySize += 8; // The ray-gen's descriptor table
    mShaderTableEntrySize = Utility::RoundUp(mShaderTableEntrySize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    uint32_t shaderTableSize = mShaderTableEntrySize * 3;

    // For simplicity, we create the shader-table on the upload heap. You can also create it on the default heap
    mShaderTable = CreateBuffer(mD3DDevice.Get(), shaderTableSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);

    // Map the buffer
    uint8_t* pData;
    ThrowIfFailed(mShaderTable->Map(0, nullptr, (void**)&pData));

    ID3D12StateObjectProperties* pRtsoProps;
    mPipelineState->QueryInterface(IID_PPV_ARGS(&pRtsoProps));

    // Entry 0 - ray-gen program ID and descriptor data
    memcpy(pData, pRtsoProps->GetShaderIdentifier(RAY_GEN_SHADER), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    uint64_t heapStart = mSrvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr;
    *(uint64_t*)(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = heapStart;

    // Entry 1 - miss program
    memcpy(pData + mShaderTableEntrySize, pRtsoProps->GetShaderIdentifier(MISS_SHADER), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // Entry 2 - hit program
    uint8_t* pHitEntry = pData + mShaderTableEntrySize * 2; // +2 skips the ray-gen and miss entries
    memcpy(pHitEntry, pRtsoProps->GetShaderIdentifier(HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // Unmap
    mShaderTable->Unmap(0, nullptr);
}

void Dx12Renderer::CreateShaderResources()
{
    // Create the output resource. The dimensions and format should match the swap-chain
    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.DepthOrArraySize = 1;
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB formats can't be used with UAVs. We will convert to sRGB ourselves in the shader
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resDesc.Height = mClientHeight;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Width = mClientWidth;
    ThrowIfFailed(mD3DDevice->CreateCommittedResource(&kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&mOutputResource))); // Starting as copy-source to simplify onFrameRender()

    // Create an SRV/UAV descriptor heap. Need 2 entries - 1 SRV for the scene and 1 UAV for the output
    mSrvUavHeap = CreateDescHeap(mD3DDevice.Get(), 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

    // Create the UAV. Based on the root signature we created it should be the first entry
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    mD3DDevice->CreateUnorderedAccessView(mOutputResource.Get(), nullptr, &uavDesc, mSrvUavHeap->GetCPUDescriptorHandleForHeapStart());

    // Create the TLAS SRV right after the UAV. Note that we are using a different SRV desc here
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = mTopLvlAS->GetGPUVirtualAddress();
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = mSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
    srvHandle.ptr += mD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    mD3DDevice->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
}

bool Dx12Renderer::InitialiseDirect3D()
{

    //1 - Creating Device
    #if defined(DEBUG) || defined(_DEBUG) 
    {
        ComPtr<ID3D12Debug> debugController;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
        debugController->EnableDebugLayer();

        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)));

        dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
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
    if (!mRaytracing) {
        auto cmdListAllocation = mCurrFrameResource->cmdListAllocator;
        ThrowIfFailed(cmdListAllocation->Reset());
        if (bIsWireframe) {
            ThrowIfFailed(mCommandList->Reset(cmdListAllocation.Get(), mPsos["opaque_wireframe"].Get()));
        }
        else {
            ThrowIfFailed(mCommandList->Reset(cmdListAllocation.Get(), mPsos["opaque"].Get()));
        }
    }
    else {
        auto cmdListAllocation = mCurrFrameResourceRT->cmdListAllocator;
        ThrowIfFailed(cmdListAllocation->Reset());
        ThrowIfFailed(mCommandList->Reset(cmdListAllocation.Get(), nullptr));
    }

    mCommandList->RSSetViewports(1, &vp);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    if (!mRaytracing) {
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
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        mCommandList->ResourceBarrier(1, &barrier);
    }
    else {
        ID3D12DescriptorHeap* heaps[] = { mSrvUavHeap.Get()};
        mCommandList->SetDescriptorHeaps(sizeof(heaps) / sizeof(heaps[0]), heaps);

       auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        mCommandList->ResourceBarrier(1, &barrier);
        mCommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::Sienna, 0, nullptr);
        mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(mOutputResource.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        mCommandList->ResourceBarrier(1, &barrier);

        D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
        raytraceDesc.Width = mClientWidth;
        raytraceDesc.Height = mClientHeight;
        raytraceDesc.Depth = 1;

        // RayGen is the first entry in the shader-table
        raytraceDesc.RayGenerationShaderRecord.StartAddress = mShaderTable->GetGPUVirtualAddress() + 0 * mShaderTableEntrySize;
        raytraceDesc.RayGenerationShaderRecord.SizeInBytes = mShaderTableEntrySize;

        // Miss is the second entry in the shader-table
        size_t missOffset = 1 * mShaderTableEntrySize;
        raytraceDesc.MissShaderTable.StartAddress = mShaderTable->GetGPUVirtualAddress() + missOffset;
        raytraceDesc.MissShaderTable.StrideInBytes = mShaderTableEntrySize;
        raytraceDesc.MissShaderTable.SizeInBytes = mShaderTableEntrySize;   // Only a s single miss-entry

        // Hit is the third entry in the shader-table
        size_t hitOffset = 2 * mShaderTableEntrySize;
        raytraceDesc.HitGroupTable.StartAddress = mShaderTable->GetGPUVirtualAddress() + hitOffset;
        raytraceDesc.HitGroupTable.StrideInBytes = mShaderTableEntrySize;
        raytraceDesc.HitGroupTable.SizeInBytes = mShaderTableEntrySize;

        //// Bind the empty root signature
        mCommandList->SetComputeRootSignature(mEmptyRootSig.Get());

        //// Dispatch
        mCommandList->SetPipelineState1(mPipelineState.Get());
        mCommandList->DispatchRays(&raytraceDesc);

        // Copy the results to the back-buffer
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(mOutputResource.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        mCommandList->ResourceBarrier(1, &barrier); 

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COPY_DEST);
        mCommandList->ResourceBarrier(1, &barrier);

        mCommandList->CopyResource(CurrentBackBuffer(), mOutputResource.Get());

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PRESENT);
        mCommandList->ResourceBarrier(1, &barrier);
    }
    
    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(mVSync, 0)); //
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;
    if (mRaytracing) mCurrFrameResourceRT->fence = ++mCurrFence;
    else mCurrFrameResource->fence = ++mCurrFence;
    mCommandQueue->Signal(mFence.Get(), mCurrFence);
    //FlushCommandQueue();
}

void Dx12Renderer::UpdateCamera(const Timer gameTimer)
{
    mainCamera.SetFrustrum(0.25f * DirectX::XM_PI, GetAspectRatio(), 1.0f, 1000.0f);
    mainCamera.UpdateViewMatrix();
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
    DirectX::XMMATRIX view = mainCamera.GetViewMatrix();
    DirectX::XMMATRIX proj = mainCamera.GetProjMatrix();

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
    mMainPassCB.eyePos = mainCamera.GetPos();
    mMainPassCB.renderTargetSize = DirectX::XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.invRenderTargetSize = DirectX::XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.nearZ = mainCamera.GetNearZ();
    mMainPassCB.farZ = mainCamera.GetFarZ();
    mMainPassCB.totalTime = gameTimer.TotalTime();
    mMainPassCB.frameTime = gameTimer.FrameTime();

    auto currPassCB = mCurrFrameResource->passCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void Dx12Renderer::Update(const Timer gameTimer)
{
    if (!mRaytracing) {
        float ft = gameTimer.FrameTime();
        if (KeyHeld(KeyValue::KeyW))            mainCamera.MoveForwards(cameraMoveSpeed * ft);
        if (KeyHeld(KeyValue::KeyS))            mainCamera.MoveForwards(-cameraMoveSpeed * ft);
        if (KeyHeld(KeyValue::KeyA))            mainCamera.MoveRight(-cameraMoveSpeed * ft);
        if (KeyHeld(KeyValue::KeyD))            mainCamera.MoveRight(cameraMoveSpeed * ft);
        if (KeyHeld(KeyValue::KeyArrowLeft))    mainCamera.Yaw(-cameraRotateSpeed * ft);
        if (KeyHeld(KeyValue::KeyArrowRight))   mainCamera.Yaw(cameraRotateSpeed * ft);
        if (KeyHeld(KeyValue::KeyArrowUp))      mainCamera.Pitch(-cameraRotateSpeed * ft);
        if (KeyHeld(KeyValue::KeyArrowDown))    mainCamera.Pitch(cameraRotateSpeed * ft);
        mainCamera.UpdateViewMatrix();

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
    }
    else {
        mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
        mCurrFrameResourceRT = mFrameResourcesRT[mCurrFrameResourceIndex].get();

        if (mCurrFrameResourceRT->fence != 0 && mFence->GetCompletedValue() < mCurrFrameResourceRT->fence) {
            //HANDLE eventHandle = CreateEventEx(nullptr, NULL, CREATE_EVENT_INITIAL_SET, EVENT_ALL_ACCESS);
            HANDLE eventHandle = CreateEventEx(nullptr, 0, 0, EVENT_ALL_ACCESS);
            ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResourceRT->fence, eventHandle));
            WaitForSingleObject(eventHandle, INFINITE);
            CloseHandle(eventHandle);
        }
    }
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

    mainCamera.SetFrustrum(0.25f * DirectX::XM_PI, GetAspectRatio(), 1.0f, 1000.0f);
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
    opaquePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
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

void Dx12Renderer::BuildFrameResourcesRT()
{
    for (int i = 0; i < gNumFrameResources; ++i) {
        mFrameResourcesRT.push_back(std::make_unique<FrameResourceRT>(mD3DDevice.Get()));
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

LRESULT Dx12MasterProject::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(renderer->GetWindowHWND());
        KeyPressedEvent(static_cast<KeyValue>(wParam));
        //else if (wParam == VK_SPACE)  renderer->ToggleRaytracingState(); 
        break;

    case WM_KEYUP:
        KeyReleasedEvent(static_cast<KeyValue>(wParam));
        break;

    case WM_MOUSEMOVE:
        MouseMovementEvent(LOWORD(lParam), HIWORD(lParam));
        break;

    case WM_LBUTTONDOWN:
        KeyPressedEvent(KeyValue::MouseLeftButton);
        break;

    case WM_LBUTTONUP:
        KeyReleasedEvent(KeyValue::MouseLeftButton);
        break;

    case WM_RBUTTONDOWN:
        KeyPressedEvent(KeyValue::MouseRightButton);
        break;

    case WM_RBUTTONUP:
        KeyReleasedEvent(KeyValue::MouseRightButton);
        break;

    case WM_MBUTTONDOWN:
        KeyPressedEvent(KeyValue::MouseMiddleButton);
        break;

    case WM_MBUTTONUP:
        KeyReleasedEvent(KeyValue::MouseMiddleButton);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        delete renderer;
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

ID3D12Resource* Dx12Renderer::CreateBuffer(ID3D12Device5* device, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Flags = flags;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.MipLevels = 1;
    desc.DepthOrArraySize = 1;
    desc.Height = 1;
    desc.Width = size;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    ID3D12Resource* buffer;
    ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, initState, nullptr, IID_PPV_ARGS(&buffer)));
    return buffer;
}

ID3D12Resource* Dx12Renderer::CreateTriangleVB(ID3D12Device5* device)
{
    const DirectX::XMFLOAT3 vertices[] =
    {
        DirectX::XMFLOAT3(0,          1,  0),
        DirectX::XMFLOAT3(0.866f,  -0.5f, 0),
        DirectX::XMFLOAT3(-0.866f, -0.5f, 0),
    };

    ID3D12Resource* buffer = CreateBuffer(device, sizeof(vertices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
    uint8_t* data;
    buffer->Map(0, nullptr, (void**)&data);
    memcpy(data, vertices, sizeof(vertices));
    buffer->Unmap(0, nullptr);
    return buffer;
}

AccelerationStructBuffers Dx12Renderer::CreateBottomLevelAS(ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList, ID3D12Resource* vertBuff)
{
    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
    geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geomDesc.Triangles.VertexBuffer.StartAddress = vertBuff->GetGPUVirtualAddress();
    geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(DirectX::XMFLOAT3);
    geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geomDesc.Triangles.VertexCount = 3;
    geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    // Get the size requirements for the scratch and AS buffers
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geomDesc;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    // Create the buffers. They need to support UAV, and since we are going to immediately use them, we create them with an unordered-access state
    AccelerationStructBuffers buffers;
    buffers.pScratch = CreateBuffer(device, info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kDefaultHeapProps);
    buffers.pResult = CreateBuffer(device, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);

    // Create the bottom-level AS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs = inputs;
    asDesc.DestAccelerationStructureData = buffers.pResult->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = buffers.pScratch->GetGPUVirtualAddress();

    cmdList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    // We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = buffers.pResult.Get();
    cmdList->ResourceBarrier(1, &uavBarrier);

    return buffers;
}

AccelerationStructBuffers Dx12Renderer::CreateTopLevelAS(ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList, ID3D12Resource* botLvlAS, uint64_t& tlasSize)
{
    // First, get the size of the TLAS buffers and create them
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = mRTInstanceCount; 
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    // Create the buffers
    AccelerationStructBuffers buffers;
    buffers.pScratch = CreateBuffer(device, info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kDefaultHeapProps);
    buffers.pResult = CreateBuffer(device, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
    tlasSize = info.ResultDataMaxSizeInBytes;

    // The instance desc should be inside a buffer, create and map the buffer
    buffers.pInstanceDesc = CreateBuffer(device, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * mRTInstanceCount, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
    D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc;
    buffers.pInstanceDesc->Map(0, nullptr, (void**)&pInstanceDesc);
    ZeroMemory(pInstanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * mRTInstanceCount);

    DirectX::XMMATRIX trans[3];
    trans[0] = trans[1] = trans[2] = DirectX::XMMatrixIdentity();
    trans[1] = DirectX::XMMatrixTranslation(-2.0f, 0.0f, 0.0f);
    trans[2] = DirectX::XMMatrixTranslation(2.0f, 0.0f, 0.0f);

    for (uint32_t i = 0; i < mRTInstanceCount; i++) {
        // Initialize the instance desc. We only have a single instance
        pInstanceDesc[i].InstanceID = i;                            // This value will be exposed to the shader via InstanceID()
        pInstanceDesc[i].InstanceContributionToHitGroupIndex = 0;   // This is the offset inside the shader-table. We only have a single geometry, so the offset 0
        pInstanceDesc[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        DirectX::XMMATRIX m = DirectX::XMMatrixTranspose(trans[i]); // Identity matrix
        memcpy(pInstanceDesc[i].Transform, &m, sizeof(pInstanceDesc[i].Transform));
        pInstanceDesc[i].AccelerationStructure = botLvlAS->GetGPUVirtualAddress();
        pInstanceDesc[i].InstanceMask = 0xFF;
    }
    // Unmap
    buffers.pInstanceDesc->Unmap(0, nullptr);

    // Create the TLAS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs = inputs;
    asDesc.Inputs.InstanceDescs = buffers.pInstanceDesc->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = buffers.pResult->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = buffers.pScratch->GetGPUVirtualAddress();

    cmdList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    // We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = buffers.pResult.Get();
    cmdList->ResourceBarrier(1, &uavBarrier);

    return buffers;
}

ID3DBlob* Dx12Renderer::CompileLibrary(const WCHAR* filename, const WCHAR* targetString)
{
    // Initialize the helper
    ThrowIfFailed(gDxcDllHelper.Initialize());
    IDxcCompiler* pCompiler;
    IDxcLibrary* pLibrary;
    ThrowIfFailed(gDxcDllHelper.CreateInstance(CLSID_DxcCompiler, &pCompiler));
    ThrowIfFailed(gDxcDllHelper.CreateInstance(CLSID_DxcLibrary, &pLibrary));

    // Open and read the file
    std::ifstream shaderFile(filename);
    assert(shaderFile.good() == true && "Can't open file " );
    
    std::stringstream strStream;
    strStream << shaderFile.rdbuf();
    std::string shader = strStream.str();

    // Create blob from the string
    IDxcBlobEncoding* pTextBlob;
    ThrowIfFailed(pLibrary->CreateBlobWithEncodingFromPinned((LPBYTE)shader.c_str(), (uint32_t)shader.size(), 0, &pTextBlob));

    // Compile
    IDxcOperationResult* pResult;
    ThrowIfFailed(pCompiler->Compile(pTextBlob, filename, L"", targetString, nullptr, 0, nullptr, 0, nullptr, &pResult));

    // Verify the result
    HRESULT resultCode;
    ThrowIfFailed(pResult->GetStatus(&resultCode));
    if (FAILED(resultCode))
    {
        IDxcBlobEncoding* pError;
        ThrowIfFailed(pResult->GetErrorBuffer(&pError));
        std::string log = convertBlobToString(pError);
        return nullptr;
    }

    IDxcBlob* blob;
    ThrowIfFailed(pResult->GetResult(&blob));
    return reinterpret_cast<ID3DBlob*>(blob);
}

RootSigDesc Dx12Renderer::CreateRayGenRootDesc()
{
    // Create the root-signature
    RootSigDesc desc;
    desc.range.resize(2);
    // gOutput
    desc.range[0].BaseShaderRegister = 0;
    desc.range[0].NumDescriptors = 1;
    desc.range[0].RegisterSpace = 0;
    desc.range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    desc.range[0].OffsetInDescriptorsFromTableStart = 0;

    // gRtScene
    desc.range[1].BaseShaderRegister = 0;
    desc.range[1].NumDescriptors = 1;
    desc.range[1].RegisterSpace = 0;
    desc.range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    desc.range[1].OffsetInDescriptorsFromTableStart = 1;

    desc.rootParams.resize(1);
    desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    desc.rootParams[0].DescriptorTable.NumDescriptorRanges = 2;
    desc.rootParams[0].DescriptorTable.pDescriptorRanges = desc.range.data();

    // Create the desc
    desc.desc.NumParameters = 1;
    desc.desc.pParameters = desc.rootParams.data();
    desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return desc;
}

DxilLibrary Dx12Renderer::CreateDxilLibrary()
{
    // Compile the shader
    ID3DBlob* pDxilLib = CompileLibrary(L"RayGenShaders.hlsl", L"lib_6_3");
    const WCHAR* entryPoints[] = { RAY_GEN_SHADER, MISS_SHADER, CLOSEST_HIT_SHADER };
    return DxilLibrary(pDxilLib, entryPoints, sizeof(entryPoints)/sizeof(entryPoints[0]));
}

ID3D12DescriptorHeap* Dx12Renderer::CreateDescHeap(ID3D12Device5* device, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool bShaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = count;
    desc.Type = type;
    desc.Flags = bShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ID3D12DescriptorHeap* heap;
    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)));
    return heap;
}
