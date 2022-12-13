#pragma once
#include "Utility.h"
#include "UploadBuffer.h"

struct ObjectsConsts {
	DirectX::XMFLOAT4X4 world = IDENTITY_MATRIX;
};

struct PassConsts {
	DirectX::XMFLOAT4X4 view = IDENTITY_MATRIX;
	DirectX::XMFLOAT4X4 invView = IDENTITY_MATRIX;
	DirectX::XMFLOAT4X4 proj = IDENTITY_MATRIX;
	DirectX::XMFLOAT4X4 invProj = IDENTITY_MATRIX;
	DirectX::XMFLOAT4X4 viewProj = IDENTITY_MATRIX;
	DirectX::XMFLOAT4X4 invViewProj = IDENTITY_MATRIX;
	DirectX::XMFLOAT3 eyePos = { 0.0f, 0.0f, 0.0f };
	float cbPerObjectPad = 0.0f;
	DirectX::XMFLOAT2 renderTargetSize = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 invRenderTargetSize = { 0.0f, 0.0f };
	float nearZ = 0.0f;
	float farZ = 0.0f;
	float totalTime = 0.0f;
	float frameTime = 0.0f;
};

struct vertexConsts {
	DirectX::XMFLOAT3 pos;
	DirectX::XMFLOAT4 colour;
};

struct FrameResource
{
public:

	ComPtr<ID3D12CommandAllocator> cmdListAllocator;
	std::unique_ptr<UploadBuffer<PassConsts>> passCB = nullptr;
	std::unique_ptr<UploadBuffer<ObjectsConsts>> objectCB = nullptr;

	FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount) {
		auto cmdListAllocAddress = cmdListAllocator.GetAddressOf();
		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmdListAllocAddress)));
		passCB = std::make_unique<UploadBuffer<PassConsts>>(device, passCount, true);
		objectCB = std::make_unique<UploadBuffer<ObjectsConsts>>(device, objectCount, true);
	}
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource(){}

	UINT64 fence = 0;
};

