#pragma once
#include <Windows.h>
#include <wrl.h>
#include <comdef.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <memory>
#include <vector>
#include <array>
#include <unordered_map>
#include "d3dx12.h"

using Microsoft::WRL::ComPtr;

//Frank D Luna Helper Functions (swap for own post triangle)
inline std::wstring AnsiToWString(const std::string& str) {
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

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
};

const DirectX::XMFLOAT4X4 IDENTITY_MATRIX = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
};

struct SubmeshGeometry {
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;
	DirectX::BoundingBox Bounds;
};

struct MeshGeometry {
	std::string name;
	ComPtr<ID3DBlob> vertexBufferCPU;
	ComPtr<ID3DBlob> indexBufferCPU;
	ComPtr<ID3D12Resource> vertexBufferGPU;
	ComPtr<ID3D12Resource> indexBufferGPU;
	ComPtr<ID3D12Resource> vertexBufferUploader;
	ComPtr<ID3D12Resource> indexBufferUploader;

	UINT vertexByteStride = 0;
	UINT vertexBufferByteSize = 0;
	DXGI_FORMAT indexFormat = DXGI_FORMAT_R16_UINT;
	UINT indexBufferByteSize = 0;

	std::unordered_map<std::string, SubmeshGeometry> drawArgs;

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView()const {
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = vertexBufferGPU->GetGPUVirtualAddress();
		vbv.StrideInBytes = vertexByteStride;
		vbv.SizeInBytes = vertexBufferByteSize;
		return vbv;
	}

	D3D12_INDEX_BUFFER_VIEW IndexBufferView()const {
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = indexBufferGPU->GetGPUVirtualAddress();
		ibv.Format = indexFormat;
		ibv.SizeInBytes = indexBufferByteSize;
		return ibv;
	}

	void DisposeUploaders() {
		vertexBufferUploader = nullptr;
		indexBufferUploader = nullptr;
	}
};