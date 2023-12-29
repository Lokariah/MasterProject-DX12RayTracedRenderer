#pragma once
#include "Utility.h"
#include "Model.h"
//#include "Win32Wnd.h"
#include "UploadBuffer.h"
#include "FrameResource.h"
#include "Timer.h"
#include "Camera.h"

namespace Dx12MasterProject {
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

	struct RootSigDesc {
		D3D12_ROOT_SIGNATURE_DESC desc = {};
		std::vector<D3D12_DESCRIPTOR_RANGE> range;
		std::vector<D3D12_ROOT_PARAMETER> rootParams;
	};

	struct DxilLibrary {
		DxilLibrary(ID3DBlob* blob, const WCHAR* entPoint[], uint32_t entPointCount) : shaderBlob(blob) {
			stateSubobj.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
			stateSubobj.pDesc = &dxilLibDesc;
			dxilLibDesc = {};
			exportDesc.resize(entPointCount);
			exportName.resize(entPointCount);
			if (blob) {
				dxilLibDesc.DXILLibrary.pShaderBytecode = blob->GetBufferPointer();
				dxilLibDesc.DXILLibrary.BytecodeLength = blob->GetBufferSize();
				dxilLibDesc.NumExports = entPointCount;
				dxilLibDesc.pExports = exportDesc.data();

				for (uint32_t i = 0; i < entPointCount; i++) {
					exportName[i] = entPoint[i];
					exportDesc[i].Name = exportName[i].c_str();
					exportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
					exportDesc[i].ExportToRename = nullptr;
				}
			}
		};

		DxilLibrary() : DxilLibrary(nullptr, nullptr, 0) {};

		D3D12_DXIL_LIBRARY_DESC dxilLibDesc;
		D3D12_STATE_SUBOBJECT stateSubobj{};
		ID3DBlob* shaderBlob;
		std::vector<D3D12_EXPORT_DESC> exportDesc;
		std::vector<std::wstring> exportName;
	};

	struct HitProgram {
		HitProgram(LPCWSTR ahsExport, LPCWSTR chsExport, const std::wstring& name) : exportName(name) {
			desc = {};
			desc.AnyHitShaderImport = ahsExport;
			desc.ClosestHitShaderImport = chsExport;
			desc.HitGroupExport = exportName.c_str();

			subObj.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
			subObj.pDesc = &desc;
		}
		std::wstring exportName;
		D3D12_HIT_GROUP_DESC desc;
		D3D12_STATE_SUBOBJECT subObj;
	};

	struct ExportAssociation {
		ExportAssociation(const WCHAR* exportNames[], uint32_t exportCount, const D3D12_STATE_SUBOBJECT* subobjectToAssociate) {
			assoc.NumExports = exportCount;
			assoc.pExports = exportNames;
			assoc.pSubobjectToAssociate = subobjectToAssociate;

			subObj.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
			subObj.pDesc = &assoc;
		}
		D3D12_STATE_SUBOBJECT subObj = {};
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc = {};
	};

	struct ShaderConfig {
		ShaderConfig(uint32_t maxAttributeSizeInBytes, uint32_t maxPayloadSizeInBytes) {
			shaderConfig.MaxAttributeSizeInBytes = maxAttributeSizeInBytes;
			shaderConfig.MaxPayloadSizeInBytes = maxPayloadSizeInBytes;
			subObj.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
			subObj.pDesc = &shaderConfig;
		}
		D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
		D3D12_STATE_SUBOBJECT subObj = {};
	};

	struct PipelineConfig {
		PipelineConfig(uint32_t maxTraceRecursionDepth) {
			pipelineConfig.MaxTraceRecursionDepth = maxTraceRecursionDepth;
			subObj.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
			subObj.pDesc = &pipelineConfig;
		}
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
		D3D12_STATE_SUBOBJECT subObj = {};
	};

	struct RTVertexBufferLayout {
		DirectX::XMFLOAT3 vertexPos;
		DirectX::XMFLOAT3 vertexNorm;
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
		void ToggleRaytracingState();

		bool Initialise(HINSTANCE hInstance, int nShowCmd);
		void DeviceRemovedReason();
		static ID3D12RootSignature* CreateRootSignature(ID3D12Device5* device, const D3D12_ROOT_SIGNATURE_DESC& desc);

	protected:

		//--------------------
		//SharedFunctions
		//--------------------

		void SetRaytracingState(bool state);

		//--------------------
		//RayTraceFunctions
		//--------------------

		void CheckRaytracingSupport();
		void CreateAccelerationStructures();
		void CreateRtPipelineState();
		void CreateShaderTable();
		void CreateConstantBufferRT();
		void CreateShaderResources();
		void BuildFrameResourcesRT();

		ComPtr<ID3D12Resource> mVertexBuffer[3];
		ComPtr<ID3D12Resource> mIndexBuffer[3];
		AccelerationStructBuffers mTopLvlBuffers;
		ComPtr<ID3D12Resource> mBotLvlAS[2];
		std::uint64_t mTlasSize = 0;

