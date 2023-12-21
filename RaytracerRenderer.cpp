#pragma once
#include "Dx12Renderer.h"

using namespace Dx12MasterProject;

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
static const WCHAR* TRI_CLOSEST_HIT_SHADER = L"Hit";
static const WCHAR* PLANE_CLOSEST_HIT_SHADER = L"PlaneHit";
static const WCHAR* SHADOW_CLOSEST_HIT_SHADER = L"ShadowHit";
static const WCHAR* SHADOW_MISS_SHADER = L"ShadowMiss";
static const WCHAR* TRI_HIT_GROUP = L"tRIHitGroup";
static const WCHAR* PLANE_HIT_GROUP = L"PlaneHitGroup";
static const WCHAR* SHADOW_HIT_GROUP = L"ShadowHitGroup";


AccelerationStructBuffers Dx12Renderer::CreateBottomLevelAS(ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList, ID3D12Resource* vertBuff[], const uint32_t vertexCount[], ID3D12Resource* indexBuff[], const uint32_t indexCount[], uint32_t geomCount)
{
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDesc;
    geomDesc.resize(geomCount);

    for (uint32_t i = 0; i < geomCount; i++) {
        geomDesc[i].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geomDesc[i].Triangles.VertexBuffer.StartAddress = vertBuff[i]->GetGPUVirtualAddress();
        geomDesc[i].Triangles.VertexBuffer.StrideInBytes = sizeof(RTVertexBufferLayout);
        geomDesc[i].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geomDesc[i].Triangles.VertexCount = vertexCount[i];
        geomDesc[i].Triangles.IndexBuffer = indexBuff[i]->GetGPUVirtualAddress();
        geomDesc[i].Triangles.IndexCount = indexCount[i];
        geomDesc[i].Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;

        geomDesc[i].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    }

    // Get size requirements for scratch and AS buffers
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    inputs.NumDescs = geomCount;
    inputs.pGeometryDescs = geomDesc.data();
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    // Create Buffers
    AccelerationStructBuffers buffers;
    buffers.pScratch = CreateBuffer(device, info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kDefaultHeapProps);
    buffers.pResult = CreateBuffer(device, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);

    // Create bottom-level AS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs = inputs;
    asDesc.DestAccelerationStructureData = buffers.pResult->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = buffers.pScratch->GetGPUVirtualAddress();

    cmdList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = buffers.pResult.Get();
    cmdList->ResourceBarrier(1, &uavBarrier);
    geomDesc.resize(0);
    return buffers;
}

