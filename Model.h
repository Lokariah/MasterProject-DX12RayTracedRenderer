#pragma once

//This is temporary model loading that needs heavy reworking!!!!!.

#include "Utility.h"
#include "FrameResource.h"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace Dx12MasterProject {

	struct Mesh {
	public:
		std::string name;
		DirectX::XMFLOAT4X4 world = IDENTITY_MATRIX;
		UINT objCBIndex = -1;
		D3D12_PRIMITIVE_TOPOLOGY primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		std::vector<vertexConsts> vertices;
		std::vector<uint16_t> indices;
		std::vector<DirectX::XMFLOAT4X4> nodePos; //Stores all node positions leading to mesh to calculate mesh position.
		int numFramesDirty = gNumFrameResources;

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

		UINT indexCount = 0;
		UINT startIndexLocation = 0;
		int baseVertexLocation = 0;

		Mesh(std::vector<vertexConsts> Vertices, std::vector<uint16_t> Indices, std::string Name) {
			vertices = Vertices;
			indices = Indices;
			name = Name;
		}

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

	class Model
	{
	public:
		std::string name;
		DirectX::XMFLOAT4X4 world = IDENTITY_MATRIX;
		std::vector<Mesh> meshes;
		void BuildModel(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
		void CalculateMeshesPositions(); //Cycles through and figures out each nodes positions relative to world space to render.
		Model(std::string path, DirectX::XMFLOAT3 pos);
		void LoadModel(std::string path, DirectX::XMFLOAT3 pos);
	private:
		void ProcessNode(aiNode* node, const aiScene* scene, std::vector<DirectX::XMFLOAT4X4> worldMatrices);
		Mesh ProcessMesh(aiMesh* mesh, const aiScene* scene, const aiNode* node);
		std::string directory;
	};
}

// Flags can be done through or bitwise operating to check what flags are active. If each bit is assigned to a flag you can check the values to see what is required.
// |+ will return a new number where all flags that were on from two numbers remain one. Ox specifies to use the hexadecimal value so Ox10 is actually 16 making it easier to
// read for large flag systems.

/*
	Grand Plan:
	-Read file into scene
	-Decifer requirements
	-Convert mesh into submeshes
	-Reread and extract info from scene for each submesh
	-Convert info into dx12 objects (buffers, textures)
	-Copy node hierarchy
	-Render function
*/