		ComPtr<ID3D12StateObject> mPipelineState;
		ComPtr<ID3D12RootSignature> mEmptyRootSig;

		ComPtr<ID3D12Resource> mShaderTable;
		uint32_t mShaderTableEntrySize = 0;

		ComPtr<ID3D12Resource> mOutputResource;
		ComPtr<ID3D12DescriptorHeap> mSrvUavHeap;
		static const uint32_t SRV_UAV_HEAP_SIZE = 2;

		ComPtr<ID3D12Resource> mConstantBufferRT[3];

		std::vector<std::unique_ptr<FrameResourceRT>> mFrameResourcesRT;
		FrameResourceRT* mCurrFrameResourceRT = nullptr;

		ID3D12Resource* CreateBuffer(ID3D12Device5* device, std::uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps);
		void CreateTriangleVB(ID3D12Device5* device, ID3D12Resource* vertexBuff[], ID3D12Resource* indexBuff[], int index);
		void CreateCubeVB(ID3D12Device5* device, ID3D12Resource* vertexBuff[], ID3D12Resource* indexBuff[], int index, float width, float height, float length);
		void CreatePlaneVB(ID3D12Device5* device, ID3D12Resource* vertexBuff[], ID3D12Resource* indexBuff[], int index, float width, float length, float heightOffset);
		AccelerationStructBuffers CreateBottomLevelAS(ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList, ID3D12Resource* vertBuff[], const uint32_t vertexCount[], ID3D12Resource* indexBuff[], const uint32_t indexCount[], uint32_t geomCount);
		void BuildTopLevelAS(ID3D12Device5* device, ID3D12GraphicsCommandList4* cmdList, ID3D12Resource* botLvlAS[], std::uint64_t& tlasSize, float rotation, bool bUpdate, AccelerationStructBuffers& buffers);

		ID3DBlob* CompileLibrary(const WCHAR* filename, const WCHAR* targetString);
		RootSigDesc CreateRayGenRootDesc();
		RootSigDesc CreateTriHitRootDesc();
		RootSigDesc CreatePlaneHitRootDesc();
		DxilLibrary CreateDxilLibrary();

		ID3D12DescriptorHeap* CreateDescHeap(ID3D12Device5* device, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool bShaderVisible);

		float mRotation = 0;

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
		void LoadModel(std::string filePathName);
		//void BuildModelGeometry();
		void BuildPSO();
		void BuildFrameResources();
		void BuildRenderItems();
		void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& rendItems);


		float GetAspectRatio();
		ID3D12Resource* CurrentBackBuffer() const;
		D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
		D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

		ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer);
		void CalculateFrameStats();

		static Dx12Renderer* mApp;
		HINSTANCE mAppInst = nullptr;

		UINT mRTVDescriptorSize = 0;
		UINT mDSVDescriptorSize = 0;
		UINT mCBVSRVUAVDescriptorSize = 0;

		UINT m4xMSAACount = 4;
		UINT m4xMSAAQuality = 0;
		DXGI_FORMAT MSAADepthFormat = DXGI_FORMAT_D32_FLOAT;
		bool m4xMSAAState = false;

		bool mVSync = false;
		bool mRaytracing = false;

		const int mRTInstanceCount = 3;

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
		ComPtr<ID3D12GraphicsCommandList4>  mCommandList = nullptr;
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

		bool lightColourIncrease = false;

		//std::unique_ptr<MeshGeometry> mMeshGeometry;
		//std::unique_ptr<Model> tempModel;
		ComPtr<ID3DBlob> mvsByteCode;
		ComPtr<ID3DBlob> mpsByteCode;

		std::vector<std::unique_ptr<FrameResource>> mFrameResources;
		FrameResource* mCurrFrameResource = nullptr;
		int mCurrFrameResourceIndex = 0;

		Camera mainCamera;
		float cameraMoveSpeed = 50.0f;
		float cameraRotateSpeed = 2.0f;

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
		static HWND GetWindowHWND() {
			return m_mainWindowHWND;
		}
		//protected:

	private:
		static HWND m_mainWindowHWND;
		std::wstring mMainWndCaption = L"Masters Project - DX12 Renderer ";
	};

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);


	struct LocalRootSig {
		LocalRootSig(ID3D12Device5* device, const D3D12_ROOT_SIGNATURE_DESC& desc) {
			rootSig = Dx12Renderer::CreateRootSignature(device, desc);
			subObj.pDesc = &rootSig;
			subObj.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
		}
		ID3D12RootSignature* rootSig;
		D3D12_STATE_SUBOBJECT subObj = {};
	};

	struct GlobalRootSig {
		GlobalRootSig(ID3D12Device5* device, const D3D12_ROOT_SIGNATURE_DESC& desc) {
			rootSig = Dx12Renderer::CreateRootSignature(device, desc);
			subObj.pDesc = &rootSig;
			subObj.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
		}
		ID3D12RootSignature* rootSig;
		D3D12_STATE_SUBOBJECT subObj = {};
	};

}