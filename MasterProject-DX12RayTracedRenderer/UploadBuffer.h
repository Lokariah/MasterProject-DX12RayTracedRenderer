#pragma once
#include "Utility.h"

template<typename T>
class UploadBuffer
{
	public:
		UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) : mIsConstantBuffer(isConstantBuffer) {
			mElementByteSize = sizeof(T);

			auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount);
			if (isConstantBuffer) mElementByteSize = Utility::CalcConstantBufferByteSize(sizeof(T));
			ThrowIfFailed(device->CreateCommittedResource(
				&heapProperties,
				D3D12_HEAP_FLAG_NONE,
				&bufferDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&mUploadBuffer)));
			ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));
		}
		UploadBuffer(const UploadBuffer& temp) = delete;
		UploadBuffer& operator= (const UploadBuffer& temp) = delete;
		~UploadBuffer() {
			if (mUploadBuffer != nullptr) mUploadBuffer->Unmap(0, nullptr);
			mMappedData = nullptr;
		}

		ID3D12Resource* Resource()const {
			return mUploadBuffer.Get();
		}

		void CopyData(int elementIndex, const T& data) {
			memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
		}

private:
	ComPtr<ID3D12Resource> mUploadBuffer;
	BYTE* mMappedData = nullptr;
	UINT mElementByteSize = 0;
	bool mIsConstantBuffer = false;
};
