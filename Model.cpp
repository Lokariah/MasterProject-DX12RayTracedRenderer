#include "Model.h"

#include <chrono>
using namespace Dx12MasterProject;

void Dx12MasterProject::Model::CalculateMeshesPositions()
{
	for (auto& i : meshes) {
		i.world = i.nodePos.at(0);								 //URGENT!!!! Need to swap this to world to ensure model can move, rotate, and scale properly during runtime.
		for (int j = 1; j < i.nodePos.size(); j++) {
			i.world = Utility::XMFLOAT4X4Multiply(Utility::XMFLOAT4X4Transpose(i.nodePos.at(j)), i.world);

		}
		i.numFramesDirty++;
	}
}

Model::Model(std::string path, DirectX::XMFLOAT3 pos)
{
	LoadModel(path, pos);
}

void Model::LoadModel(std::string path, DirectX::XMFLOAT3 pos)
{
	// Read file via ASSIMP
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path,
		aiProcess_ConvertToLeftHanded | aiProcess_Triangulate | aiProcess_GenUVCoords | aiProcess_FlipUVs | aiProcess_FixInfacingNormals | aiProcess_FindInvalidData | aiProcess_JoinIdenticalVertices | aiProcess_ValidateDataStructure | aiProcess_OptimizeMeshes);

	if (scene->mRootNode == nullptr) {
		std::cout << "Assimp Error: " << importer.GetErrorString() << std::endl;
		return;
	}

	name = path.substr(path.find_last_of('/') + 1, path.find_last_of('.') - path.find_last_of('/') - 1);
	world = {	1,		0,		0,		0,
				0,		1,		0,		0,
				0,		0,		1,		0,
				pos.x, pos.y,	pos.z,	1 };

	directory = path.substr(0, path.find_last_of('/'));
	std::vector<DirectX::XMFLOAT4X4> worldMatrices;
	DirectX::XMFLOAT4X4 rootPos = world;
	worldMatrices.push_back(rootPos);
	ProcessNode(scene->mRootNode, scene, worldMatrices);

	//for (auto &i : meshes) {
	//	const UINT vbByteSize = (UINT)i.vertices.size() * sizeof(vertexConsts);
	//	const UINT ibByteSize = (UINT)i.indices.size() * sizeof(std::uint16_t);

	//	ThrowIfFailed(D3DCreateBlob(vbByteSize, &i.vertexBufferCPU));
	//	CopyMemory(i.vertexBufferCPU->GetBufferPointer(), i.vertices.data(), vbByteSize);

	//	ThrowIfFailed(D3DCreateBlob(ibByteSize, &i.indexBufferCPU));
	//	CopyMemory(i.indexBufferCPU->GetBufferPointer(), i.indices.data(), ibByteSize);

	//	i.vertexBufferGPU = Utility::CreateDefaultBuffer(device, cmdList, i.vertices.data(), vbByteSize, i.vertexBufferUploader);
	//	i.indexBufferGPU  = Utility::CreateDefaultBuffer(device, cmdList, i.indices.data(),  ibByteSize, i.indexBufferUploader);
	//	
	//	i.vertexByteStride = sizeof(vertexConsts);
	//	i.vertexBufferByteSize = vbByteSize;
	//	i.indexFormat = DXGI_FORMAT_R16_UINT;
	//	i.indexBufferByteSize = ibByteSize;

	//	i.indexCount = (UINT)i.indices.size();
	//	i.startIndexLocation = 0;
	//	i.baseVertexLocation = 0;
	//	i.objCBIndex = 0; //This is very much bad.
	//}
}

void Model::ProcessNode(aiNode* node, const aiScene* scene, std::vector<DirectX::XMFLOAT4X4> worldMatrices)
{
	if(node != scene->mRootNode && !node->mTransformation.IsIdentity()) worldMatrices.push_back(DirectX::XMFLOAT4X4(&node->mTransformation.a1));
	
	for (uint32_t i = 0; i < node->mNumMeshes; i++) {
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		meshes.push_back(ProcessMesh(mesh, scene, node));
		meshes.at(meshes.size() - 1).nodePos = worldMatrices;
	}

	for (uint32_t i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(node->mChildren[i], scene, worldMatrices);
	}
}

Mesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene, const aiNode* node)
{
	std::vector<vertexConsts> vertices;
	std::vector<uint16_t> indices;
	std::string name;

	world = { node->mTransformation.a1, node->mTransformation.a2, node->mTransformation.a3, node->mTransformation.a4,
			  node->mTransformation.b1, node->mTransformation.b2, node->mTransformation.b3, node->mTransformation.b4,
			  node->mTransformation.c1, node->mTransformation.c2, node->mTransformation.c3, node->mTransformation.c4,
			  node->mTransformation.d1, node->mTransformation.d2, node->mTransformation.d3, node->mTransformation.d4 };

	name = mesh->mName.C_Str();
	if (name == "") {
		if (node->mName.length != 0) name = node->mName.C_Str() + std::to_string(mesh->mNumVertices);
		else name = scene->mName.C_Str() + std::to_string(mesh->mNumVertices);
	}
	for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
		vertexConsts vertex;
		DirectX::XMFLOAT3 tempVector;

		// Positions
		tempVector.x = mesh->mVertices[i].x;
		tempVector.y = mesh->mVertices[i].y;
		tempVector.z = mesh->mVertices[i].z;
		vertex.pos = tempVector;

		//Normals
		tempVector.x = mesh->mNormals[i].x;
		tempVector.y = mesh->mNormals[i].y;
		tempVector.z = mesh->mNormals[i].z;
		vertex.norm = tempVector;

		//Texture coords
		if (mesh->HasTextureCoords(0)) {
			vertex.uv = {
				mesh->mTextureCoords[0][i].x,
				mesh->mTextureCoords[0][i].y
			};
		}
		

		//Remove this when textures are add !!!!
		if (mesh->HasVertexColors(0)) {
			vertex.colour = DirectX::XMFLOAT4{
				mesh->mColors[0][i].r,
				mesh->mColors[0][i].g,
				mesh->mColors[0][i].b,
				mesh->mColors[0][i].a
			};
		}
		else {
			auto start = std::chrono::system_clock::now();
			auto end = std::chrono::system_clock::now();
			
			std::chrono::duration<double> elapsed_seconds = end - start;
			//std::srand(elapsed_seconds.count());
			vertex.colour = DirectX::XMFLOAT4{ ((float)std::rand() / RAND_MAX), ((float)std::rand() / RAND_MAX),  ((float)std::rand() / RAND_MAX),  1.0f };
		}

		vertices.push_back(vertex);
	}

	for (uint32_t i = 0; i < mesh->mNumFaces; i++) {
		aiFace face = mesh->mFaces[i];
		for (uint32_t j = 0; j < face.mNumIndices; j++) {
			indices.push_back(face.mIndices[j]);
		}
		
		//Add texture processing here
		
	}

	return Mesh(vertices, indices, name);
}

void Model::BuildModel(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) {

	for (auto& i : meshes) {
		const UINT vbByteSize = (UINT)i.vertices.size() * sizeof(vertexConsts);
		const UINT ibByteSize = (UINT)i.indices.size() * sizeof(std::uint16_t);

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &i.vertexBufferCPU));
		CopyMemory(i.vertexBufferCPU->GetBufferPointer(), i.vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &i.indexBufferCPU));
		CopyMemory(i.indexBufferCPU->GetBufferPointer(), i.indices.data(), ibByteSize);

		i.vertexBufferGPU = Utility::CreateDefaultBuffer(device, cmdList, i.vertices.data(), vbByteSize, i.vertexBufferUploader);
		i.indexBufferGPU = Utility::CreateDefaultBuffer(device, cmdList, i.indices.data(), ibByteSize, i.indexBufferUploader);

		i.vertexByteStride = sizeof(vertexConsts);
		i.vertexBufferByteSize = vbByteSize;
		i.indexFormat = DXGI_FORMAT_R16_UINT;
		i.indexBufferByteSize = ibByteSize;

		i.indexCount = (UINT)i.indices.size();
		i.startIndexLocation = 0;
		i.baseVertexLocation = 0;
		//i.objCBIndex = 0; //This is very much bad.
	}
}