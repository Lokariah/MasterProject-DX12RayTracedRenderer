#pragma once
#include "Utility.h"
//#include "Win32Wnd.h"
#include "UploadBuffer.h"
#include "FrameResource.h"
#include "Timer.h"

const int gNumFrameResources = 3;

struct RenderItem {
	RenderItem() = default;

	DirectX::XMFLOAT4X4 world = IDENTITY_MATRIX;
	int numFramesDirty = gNumFrameResources;
	UINT objCBIndex = -1;
	MeshGeometry* meshGeo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	
	UINT indexCount = 0;
	UINT startIndexLocation = 0;
	int baseVertexLocation = 0;
};

struct AccelerationStructBuffers {
	ComPtr<ID3D12Resource> pScratch;
	ComPtr<ID3D12Resource> pResult;
	ComPtr<ID3D12Resource> pInstanceDesc;
};

struct Instance {
	Instance(ID3D12Resource* BotLvlAS, const DirectX::XMMATRIX& Trans, UINT InsID, UINT HitGroupIndex) : botLvlAS(BotLvlAS), transformMatrix(Trans), instanceID(InsID), hitGroupIndex(HitGroupIndex) {}
	
	ID3D12Resource* botLvlAS;
	const DirectX::XMMATRIX& transformMatrix;
	UINT instanceID;
	UINT hitGroupIndex;
};

struct Library {
	Library(IDxcBlob* dxil, const std::vector<std::wstring>& exSymbols);
	Library(const Library& src) : Library(src.mDxil, src.mExSymbols) {};

	IDxcBlob* mDxil;
	const std::vector<std::wstring> mExSymbols;
	std::vector<D3D12_EXPORT_DESC> mExports;
	D3D12_DXIL_LIBRARY_DESC mLibDesc;
};

struct HitGroup {
	HitGroup(std::wstring hitGroupName, std::wstring closestHitSymbol, std::wstring anyHitSymbol = L"", std::wstring intersectionSymbol = L"");
	HitGroup(const HitGroup& src) : HitGroup(src.mHitGroupName, src.mClosestHitSymbol, src.mAnyHitSymbol, src.mIntersectionSymbol) {}

	std::wstring mHitGroupName;
	std::wstring mClosestHitSymbol;
	std::wstring mAnyHitSymbol;
	std::wstring mIntersectionSymbol;
	D3D12_HIT_GROUP_DESC mDesc;
};

struct RootSigAssociation {
	RootSigAssociation(ID3D12RootSignature* rootSignature, const std::vector<std::wstring>& symbols);
	RootSigAssociation(const RootSigAssociation& src) : RootSigAssociation(src.mRootSigPtr, src.mSymbols) {}

	ID3D12RootSignature* mRootSig;
	ID3D12RootSignature* mRootSigPtr;
	std::vector<std::wstring> mSymbols;
	std::vector<LPCWSTR> mSymbolsPtr;
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION mAssociation;
};

class Dx12Renderer
{
public:
	Dx12Renderer(HINSTANCE hInstance);
	Dx12Renderer(const Dx12Renderer& temp) = delete;
	Dx12Renderer operator= (const Dx12Renderer& temp) = delete;
	~Dx12Renderer();

	static Dx12Renderer* GetApp();
	HINSTANCE AppInst() const;
	HWND MainWnd() const;

	bool Get4xMsaaState() const;
	void Set4xMsaaState(bool value);

	bool Initialise(HINSTANCE hInstance, int nShowCmd);
	void DeviceRemovedReason();

protected:

	//--------------------
	//SharedFunctions
	//--------------------

	void SetRaytracingState(bool state);

	//--------------------
	//RayTraceFunctions
	//--------------------

	void CheckRaytracingSupport();