void Dx12Renderer::BuildTopLevelAS(ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList, ID3D12Resource* botLvlAS[], std::uint64_t& tlasSize, float rotation, bool bUpdate, AccelerationStructBuffers& buffers)
{
    // Create TLas
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    inputs.NumDescs = mRTInstanceCount; 
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    //
    if (bUpdate) {
        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = buffers.pResult.Get();
        cmdList->ResourceBarrier(1, &uavBarrier);
    }
    else {
        // Create the buffers
        buffers.pScratch = CreateBuffer(device, info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kDefaultHeapProps);
        buffers.pResult = CreateBuffer(device, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, kDefaultHeapProps);
        buffers.pInstanceDesc = CreateBuffer(device, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * mRTInstanceCount, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
        tlasSize = info.ResultDataMaxSizeInBytes;
    }

    D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc;
    buffers.pInstanceDesc->Map(0, nullptr, (void**)&pInstanceDesc);
    ZeroMemory(pInstanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * mRTInstanceCount);

    DirectX::XMMATRIX trans[3];
    DirectX::XMMATRIX rotationMat = DirectX::XMMatrixRotationY(rotation);
    trans[0] = trans[1] = trans[2] = DirectX::XMMatrixIdentity();
    trans[1] = rotationMat * DirectX::XMMatrixTranslation(-2.0f, 0.0f, 0.0f);
    trans[2] = rotationMat * DirectX::XMMatrixTranslation(2.0f, 0.0f, 0.0f);

    pInstanceDesc[0].InstanceID = 0;
    pInstanceDesc[0].InstanceContributionToHitGroupIndex = 0;
    pInstanceDesc[0].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    memcpy(pInstanceDesc[0].Transform, &trans[0], sizeof(pInstanceDesc[0].Transform));
    pInstanceDesc[0].AccelerationStructure = botLvlAS[0]->GetGPUVirtualAddress();
    pInstanceDesc[0].InstanceMask = 0xFF;
    

    for (uint32_t i = 1; i < mRTInstanceCount; i++) {
        // Initialize instance desc.
        pInstanceDesc[i].InstanceID = i;                           
        pInstanceDesc[i].InstanceContributionToHitGroupIndex = (i* 2) + 2; //shader table offset
        pInstanceDesc[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        DirectX::XMMATRIX m = DirectX::XMMatrixTranspose(trans[i]); 
        memcpy(pInstanceDesc[i].Transform, &m, sizeof(pInstanceDesc[i].Transform));
        pInstanceDesc[i].AccelerationStructure = botLvlAS[1]->GetGPUVirtualAddress();
        pInstanceDesc[i].InstanceMask = 0xFF;
    }
    // Unmap
    buffers.pInstanceDesc->Unmap(0, nullptr);

    // Create TLAS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
    asDesc.Inputs = inputs;
    asDesc.Inputs.InstanceDescs = buffers.pInstanceDesc->GetGPUVirtualAddress();
    asDesc.DestAccelerationStructureData = buffers.pResult->GetGPUVirtualAddress();
    asDesc.ScratchAccelerationStructureData = buffers.pScratch->GetGPUVirtualAddress();

    if (bUpdate) {
        asDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        asDesc.SourceAccelerationStructureData = buffers.pResult->GetGPUVirtualAddress();
    }

    cmdList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = buffers.pResult.Get();
    cmdList->ResourceBarrier(1, &uavBarrier);

}

void Dx12Renderer::CreateAccelerationStructures()
{
    int index = 0;
    CreateTriangleVB(mD3DDevice.Get(), mVertexBuffer->GetAddressOf(), mIndexBuffer->GetAddressOf(), 0);
    CreatePlaneVB(mD3DDevice.Get(), mVertexBuffer->GetAddressOf(), mIndexBuffer->GetAddressOf(), 1, 100.0f, 100.0f, -1.0f);
    //CreateCubeVB(mD3DDevice.Get(), mVertexBuffer->GetAddressOf(), mIndexBuffer->GetAddressOf(), 2, 0.5f, 0.5f, 0.5f);
    AccelerationStructBuffers botLevelBuffers[2];

    const uint32_t vertexCount[] = { 3, 4 };// , 8};
    const uint32_t indexCount[] = { 3, 6 };// , 36 };
    botLevelBuffers[0] = CreateBottomLevelAS(mD3DDevice.Get(), mCommandList.Get(), mVertexBuffer->GetAddressOf(), vertexCount, mIndexBuffer->GetAddressOf(), indexCount, 2);
    mBotLvlAS[0] = botLevelBuffers[0].pResult;
    botLevelBuffers[1] = CreateBottomLevelAS(mD3DDevice.Get(), mCommandList.Get(), mVertexBuffer->GetAddressOf(), vertexCount, mIndexBuffer->GetAddressOf(), indexCount, 1);
    mBotLvlAS[1] = botLevelBuffers[1].pResult;

    BuildTopLevelAS(mD3DDevice.Get(), mCommandList.Get(), mBotLvlAS->GetAddressOf(), mTlasSize, 0, false, mTopLvlBuffers);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    FlushCommandQueue();
}

void Dx12Renderer::CreateRtPipelineState()
{
    // Need 16 subobjects:
    //  1 for the DXIL library
    //  3 for hit-groups (tri, plane, shadow)
    //  2 for RayGen root-signature (root-signature and the subobject association)
    //  2 for the tri root-signature hit shaders (signature and association)
    //  2 for the plane root-signature hit shaders (signature and association)
    //  2 for the shadow root-signature miss shaders (signature and association)
    //  2 for shader config (shared between all programs. 1 for the config, 1 for association)
    //  1 for pipeline config
    //  1 for the global root signature
    std::array<D3D12_STATE_SUBOBJECT, 16> subObj;
    uint32_t index = 0;

    // Create the DXIL library
    DxilLibrary dxilLib = CreateDxilLibrary();
    subObj[index++] = dxilLib.stateSubobj; // 0 Library

    HitProgram triHitProgram(nullptr, TRI_CLOSEST_HIT_SHADER, TRI_HIT_GROUP);
    subObj[index++] = triHitProgram.subObj; // 1 Hit Group

    HitProgram planeHitProgram(nullptr, PLANE_CLOSEST_HIT_SHADER, PLANE_HIT_GROUP);
    subObj[index++] = planeHitProgram.subObj; // 2 Hit Group

    HitProgram shadowHitProgram(nullptr, SHADOW_CLOSEST_HIT_SHADER, SHADOW_HIT_GROUP);
    subObj[index++] = shadowHitProgram.subObj; // 3 Hit Group

    // Create the ray-gen root-signature and association
    LocalRootSig rgsRootSignature(mD3DDevice.Get(), CreateRayGenRootDesc().desc);
    subObj[index] = rgsRootSignature.subObj; // 4 RayGen Root Sig

    uint32_t rgsRootIndex = index++; // 4
    ExportAssociation rgsRootAssociation(&RAY_GEN_SHADER, 1, &(subObj[rgsRootIndex]));
    subObj[index++] = rgsRootAssociation.subObj; // 5 Associate Root Sig to RGS

    //Hit Root Signature and Associations 
    LocalRootSig triHitRootSig(mD3DDevice.Get(), CreateTriHitRootDesc().desc);
    subObj[index] = triHitRootSig.subObj;

    uint32_t hitRootIndex = index++; //6
    ExportAssociation triHitRootAssociation(&TRI_CLOSEST_HIT_SHADER, 1, &(subObj[hitRootIndex]));
    subObj[index++] = triHitRootAssociation.subObj; //7

    //Hit Root Signature and Associations 
    LocalRootSig planeHitRootSig(mD3DDevice.Get(), CreatePlaneHitRootDesc().desc);
    subObj[index] = planeHitRootSig.subObj;

    uint32_t planeHitRootIndex = index++; //8
    ExportAssociation planeHitRootAssociation(&PLANE_HIT_GROUP, 1, &(subObj[planeHitRootIndex]));
    subObj[index++] = planeHitRootAssociation.subObj; //9


    // Create the miss- and hit-programs root-signature and association
    D3D12_ROOT_SIGNATURE_DESC emptyDesc = {};
    emptyDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    LocalRootSig missRootSignature(mD3DDevice.Get(), emptyDesc);
    subObj[index] = missRootSignature.subObj; // 10

    uint32_t missRootIndex = index++; // 10
    const WCHAR* emptyRootExport[] = {  SHADOW_CLOSEST_HIT_SHADER, MISS_SHADER, SHADOW_MISS_SHADER };
    ExportAssociation missRootAssociation(emptyRootExport, (sizeof(emptyRootExport) / sizeof(emptyRootExport[0])), &(subObj[missRootIndex]));
    subObj[index++] = missRootAssociation.subObj; // 11

    // Bind the payload size to the programs
    ShaderConfig shaderConfig(sizeof(float) * 2, sizeof(float) * 3);
    subObj[index] = shaderConfig.subObj; // 12 Shader Config

    uint32_t shaderConfigIndex = index++; //12
    const WCHAR* shaderExports[] = { MISS_SHADER, TRI_CLOSEST_HIT_SHADER, PLANE_CLOSEST_HIT_SHADER, RAY_GEN_SHADER, SHADOW_CLOSEST_HIT_SHADER, SHADOW_MISS_SHADER};
    ExportAssociation configAssociation(shaderExports, (sizeof(shaderExports) / sizeof(shaderExports[0])), &(subObj[shaderConfigIndex]));
    subObj[index++] = configAssociation.subObj; // 13 Associate Shader Config to shaders

    // Create the pipeline config
    PipelineConfig config(2);
    subObj[index++] = config.subObj; // 14

    // Create the global root signature and store the empty signature
    GlobalRootSig root(mD3DDevice.Get(), {});
    mEmptyRootSig = root.rootSig;
    subObj[index++] = root.subObj; // 15

    // Create the state
    D3D12_STATE_OBJECT_DESC desc;
    desc.NumSubobjects = index; // 16
    desc.pSubobjects = subObj.data();
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

    ThrowIfFailed(mD3DDevice->CreateStateObject(&desc, IID_PPV_ARGS(&mPipelineState)));
}

void Dx12Renderer::CreateShaderTable()
{
    /** The shader-table layout is as follows:
       Entry 0 - Ray-gen program
       Entry 1 - Miss primary ray
       Entry 2 - Miss shadow ray
       Entry 3,4 - Hit program tri 0
       Entry 5,6 - Hit program plane
       Entry 7,8 - Hit program tri 1
       Entry 9,10 - Hit program tri 2
       Entry 11,12 - Hit Program cube
   */

    mShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    mShaderTableEntrySize += 8;
    mShaderTableEntrySize = Utility::RoundUp(mShaderTableEntrySize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    uint32_t shaderTableSize = mShaderTableEntrySize * 13;


    mShaderTable = CreateBuffer(mD3DDevice.Get(), shaderTableSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);

    // Map buffer
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

    //Entry 2 - miss Shadow
    memcpy(pData + mShaderTableEntrySize * 2, pRtsoProps->GetShaderIdentifier(SHADOW_MISS_SHADER), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // Entry 3 - tri 0, pri ray hit program
    uint8_t* pHitEntry3 = pData + mShaderTableEntrySize * 3; // +2 skips the ray-gen and miss entries
    memcpy(pHitEntry3, pRtsoProps->GetShaderIdentifier(TRI_HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    uint8_t* pCBDesc = pHitEntry3 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    assert(((uint64_t)pCBDesc % 8) == 0);
    *(D3D12_GPU_VIRTUAL_ADDRESS*)pCBDesc = mConstantBufferRT[0]->GetGPUVirtualAddress();

    //Entry 4 - tri 0 shadow ray hit program
    uint8_t* pHitEntry4 = pData + mShaderTableEntrySize * 4;
    memcpy(pHitEntry4, pRtsoProps->GetShaderIdentifier(SHADOW_HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // Entry 5 -  plane Pri ray hit program
    uint8_t* pHitEntry5 = pData + mShaderTableEntrySize * 5;
    memcpy(pHitEntry5, pRtsoProps->GetShaderIdentifier(PLANE_HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    *(uint64_t*)(pHitEntry5 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = heapStart + mD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    
    //Entry 6 - plane Shadow ray hit program
    uint8_t* pHitEntry6 = pData + mShaderTableEntrySize * 6;
    memcpy(pHitEntry6, pRtsoProps->GetShaderIdentifier(SHADOW_HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // Entry 7 - tri 1 primary ray hit program
    uint8_t* pHitEntry7 = pData + mShaderTableEntrySize * 7; 
    memcpy(pHitEntry7, pRtsoProps->GetShaderIdentifier(TRI_HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    uint8_t* pCBDesc1 = pHitEntry7 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    assert(((uint64_t)pCBDesc1 % 8) == 0);
    *(D3D12_GPU_VIRTUAL_ADDRESS*)pCBDesc1 = mConstantBufferRT[1]->GetGPUVirtualAddress();

    // Entry 8 - tri 1 Shadow ray hit program
    uint8_t* pHitEntry8 = pData + mShaderTableEntrySize * 8;
    memcpy(pHitEntry8, pRtsoProps->GetShaderIdentifier(SHADOW_HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // Entry 9 - tri 2 primary ray hit program
    uint8_t* pHitEntry9 = pData + mShaderTableEntrySize * 9; 
    memcpy(pHitEntry9, pRtsoProps->GetShaderIdentifier(TRI_HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    uint8_t* pCBDesc2 = pHitEntry9 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    assert(((uint64_t)pCBDesc2 % 8) == 0);
    *(D3D12_GPU_VIRTUAL_ADDRESS*)pCBDesc2 = mConstantBufferRT[2]->GetGPUVirtualAddress();

    //Entry 10 - tri 2 shadow ray hit program
    uint8_t* pHitEntry10 = pData + mShaderTableEntrySize * 10;
    memcpy(pHitEntry10, pRtsoProps->GetShaderIdentifier(SHADOW_HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    //Entry 11
    uint8_t* pHitEntry11 = pData + mShaderTableEntrySize * 11; // +2 skips the ray-gen and miss entries
    memcpy(pHitEntry11, pRtsoProps->GetShaderIdentifier(TRI_HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    uint8_t* pCBDesc3 = pHitEntry11 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    assert(((uint64_t)pCBDesc3 % 8) == 0);
    *(D3D12_GPU_VIRTUAL_ADDRESS*)pCBDesc3 = mConstantBufferRT[0]->GetGPUVirtualAddress();

    //Entry 12 - tri 0 shadow ray hit program
    uint8_t* pHitEntry12 = pData + mShaderTableEntrySize * 12;
    memcpy(pHitEntry12, pRtsoProps->GetShaderIdentifier(SHADOW_HIT_GROUP), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // Unmap
    mShaderTable->Unmap(0, nullptr);
}

void Dx12Renderer::CreateConstantBufferRT()
{
    DirectX::XMFLOAT4 bufferData[] = {
        //A
        DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f),
        DirectX::XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f),
        DirectX::XMFLOAT4(1.0f, 0.0f, 1.0f, 1.0f),

        //B
        DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f),
        DirectX::XMFLOAT4(0.0f, 1.0f, 1.0f, 1.0f),
        DirectX::XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f),

        //C
        DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f),
        DirectX::XMFLOAT4(1.0f, 0.0f, 1.0f, 1.0f),
        DirectX::XMFLOAT4(0.0f, 1.0f, 1.0f, 1.0f),
    };

    for (uint32_t i = 0; i < 3; i++) {
        const uint32_t buffSize = sizeof(DirectX::XMFLOAT4) *  3;
        mConstantBufferRT[i] = CreateBuffer(mD3DDevice.Get(), sizeof(buffSize), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
        uint8_t* pData;
        ThrowIfFailed(mConstantBufferRT[i]->Map(0, nullptr, (void**)&pData));
        memcpy(pData, &bufferData[i * 3], sizeof(bufferData));
        mConstantBufferRT[i]->Unmap(0, nullptr);
    }
}

void Dx12Renderer::CreateShaderResources()
{
    
    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.DepthOrArraySize = 1;
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; 
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resDesc.Height = mClientHeight;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Width = mClientWidth;
    ThrowIfFailed(mD3DDevice->CreateCommittedResource(&kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&mOutputResource))); 

    // 2 entries - 1 SRV scene and 1 UAV output
    mSrvUavHeap = CreateDescHeap(mD3DDevice.Get(), 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    mD3DDevice->CreateUnorderedAccessView(mOutputResource.Get(), nullptr, &uavDesc, mSrvUavHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = mTopLvlBuffers.pResult->GetGPUVirtualAddress();
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = mSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
    srvHandle.ptr += mD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    mD3DDevice->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
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

void Dx12Renderer::CreateTriangleVB(ID3D12Device5* device, ID3D12Resource* vertexBuff[], ID3D12Resource* indexBuff[], int index)
{
    const RTVertexBufferLayout vertices[] =
    {
        RTVertexBufferLayout{ DirectX::XMFLOAT3(0,          1,  0), DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f)},
        RTVertexBufferLayout{ DirectX::XMFLOAT3(0.866f,  -0.5f, 0), DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f)},
        RTVertexBufferLayout{ DirectX::XMFLOAT3(-0.866f, -0.5f, 0), DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f)},
    };  

    const int indices[] = {
        0, 1, 2
    };

    vertexBuff[index] = CreateBuffer(device, sizeof(vertices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
    uint8_t* data;
    vertexBuff[index]->Map(0, nullptr, (void**)&data);
    memcpy(data, vertices, sizeof(vertices));
    vertexBuff[index]->Unmap(0, nullptr);

    indexBuff[index] = CreateBuffer(device, sizeof(indices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
    uint8_t* data1;
    indexBuff[index]->Map(0, nullptr, (void**)&data1);
    memcpy(data1, indices, sizeof(indices));
    indexBuff[index]->Unmap(0, nullptr);
}

void Dx12Renderer::CreateCubeVB(ID3D12Device5* device, ID3D12Resource* vertexBuff[], ID3D12Resource* indexBuff[], int index, float width, float height, float length)
{
    const RTVertexBufferLayout vertices[] =
    {
      RTVertexBufferLayout{DirectX::XMFLOAT3(-width, -height, -length), DirectX::XMFLOAT3(-0.577350f, -0.577350f, -0.577350f)},
      RTVertexBufferLayout{DirectX::XMFLOAT3(-width, +height, -length), DirectX::XMFLOAT3(-0.408248f,  0.816497f, -0.408248f)},
      RTVertexBufferLayout{DirectX::XMFLOAT3(+width, +height, -length), DirectX::XMFLOAT3( 0.408248f,  0.408248f, -0.816497f)},
      RTVertexBufferLayout{DirectX::XMFLOAT3(+width, -height, -length), DirectX::XMFLOAT3( 0.816497f, -0.408248f, -0.408248f)},
      RTVertexBufferLayout{DirectX::XMFLOAT3(-width, -height, +length), DirectX::XMFLOAT3(-0.408248f, -0.408248f,  0.816497f)},
      RTVertexBufferLayout{DirectX::XMFLOAT3(-width, +height, +length), DirectX::XMFLOAT3(-0.816497f,  0.408248f,  0.408248f)},
      RTVertexBufferLayout{DirectX::XMFLOAT3(+width, +height, +length), DirectX::XMFLOAT3( 0.577350f,  0.577350f,  0.577350f)},
      RTVertexBufferLayout{DirectX::XMFLOAT3(+width, -height, +length), DirectX::XMFLOAT3( 0.408248f, -0.816497f,  0.408248f)},
    };

    const int indices[] = {
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

    vertexBuff[index] = CreateBuffer(device, sizeof(vertices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
    uint8_t* data;
    vertexBuff[index]->Map(0, nullptr, (void**)&data);
    memcpy(data, vertices, sizeof(vertices));
    vertexBuff[index]->Unmap(0, nullptr);

    indexBuff[index] = CreateBuffer(device, sizeof(indices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
    uint8_t* data1;
    indexBuff[index]->Map(0, nullptr, (void**)&data1);
    memcpy(data1, indices, sizeof(indices));
    indexBuff[index]->Unmap(0, nullptr);
}

void Dx12Renderer::CreatePlaneVB(ID3D12Device5* device, ID3D12Resource* vertexBuff[], ID3D12Resource* indexBuff[], int index, float width, float length, float heightOffset)
{
    const RTVertexBufferLayout vertices[] =
    {
        RTVertexBufferLayout{DirectX::XMFLOAT3(-width, heightOffset, -length), DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f)},
        RTVertexBufferLayout{DirectX::XMFLOAT3( width, heightOffset, -length), DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f)},
        RTVertexBufferLayout{DirectX::XMFLOAT3( width, heightOffset,  length), DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f)},
        RTVertexBufferLayout{DirectX::XMFLOAT3(-width, heightOffset,  length), DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f)},
    };

    const int indices[] = {
        1, 0, 2, 2, 0, 3
    };

    vertexBuff[index] = CreateBuffer(device, sizeof(vertices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
    uint8_t* data;
    vertexBuff[index]->Map(0, nullptr, (void**)&data);
    memcpy(data, vertices, sizeof(vertices));
    vertexBuff[index]->Unmap(0, nullptr);

    indexBuff[index] = CreateBuffer(device, sizeof(indices), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, kUploadHeapProps);
    uint8_t* data1;
    indexBuff[index]->Map(0, nullptr, (void**)&data1);
    memcpy(data1, indices, sizeof(indices));
    indexBuff[index]->Unmap(0, nullptr);
}


ID3DBlob* Dx12Renderer::CompileLibrary(const WCHAR* filename, const WCHAR* targetString)
{
    // Initialize helper
    ThrowIfFailed(gDxcDllHelper.Initialize());
    IDxcCompiler* pCompiler;
    IDxcLibrary* pLibrary;
    ThrowIfFailed(gDxcDllHelper.CreateInstance(CLSID_DxcCompiler, &pCompiler));
    ThrowIfFailed(gDxcDllHelper.CreateInstance(CLSID_DxcLibrary, &pLibrary));

    std::ifstream shaderFile(filename);
    assert(shaderFile.good() == true && "Can't open file " );
    
    std::stringstream strStream;
    strStream << shaderFile.rdbuf();
    std::string shader = strStream.str();

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

    desc.desc.NumParameters = 1;
    desc.desc.pParameters = desc.rootParams.data();
    desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return desc;
}

RootSigDesc Dx12Renderer::CreateTriHitRootDesc()
{
    RootSigDesc desc;
    desc.rootParams.resize(1);
    desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    desc.rootParams[0].Descriptor.RegisterSpace = 0;
    desc.rootParams[0].Descriptor.ShaderRegister = 0;

    desc.desc.NumParameters = 1;
    desc.desc.pParameters = desc.rootParams.data();
    desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return desc;
}

RootSigDesc Dx12MasterProject::Dx12Renderer::CreatePlaneHitRootDesc()
{
    RootSigDesc desc;
    desc.range.resize(1);
    desc.range[0].BaseShaderRegister = 0;
    desc.range[0].NumDescriptors = 1;
    desc.range[0].RegisterSpace = 0;
    desc.range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    desc.range[0].OffsetInDescriptorsFromTableStart = 0;

    desc.rootParams.resize(1);
    desc.rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    desc.rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    desc.rootParams[0].DescriptorTable.pDescriptorRanges = desc.range.data();

    desc.desc.NumParameters = 1;
    desc.desc.pParameters = desc.rootParams.data();
    desc.desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    return desc;
}

DxilLibrary Dx12Renderer::CreateDxilLibrary()
{
    // Compile shader
    ID3DBlob* pDxilLib = CompileLibrary(L"RayGenShaders.hlsl", L"lib_6_3");
    const WCHAR* entryPoints[] = { RAY_GEN_SHADER, MISS_SHADER, TRI_CLOSEST_HIT_SHADER, PLANE_CLOSEST_HIT_SHADER, SHADOW_CLOSEST_HIT_SHADER, SHADOW_MISS_SHADER };
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
