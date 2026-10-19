// Aurora3D — model.h
// Model loading. A Model is one or more meshes plus the materials they reference.
//
// The built-in loader reads Wavefront OBJ (with the matching MTL file) and needs
// nothing but the engine itself. When Aurora3D is built with AURORA_HAS_ASSIMP
// and Assimp is on the include path, LoadModel() additionally accepts every
// format Assimp supports (FBX, glTF, DAE, ...); the OBJ path stays available
// either way.
#pragma once

#include "aurora/color.h"
#include "aurora/math.h"
#include "aurora/mesh.h"
#include "aurora/texture.h"

namespace a3d {

// A material as the loader found it. Textures are loaded when the material names
// a map; `baseColor` is the diffuse colour.
struct Material {
    Color baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    Texture* albedo = nullptr;
    float metallic = 0.0f;
    float roughness = 0.8f;
    char name[64] = {};
};

// One drawable piece of a model: geometry plus the material it uses.
struct ModelPart {
    Mesh* mesh = nullptr;
    int materialIndex = -1;   // index into Model::materials, or -1 for none
    Mat4 transform;           // local transform from the source file
    char name[64] = {};
};

// A loaded model. `parts` are drawn in order by DrawModel().
struct Model {
    ModelPart* parts = nullptr;
    int partCount = 0;
    Material* materials = nullptr;
    int materialCount = 0;
    // Overall bounding box across every part, in model space.
    Bounds bounds;
    bool loaded = false;
};

// Loads a model from disk. The extension decides the backend: .obj uses the
// built-in loader; anything else needs Assimp. Returns nullptr on failure.
Model* LoadModel(const char* path);
void UnloadModel(Model* model);

// Draws every part at `position` with `rotationDegrees` and `scale`.
void DrawModel(const Model* model, Vec3 position,
               Vec3 rotationDegrees = Vec3{},
               Vec3 scale = Vec3{1, 1, 1},
               Color tint = colors::White);
void DrawModelEx(const Model* model, const Mat4& transform, Color tint = colors::White);

// Bounding box of a model placed at `position` with `rotationDegrees`/`scale`.
Bounds ModelWorldBounds(const Model* model, Vec3 position, Vec3 rotationDegrees, Vec3 scale);

} // namespace a3d
