#pragma once
#include "Utility.h"

//#include "Win32Wnd.h"
#include "UploadBuffer.h"
#include "Timer.h"

class Dx12Renderer
{
public:
	Dx12Renderer(HINSTANCE hInstance);
	Dx12Renderer(const Dx12Renderer& temp) = delete;
	Dx12Renderer operator= (const Dx12Renderer& temp) = delete;
	~Dx12Renderer();

	//static Dx12Renderer* GetApp();
	HINSTANCE AppInst() const;
	HWND MainWnd() const;

	bool Get4xMsaaState() const;
	void Set4xMsaaState(bool value);

	bool Initialise(HINSTANCE hInstance, int nShowCmd);
	void DeviceRemovedReason();

protected:

	struct vertexLayout {
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT4 colour;
	};

	struct constants {
		DirectX::XMFLOAT4X4 worldViewProj = IDENTITY_MATRIX;
	};

	bool InitialiseDirect3D();
	void OnResize();
	void Update(const Timer gameTimer);
	void Draw(const Timer gameTimer);

	void CreateCommandObjects();
	void CreateSwapChain();
	void CreateRTVAndDSVDescriptorHeaps();
	void FlushCommandQueue();

	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildBoxGeometry();
	void BuildPSO();

	float GetAspectRatio();
	ID3D12Resource* CurrentBackBuffer() const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

	ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer);

	void CalculateFrameStats();

	// Dx12Renderer* mApp;
	HINSTANCE mAppInst = nullptr;

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

	ComPtr<IDXGIFactory4>				mDXGIFactory = nullptr;
	ComPtr<ID3D12Device>				mD3DDevice = nullptr;
	ComPtr<IDXGISwapChain>				mSwapChain = nullptr;
	ComPtr<ID3D12Resource>				mSwapChainBuffer[SWAP_CHAIN_BUFFER_COUNT];
	ComPtr<ID3D12Resource>				mDepthStencilBuffer = nullptr;
	ComPtr<ID3D12Fence>					mFence = nullptr;
	ComPtr<ID3D12CommandQueue>			mCommandQueue = nullptr;
	ComPtr<ID3D12CommandAllocator>		mDirectCmdListAlloc = nullptr;
	ComPtr<ID3D12GraphicsCommandList>   mCommandList = nullptr;
	ComPtr<ID3D12RootSignature>			mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap>		mRTVHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap>		mDSVHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap>		mCBVHeap = nullptr;

	std::unique_ptr<UploadBuffer<constants>> mObjectCB;
	std::unique_ptr<MeshGeometry> mBoxGeometry;
	ComPtr<ID3DBlob> mvsByteCode;
	ComPtr<ID3DBlob> mpsByteCode;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	ComPtr<ID3D12PipelineState> mPSO;

	DirectX::XMFLOAT4X4 mWorld = IDENTITY_MATRIX;
	DirectX::XMFLOAT4X4 mView  = IDENTITY_MATRIX;
	DirectX::XMFLOAT4X4 mProj  = IDENTITY_MATRIX;

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = DirectX::XM_PIDIV4;
	float mRadius = 5.0f;

	POINT mLastMousePos;

	D3D12_VIEWPORT vp;
	D3D12_RECT mScissorRect;

	int mClientWidth = 1980;
	int mClientHeight = 1080;

	Timer mGameTimer;

	//WindowsClassTemporaryAddedHere
public:
	bool InitWinApp(HINSTANCE hInstance, int show);
	int Run();

protected:
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
	static HWND m_mainWindowHWND;
	std::wstring mMainWndCaption = L"Masters Project - DX12 Renderer ";
};

