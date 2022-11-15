#pragma once
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <DirectXColors.h>
#include <wrl.h>
//#include "Win32Wnd.h"
#include <d3dcompiler.h>
#include <comdef.h>
#include <string>


//Frank D Luna Helper Functions (swap for own post triangle)
inline std::wstring AnsiToWString(const std::string& str) {
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

class DxException {
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& fileName, int lineNum):
		errorCode(hr), functionName(functionName), fileName(fileName), lineNum(lineNum){}
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

#ifndef ThrowIfFailed
#define ThrowIfFailed(x) {										\
HRESULT hr_ = (x);												\
std::wstring wfn = AnsiToWString(__FILE__);						\
if (FAILED(hr_))throw DxException(hr_, L#x, wfn, __LINE__);		\
}																
#endif

class Dx12Renderer
{
public:
	bool Initialise(HINSTANCE hInstance, int nShowCmd);

protected:
	bool InitialiseDirect3D();
	void Draw();
	void CreateCommandObjects();
	void CreateSwapChain();
	void CreateRTVAndDSVDescriptorHeaps();
	void OnResize();
	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer() const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

	UINT mRTVDescriptorSize = 0;
	UINT mDSVDescriptorSize = 0;
	UINT mCBVSRVUAVDescriptorSize = 0;

	UINT m4xMSAAQuality = 0;
	bool m4xMSAAState = false;

	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	static const UINT SWAP_CHAIN_BUFFER_COUNT = 2;
	int mCurrBackBuffer = 0;
	UINT64 mCurrFence = 0;

	Microsoft::WRL::ComPtr<IDXGIFactory4>				mDXGIFactory;
	Microsoft::WRL::ComPtr<ID3D12Device>				mD3DDevice;
	Microsoft::WRL::ComPtr<IDXGISwapChain>				mSwapChain;
	Microsoft::WRL::ComPtr<ID3D12Resource>				mSwapChainBuffer[SWAP_CHAIN_BUFFER_COUNT];
	Microsoft::WRL::ComPtr<ID3D12Resource>				mDepthStencilBuffer;
	Microsoft::WRL::ComPtr<ID3D12Fence>					mFence;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue>			mCommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator>		mDirectCmdListAlloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>   mCommandList;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>		mRTVHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>		mDSVHeap;

	D3D12_VIEWPORT vp;
	D3D12_RECT mScissorRect;

	int mClientWidth = 1980;
	int mClientHeight = 1080;

	//1- ID3D12Device
	//2- ID3D12Fence
	//3- 4X MSAA
	//4- CommandQueue, CommandListAllocator, CommandList
	//5- SwapChain
	//6- DescriptorHeaps
	//7- BackBuffer RTV
	//8- Depth/Stencil Buffer+View
	//9- ViewportScissor


	//WindowsClassTemporaryAddedHere
public:
	bool InitWinApp(HINSTANCE hInstance, int show);
	int Run();

protected:
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
	static HWND m_mainWindowHWND;

};

