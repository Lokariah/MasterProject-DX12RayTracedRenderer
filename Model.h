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
		std::vector<vertexConsts> vertices;
		std::vector<uint16_t> indices;
		std::string name;
		Mesh(std::vector<vertexConsts> Vertices, std::vector<uint16_t> Indices, std::string Name) {
			vertices = Vertices;
			indices = Indices;
			name = Name;
		}
	};

	class Model
	{
	public:
		std::vector<Mesh> meshes;
		Model(std::string path);
		void LoadModel(std::string path);
	private:
		void ProcessNode(aiNode* node, const aiScene* scene);
		Mesh ProcessMesh(aiMesh* mesh, const aiScene* scene);
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
