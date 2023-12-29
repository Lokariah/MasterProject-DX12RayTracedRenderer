#include "Model.h"
#include <chrono>
using namespace Dx12MasterProject;

Model::Model(std::string path)
{
	LoadModel(path);
}

void Model::LoadModel(std::string path)
{
	// Read file via ASSIMP
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path,
		aiProcess_ConvertToLeftHanded | aiProcess_Triangulate | aiProcess_GenUVCoords | aiProcess_FlipUVs | aiProcess_FixInfacingNormals | aiProcess_FindInvalidData | aiProcess_JoinIdenticalVertices | aiProcess_ValidateDataStructure | aiProcess_OptimizeMeshes);

	if (scene->mRootNode == nullptr) {
		std::cout << "Assimp Error: " << importer.GetErrorString() << std::endl;
		return;
	}

	directory = path.substr(0, path.find_last_of('/'));
	ProcessNode(scene->mRootNode, scene);
}

void Model::ProcessNode(aiNode* node, const aiScene* scene)
{
	for (uint32_t i = 0; i < node->mNumMeshes; i++) {
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		meshes.push_back(ProcessMesh(mesh, scene));
		
	}

	for (uint32_t i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(node->mChildren[i], scene);
	}
}

Mesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene)
{
	std::vector<vertexConsts> vertices;
	std::vector<uint16_t> indices;
	std::string name;

	for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
		vertexConsts vertex;
		DirectX::XMFLOAT3 tempVector;

		name = mesh->mName.C_Str();
		if (name == "") name = scene->mName.C_Str() + std::to_string(i);

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
