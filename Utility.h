#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wrl.h>
#include <comdef.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <dxcapi.h>
#include "dxcapi.use.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <array>
#include <locale>
#include <codecvt>
#include <unordered_map>
#include <unordered_set>
#include "d3dx12.h"

using Microsoft::WRL::ComPtr;

//Frank D Luna Helper Functions (swap for own post triangle)
inline std::wstring AnsiToWString(const std::string& str) {
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

template<class blobType>
std::string convertBlobToString(blobType* blob)
{
	std::vector<char> infoLog(blob->GetBufferSize() + 1);
	memcpy(infoLog.data(), blob->GetBufferPointer(), blob->GetBufferSize());
	infoLog[blob->GetBufferSize()] = 0;
	return std::string(infoLog.data());
}

std::string wstring_2_string(const std::wstring& ws);


#ifndef ThrowIfFailed
#define ThrowIfFailed(x) {										\
HRESULT hr_ = (x);												\
std::wstring wfn = AnsiToWString(__FILE__);						\
if (FAILED(hr_))throw DxException(hr_, L#x, wfn, __LINE__);		\
}																
#endif

class DxException {
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& fileName, int lineNum) :
		errorCode(hr), functionName(functionName), fileName(fileName), lineNum(lineNum) {}
	std::wstring ToString() const {
		_com_error err(errorCode);
		std::wstring msg = err.ErrorMessage();
		return functionName + L" failed in " + fileName + L"; line " + std::to_wstring(lineNum) + L"; error: " + msg;
	};
	HRESULT errorCode = S_OK;
	std::wstring functionName;
	std::wstring fileName;
	int lineNum = -1;
};

class Utility
{
public:
	static UINT CalcConstantBufferByteSize(UINT byteSize) {
		return (byteSize + 255) & ~255;
	}
	
	static UINT RoundUp(UINT x, UINT pow2Alignment) {
		return x + (pow2Alignment - 1) & ~(pow2Alignment - 1);
	}

	static ComPtr<ID3DBlob> CompileShader(
		const std::wstring& fileName,
		const D3D_SHADER_MACRO* defines,
		const std::string& entryPoint,
		const std::string& target) {
		UINT compileFlags = 0;
		
#if defined(DEBUG) || defined(_DEBUG)
		compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
		HRESULT hr = S_OK;

		ComPtr<ID3DBlob> byteCode = nullptr;
		ComPtr<ID3DBlob> errors;
		hr = D3DCompileFromFile(fileName.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);
		if (errors != nullptr)OutputDebugStringA((char*)errors->GetBufferPointer());
		ThrowIfFailed(hr);
		return byteCode;
	}

	static ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer)
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

		D3D12_SUBRESOURCE_DATA subResourceData = { initData, byteSize, subResourceData.RowPitch };

		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST);
		cmdList->ResourceBarrier(1, &barrier);
		UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
		barrier = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_GENERIC_READ);
		cmdList->ResourceBarrier(1, &barrier);

		return defaultBuffer;
	}

	static DirectX::XMFLOAT4X4 XMFLOAT4X4Multiply(DirectX::XMFLOAT4X4 a, DirectX::XMFLOAT4X4 b) {
		DirectX::XMFLOAT4X4 c;
		c._11 = a._11 * b._11 + a._12 * b._21 + a._13 * b._31 + a._14 * b._41;
		c._12 = a._11 * b._12 + a._12 * b._22 + a._13 * b._32 + a._14 * b._42;
		c._13 = a._11 * b._13 + a._12 * b._23 + a._13 * b._33 + a._14 * b._43;
		c._14 = a._11 * b._14 + a._12 * b._24 + a._13 * b._34 + a._14 * b._44;

		c._21 = a._21 * b._11 + a._22 * b._21 + a._23 * b._31 + a._24 * b._41;
		c._22 = a._21 * b._12 + a._22 * b._22 + a._23 * b._32 + a._24 * b._42;
		c._23 = a._21 * b._13 + a._22 * b._23 + a._23 * b._33 + a._24 * b._43;
		c._24 = a._21 * b._14 + a._22 * b._24 + a._23 * b._34 + a._24 * b._44;

		c._31 = a._31 * b._11 + a._32 * b._21 + a._33 * b._31 + a._34 * b._41;
		c._32 = a._31 * b._12 + a._32 * b._22 + a._33 * b._32 + a._34 * b._42;
		c._33 = a._31 * b._13 + a._32 * b._23 + a._33 * b._33 + a._34 * b._43;
		c._34 = a._31 * b._14 + a._32 * b._24 + a._33 * b._34 + a._34 * b._44;

		c._41 = a._41 * b._11 + a._42 * b._21 + a._43 * b._31 + a._44 * b._41;
		c._42 = a._41 * b._12 + a._42 * b._22 + a._43 * b._32 + a._44 * b._42;
		c._43 = a._41 * b._13 + a._42 * b._23 + a._43 * b._33 + a._44 * b._43;
		c._44 = a._41 * b._14 + a._42 * b._24 + a._43 * b._34 + a._44 * b._44;
		return c;
	}

	static DirectX::XMFLOAT4X4 XMFLOAT4X4Transpose(DirectX::XMFLOAT4X4 matrix) {
		DirectX::XMFLOAT4X4 a = matrix;
		std::swap(a._12, a._21);
		std::swap(a._13, a._31);
		std::swap(a._14, a._41);
		std::swap(a._23, a._32);
		std::swap(a._24, a._42);
		std::swap(a._34, a._43);

		return a;
	}
};

const DirectX::XMFLOAT4X4 IDENTITY_MATRIX = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
};


//
//struct SubmeshGeometry {
//	UINT IndexCount = 0;
//	UINT StartIndexLocation = 0;
//	INT BaseVertexLocation = 0;
//	DirectX::BoundingBox Bounds;
//};
//
//struct MeshGeometry {
//	std::string name;
//	ComPtr<ID3DBlob> vertexBufferCPU;
//	ComPtr<ID3DBlob> indexBufferCPU;
//	ComPtr<ID3D12Resource> vertexBufferGPU;
//	ComPtr<ID3D12Resource> indexBufferGPU;
//	ComPtr<ID3D12Resource> vertexBufferUploader;
//	ComPtr<ID3D12Resource> indexBufferUploader;
//
//	UINT vertexByteStride = 0;
//	UINT vertexBufferByteSize = 0;
//	DXGI_FORMAT indexFormat = DXGI_FORMAT_R16_UINT;
//	UINT indexBufferByteSize = 0;
//
//	std::unordered_map<std::string, SubmeshGeometry> drawArgs;
//
//	D3D12_VERTEX_BUFFER_VIEW VertexBufferView()const {
//		D3D12_VERTEX_BUFFER_VIEW vbv;
//		vbv.BufferLocation = vertexBufferGPU->GetGPUVirtualAddress();
//		vbv.StrideInBytes = vertexByteStride;
//		vbv.SizeInBytes = vertexBufferByteSize;
//		return vbv;
//	}
//
//	D3D12_INDEX_BUFFER_VIEW IndexBufferView()const {
//		D3D12_INDEX_BUFFER_VIEW ibv;
//		ibv.BufferLocation = indexBufferGPU->GetGPUVirtualAddress();
//		ibv.Format = indexFormat;
//		ibv.SizeInBytes = indexBufferByteSize;
//		return ibv;
//	}
//
//	void DisposeUploaders() {
//		vertexBufferUploader = nullptr;
//		indexBufferUploader = nullptr;
//	}
//};