	AccelerationStructBuffers CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vertexBuffers);
	void CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances);
	void CreateAccelerationStructures();

	//Bottom Level Acceleration Structure relevant
	void BotAddVertexBuffer(ID3D12Resource* vertBuffer, UINT64 vertOffsetInBytes, uint32_t vertCount, UINT vertSizeInBytes, ID3D12Resource* transBuffer, UINT64 transOffsetInBytes, bool bOpaque = true);
	void BotAddVertexBuffer(ID3D12Resource* vertBuffer, UINT64 vertOffsetInBytes, uint32_t vertCount, UINT vertSizeInBytes, ID3D12Resource* indexBuffer, UINT64 indexOffsetInBytes, uint32_t indexCount, ID3D12Resource* transBuffer, UINT64 transOffsetInBytes, bool bOpaque = true);
	void ComputeBotASBufferSize(ID3D12Device5* device, bool bUpdatable, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes);
	void GenerateBotASBuffers(ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer, ID3D12Resource* resultBuffer, bool bUpdateOnly = false, ID3D12Resource* prevResult = nullptr);

	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>	mBotVertexBuffers = {};
	UINT64 mBotScratchSizeInBytes = 0;
	UINT64 mBotResultSizeInBytes = 0;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS mBotFlags;


	//Top Level Acceleration Structure Relevant
	void TopAddInstance(ID3D12Resource* BotLvlAS, const DirectX::XMMATRIX& Trans, UINT InstID, UINT HitGroupIndex);
	void ComputeTopASBufferSizes(ID3D12Device5* device, bool bUpdatable, UINT64* scratchSizeInBytes, UINT64* resultSizeInBytes, UINT64* descSizeInBytes);
	void GenerateTopASBuffers(ID3D12GraphicsCommandList4* commandList, ID3D12Resource* scratchBuffer, ID3D12Resource* resultBuffer, ID3D12Resource* descBuffer, bool bUpdateOnly = false, ID3D12Resource* prevResult = nullptr);

	std::vector<Instance> mTopInstances;
	UINT64 mTopScratchSizeInBytes = 0;
	UINT64 mTopInstDescsSizeInBytes = 0;
	UINT64 mTopResultSizeInBytes = 0;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS mTopFlags;

	ComPtr<ID3D12Resource> mBottomLevelAS;
	AccelerationStructBuffers mTopLevelASBuffers;
	std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> mInstances;

	//ImplementationFunctionAndProperties
	ComPtr<ID3D12RootSignature> CreateRayGenSignature();
	ComPtr<ID3D12RootSignature> CreateMissSignature();
	ComPtr<ID3D12RootSignature> CreateHitSignature();
	void CreateRaytracingPipeline();

	ComPtr<IDxcBlob> mRayGenLibrary;
	ComPtr<IDxcBlob> mHitLibrary;
	ComPtr<IDxcBlob> mMissLibrary;
	ComPtr<ID3D12RootSignature> mRayGenSignature;
	ComPtr<ID3D12RootSignature> mMissSignature;
	ComPtr<ID3D12RootSignature> mHitSignature;
	ComPtr<ID3D12StateObject> mRTStateObj;
	ComPtr<ID3D12StateObjectProperties> mRTStateObjProps;

	//RootSignatureGeneration Relevant
	void RSAddHeapRangesParam(const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges);
	void RSAddHeapRangesParam(std::vector<std::tuple<UINT, UINT, UINT, D3D12_DESCRIPTOR_RANGE_TYPE, UINT >> ranges);
	void RSAddRootParam(D3D12_ROOT_PARAMETER_TYPE type, UINT shaderReg = 0, UINT regSpace = 0, UINT numRootConsts = 1);
	ID3D12RootSignature* RSGenerate(ID3D12Device* device, bool bLocal);

	void RSReset();

	std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> mRSRanges;
	std::vector<D3D12_ROOT_PARAMETER> mRSParams;
	std::vector<UINT> mRSRangeLocs;

	enum {
		RSC_BASE_SHADER_REG = 0,
		RSC_NUM_DESCS = 1,
		RSC_REG_SPACE = 2,
		RSC_RANGE_TYPE = 3,
		RSC_OFFSET_DESC_FROM_TABLE_START = 4
	};

	//RayTracingPipelineGeneration Relevant
	void AddLibrary(IDxcBlob* dxilLibrary, const std::vector<std::wstring>& symbolEx);
	void AddHitGroup(const std::wstring& hitGroupName, const std::wstring& closestHitSymbol, const std::wstring& anyHitSymbol = L"", const std::wstring& intersectionSymbol = L"");
	void AddRootSignatureAssociation(ID3D12RootSignature* rootSig, const std::vector<std::wstring>& symbols);
	void SetMaxPayloadSize(UINT sizeInBytes);
	void SetMaxAttributeSize(UINT sizeInBytes);
	void SetMaxRecursionDepth(UINT maxDepth);
	ID3D12StateObject* RTPipelineGenerate();

	void CreateDummyRootSigs();
	void BuildShaderExList(std::vector<std::wstring>& exSymbols);

	std::vector<Library> mRTPLibraries;
	std::vector<HitGroup> mRTPHitGroups;
	std::vector<RootSigAssociation> mRTPRootSigAssociations;
	UINT mRTPMaxPayLoadSizeInBytes = 0;
	UINT mRTPMaxAttributeSizeInBytes = 2 * sizeof(float);
	UINT mRTPMaxRecursionDepth = 1;
	ID3D12RootSignature* mRTPDummyLocalRootSig;
	ID3D12RootSignature* mRTPDummyGlobalRootSig;


	//--------------------
	//RasterizerFunctions
	//--------------------

	bool InitialiseDirect3D();
	void OnResize();
	void Update(const Timer gameTimer);
	void Draw(const Timer gameTimer);

	void UpdateCamera(const Timer gameTimer);
	void UpdateObjectsCB(const Timer gameTimer);
	void UpdateMainPassCB(const Timer gameTimer);

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
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& rendItems);


	float GetAspectRatio();
	ID3D12Resource* CurrentBackBuffer() const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

	ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer);
	ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps);
	ComPtr<IDxcBlob> CompileShaderLibrary(LPCWSTR filename);
	void CalculateFrameStats();

	static Dx12Renderer* mApp;
	HINSTANCE mAppInst = nullptr;

	UINT mRTVDescriptorSize = 0;
	UINT mDSVDescriptorSize = 0;
	UINT mCBVSRVUAVDescriptorSize = 0;

	UINT m4xMSAAQuality = 0;
	bool m4xMSAAState = false;

	bool mVSync = false;
	bool mRaytracing = true;

	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	static const UINT SWAP_CHAIN_BUFFER_COUNT = 2;
	int mCurrBackBuffer = 0;
	UINT64 mCurrFence = 0;

	ComPtr<IDXGIFactory4>				mDXGIFactory = nullptr;
	ComPtr<ID3D12Device5>				mD3DDevice = nullptr;
	ComPtr<IDXGISwapChain>				mSwapChain = nullptr;
	ComPtr<ID3D12Resource>				mSwapChainBuffer[SWAP_CHAIN_BUFFER_COUNT];
	ComPtr<ID3D12Resource>				mDepthStencilBuffer = nullptr;
	ComPtr<ID3D12Fence>					mFence = nullptr;
	ComPtr<ID3D12CommandQueue>			mCommandQueue = nullptr;
	ComPtr<ID3D12CommandAllocator>		mDirectCmdListAlloc = nullptr;
	ComPtr<ID3D12GraphicsCommandList4>   mCommandList = nullptr;
	ComPtr<ID3D12RootSignature>			mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap>		mRTVHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap>		mDSVHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap>		mCBVHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap>		mSRVDescHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeos;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPsos;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	std::vector<std::unique_ptr<RenderItem>> mAllRendItems;
	std::vector<RenderItem*> mOpaqueRendItems;

	PassConsts mMainPassCB;
	UINT mPassCBVOffset = 0;
	bool bIsWireframe = false;

	//std::unique_ptr<UploadBuffer<constants>> mObjectCB;
	std::unique_ptr<MeshGeometry> mBoxGeometry;
	ComPtr<ID3DBlob> mvsByteCode;
	ComPtr<ID3DBlob> mpsByteCode;

	//ComPtr<ID3D12PipelineState> mPSO;

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	DirectX::XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
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

