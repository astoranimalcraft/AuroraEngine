// NexiumEngine — model loading.
//
// Two backends sit behind one API:
//
//   * the built-in Wavefront OBJ/MTL loader, always available and dependency
//     free, which handles the common case (v/vn/vt/f, usemtl, map_Kd) including
//     negative indices and polygons of any size;
//   * an optional Assimp backend compiled in with NEXIUM_HAS_ASSIMP, which adds
//     FBX, glTF, DAE and the rest. When it is present it is used for every
//     extension other than .obj, and the OBJ loader keeps handling .obj so
//     behaviour does not change based on the build.
#include "nexium/model.h"

#include "nexium/draw.h"
#include "nexium/log.h"

#include "internal/renderer.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(NEXIUM_HAS_ASSIMP)
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#endif

namespace n3d {

namespace {

std::string DirectoryOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string ExtensionOf(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::string();
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return ext;
}

void CopyName(char* destination, size_t size, const std::string& source) {
    if (size == 0) return;
    std::strncpy(destination, source.c_str(), size - 1);
    destination[size - 1] = '\0';
}

} // namespace

// =============================================================== OBJ/MTL ====
namespace obj {

// A face vertex: three indices into the position/normal/uv arrays, -1 when the
// OBJ file did not supply that attribute for this corner.
struct FaceCorner {
    int position = -1;
    int normal = -1;
    int uv = -1;
};

struct Builder {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // The model being built and the material currently selected by usemtl.
    Model* model = nullptr;
    std::string directory;
    int currentMaterial = -1;

    // Names as they are first seen, so a usemtl can resolve to an index.
    std::vector<std::string> materialNames;

    // One part accumulates until a new material or object starts.
    std::vector<Vertex> partVertices;
    std::vector<uint32_t> partIndices;
    int partMaterial = -1;
    std::string partName;

    void FlushPart() {
        if (partVertices.empty()) return;
        if (model->partCount >= 256) {
            LogWarn("obj: more than 256 parts; the rest are ignored");
            partVertices.clear();
            partIndices.clear();
            return;
        }
        ModelPart part;
        part.mesh = CreateMesh(partVertices.data(), static_cast<uint32_t>(partVertices.size()),
                               partIndices.empty() ? nullptr : partIndices.data(),
                               static_cast<uint32_t>(partIndices.size()));
        part.materialIndex = partMaterial;
        CopyName(part.name, sizeof(part.name), partName);
        model->parts[model->partCount++] = part;
        partVertices.clear();
        partIndices.clear();
    }

    // Resolves (creating on first use) a material slot by name.
    int MaterialIndex(const std::string& name) {
        for (int i = 0; i < model->materialCount; ++i) {
            if (name == model->materials[i].name) return i;
        }
        if (model->materialCount >= 64) return -1;
        const int index = model->materialCount++;
        CopyName(model->materials[index].name, sizeof(model->materials[index].name), name);
        return index;
    }
};

// Splits an OBJ face token like "3/5/2" or "1//7" into its three indices. OBJ
// indices are 1-based, and negative values count back from the current end.
void ParseFaceCorner(const char* token, int positionCount, int normalCount, int uvCount,
                     FaceCorner& out) {
    int values[3] = {0, 0, 0};
    int slot = 0;
    const char* cursor = token;

    while (*cursor && slot < 3) {
        if (*cursor == '/') {
            ++slot;
            ++cursor;
            continue;
        }
        // Read an integer (optionally negative).
        bool negative = false;
        if (*cursor == '-') {
            negative = true;
            ++cursor;
        }
        int value = 0;
        bool any = false;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10 + (*cursor - '0');
            ++cursor;
            any = true;
        }
        if (!any) {
            ++cursor;
            continue;
        }
        values[slot] = negative ? -value : value;
    }

    // Convert 1-based / negative indices to 0-based array indices.
    auto resolve = [](int value, int count) {
        if (value > 0) return value - 1;
        if (value < 0) return count + value;
        return -1;
    };
    out.position = resolve(values[0], positionCount);
    out.uv = resolve(values[1], uvCount);
    out.normal = resolve(values[2], normalCount);
}

void ParseMtl(Builder& builder, const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        LogWarn("obj: cannot open material file '%s'", path.c_str());
        return;
    }

    Material* current = nullptr;
    char line[1024];

    while (std::fgets(line, sizeof(line), file)) {
        // Trim leading whitespace.
        char* cursor = line;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;

        if (std::strncmp(cursor, "newmtl ", 7) == 0) {
            std::string name = cursor + 7;
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
            const int index = builder.MaterialIndex(name);
            current = index >= 0 ? &builder.model->materials[index] : nullptr;
        } else if (!current) {
            continue;
        } else if (std::strncmp(cursor, "Kd ", 3) == 0) {
            float r = 0, g = 0, b = 0;
            if (std::sscanf(cursor + 3, "%f %f %f", &r, &g, &b) == 3) {
                current->baseColor = Color{r, g, b, 1.0f};
            }
        } else if (std::strncmp(cursor, "Ns ", 3) == 0) {
            float shininess = 0.0f;
            if (std::sscanf(cursor + 3, "%f", &shininess) == 1) {
                // OBJ shininess is 0..1000; map it onto a roughness the PBR shader
                // can use (high shininess -> low roughness).
                current->roughness = Clamp(1.0f - shininess / 1000.0f, 0.05f, 1.0f);
            }
        } else if (std::strncmp(cursor, "map_Kd ", 7) == 0) {
            std::string map = cursor + 7;
            while (!map.empty() && (map.back() == '\n' || map.back() == '\r')) map.pop_back();
            // Strip any option flags; the last whitespace-separated token is the
            // file name.
            const size_t space = map.find_last_of(" \t");
            if (space != std::string::npos) map = map.substr(space + 1);
            if (!map.empty()) {
                const std::string full = builder.directory + map;
                current->albedo = LoadTextureFromFile(full.c_str(), true);
            }
        }
    }
    std::fclose(file);
}

bool LoadObj(const char* path, Model* model) {
    FILE* file = std::fopen(path, "rb");
    if (!file) {
        LogError("obj: cannot open '%s'", path);
        return false;
    }

    Builder builder;
    builder.model = model;
    builder.directory = DirectoryOf(path);
    // Parts are collected into a fixed array on the model; 256 is plenty for a
    // single OBJ and keeps the allocation simple.
    model->parts = new ModelPart[256];
    model->partCount = 0;
    model->materials = new Material[64];
    model->materialCount = 0;

    char line[1024];
    while (std::fgets(line, sizeof(line), file)) {
        char* cursor = line;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;

        if (std::strncmp(cursor, "v ", 2) == 0) {
            float x = 0, y = 0, z = 0;
            if (std::sscanf(cursor + 2, "%f %f %f", &x, &y, &z) == 3) {
                builder.positions.push_back(Vec3{x, y, z});
            }
        } else if (std::strncmp(cursor, "vn ", 3) == 0) {
            float x = 0, y = 0, z = 0;
            if (std::sscanf(cursor + 3, "%f %f %f", &x, &y, &z) == 3) {
                builder.normals.push_back(Normalize(Vec3{x, y, z}));
            }
        } else if (std::strncmp(cursor, "vt ", 3) == 0) {
            float u = 0, v = 0;
            if (std::sscanf(cursor + 3, "%f %f", &u, &v) >= 2) {
                builder.uvs.push_back(Vec2{u, v});
            }
        } else if (std::strncmp(cursor, "usemtl ", 7) == 0) {
            builder.FlushPart();
            std::string name = cursor + 7;
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
            builder.currentMaterial = builder.MaterialIndex(name);
            builder.partMaterial = builder.currentMaterial;
        } else if (std::strncmp(cursor, "mtllib ", 7) == 0) {
            std::string mtl = cursor + 7;
            while (!mtl.empty() && (mtl.back() == '\n' || mtl.back() == '\r')) mtl.pop_back();
            ParseMtl(builder, builder.directory + mtl);
        } else if (std::strncmp(cursor, "o ", 2) == 0 || std::strncmp(cursor, "g ", 2) == 0) {
            builder.FlushPart();
            std::string name = cursor + 2;
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
            builder.partName = name;
        } else if (std::strncmp(cursor, "f ", 2) == 0) {
            // Gather the corners of the polygon, then fan it into triangles.
            std::vector<uint32_t> faceIndices;
            const char* token = cursor + 2;
            char buffer[128];

            while (*token) {
                while (*token == ' ' || *token == '\t') ++token;
                if (*token == '\0' || *token == '\n' || *token == '\r') break;

                size_t length = 0;
                while (token[length] && token[length] != ' ' && token[length] != '\t' &&
                       token[length] != '\n' && token[length] != '\r' &&
                       length < sizeof(buffer) - 1) {
                    buffer[length] = token[length];
                    ++length;
                }
                buffer[length] = '\0';
                token += length;

                FaceCorner corner;
                ParseFaceCorner(buffer, static_cast<int>(builder.positions.size()),
                                static_cast<int>(builder.normals.size()),
                                static_cast<int>(builder.uvs.size()), corner);
                if (corner.position < 0 ||
                    corner.position >= static_cast<int>(builder.positions.size())) {
                    continue;   // malformed corner; drop it rather than crash
                }

                Vertex vertex;
                vertex.position = builder.positions[static_cast<size_t>(corner.position)];
                if (corner.normal >= 0 && corner.normal < static_cast<int>(builder.normals.size())) {
                    vertex.normal = builder.normals[static_cast<size_t>(corner.normal)];
                }
                if (corner.uv >= 0 && corner.uv < static_cast<int>(builder.uvs.size())) {
                    // OBJ v runs bottom-up; flip it to match the engine's textures.
                    vertex.uv = Vec2{builder.uvs[static_cast<size_t>(corner.uv)].x,
                                     1.0f - builder.uvs[static_cast<size_t>(corner.uv)].y};
                }
                vertex.color = colors::White;

                const uint32_t index = static_cast<uint32_t>(builder.partVertices.size());
                builder.partVertices.push_back(vertex);
                faceIndices.push_back(index);
            }

            for (size_t i = 1; i + 1 < faceIndices.size(); ++i) {
                builder.partIndices.push_back(faceIndices[0]);
                builder.partIndices.push_back(faceIndices[i]);
                builder.partIndices.push_back(faceIndices[i + 1]);
            }
        }
    }

    std::fclose(file);
    builder.FlushPart();

    if (model->partCount == 0) {
        LogError("obj: '%s' contained no drawable geometry", path);
        return false;
    }

    // Grow the bounds over every part.
    for (int i = 0; i < model->partCount; ++i) {
        const Mesh* mesh = model->parts[i].mesh;
        if (!mesh) continue;
        const Bounds local = MeshBounds(mesh);
        model->bounds.Grow(local.min);
        model->bounds.Grow(local.max);
    }
    model->loaded = true;
    return true;
}

} // namespace obj

#if defined(NEXIUM_HAS_ASSIMP)
namespace assimp_backend {

bool LoadWithAssimp(const char* path, Model* model) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate |
                                                       aiProcess_GenSmoothNormals |
                                                       aiProcess_JoinIdenticalVertices);
    if (!scene || !scene->mRootNode) {
        LogError("assimp: %s", importer.GetErrorString());
        return false;
    }

    model->materials = new Material[scene->mNumMaterials > 0 ? scene->mNumMaterials : 1];
    model->materialCount = static_cast<int>(scene->mNumMaterials);
    for (unsigned m = 0; m < scene->mNumMaterials; ++m) {
        Material& material = model->materials[m];
        aiString name;
        if (scene->mMaterials[m]->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
            CopyName(material.name, sizeof(material.name), name.C_Str());
        }
        aiColor3D diffuse(1.0f, 1.0f, 1.0f);
        if (scene->mMaterials[m]->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
            material.baseColor = Color{diffuse.r, diffuse.g, diffuse.b, 1.0f};
        }
        if (scene->mMaterials[m]->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            aiString texPath;
            if (scene->mMaterials[m]->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) ==
                AI_SUCCESS) {
                const std::string full = DirectoryOf(path) + texPath.C_Str();
                material.albedo = LoadTextureFromFile(full.c_str(), true);
            }
        }
    }

    model->parts = new ModelPart[scene->mNumMeshes];
    model->partCount = 0;

    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* source = scene->mMeshes[m];
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(source->mNumVertices);

        for (unsigned v = 0; v < source->mNumVertices; ++v) {
            Vertex vertex;
            vertex.position = Vec3{source->mVertices[v].x, source->mVertices[v].y,
                                   source->mVertices[v].z};
            if (source->mNormals) {
                vertex.normal = Normalize(Vec3{source->mNormals[v].x, source->mNormals[v].y,
                                               source->mNormals[v].z});
            }
            if (source->mTextureCoords[0]) {
                vertex.uv = Vec2{source->mTextureCoords[0][v].x, source->mTextureCoords[0][v].y};
            }
            vertex.color = colors::White;
            vertices.push_back(vertex);
        }
        for (unsigned f = 0; f < source->mNumFaces; ++f) {
            const aiFace& face = source->mFaces[f];
            for (unsigned i = 0; i < face.mNumIndices; ++i) indices.push_back(face.mIndices[i]);
        }

        ModelPart& part = model->parts[model->partCount++];
        part.mesh = CreateMesh(vertices.data(), static_cast<uint32_t>(vertices.size()),
                               indices.data(), static_cast<uint32_t>(indices.size()));
        part.materialIndex = static_cast<int>(source->mMaterialIndex);
        CopyName(part.name, sizeof(part.name), source->mName.C_Str());
    }

    for (int i = 0; i < model->partCount; ++i) {
        if (!model->parts[i].mesh) continue;
        const Bounds local = MeshBounds(model->parts[i].mesh);
        model->bounds.Grow(local.min);
        model->bounds.Grow(local.max);
    }
    model->loaded = true;
    return true;
}

} // namespace assimp_backend
#endif

// =============================================================== public =====
Model* LoadModel(const char* path) {
    if (!path) return nullptr;

    auto* model = new Model();
    const std::string extension = ExtensionOf(path);

    bool ok = false;
    if (extension == "obj") {
        ok = obj::LoadObj(path, model);
    } else {
#if defined(NEXIUM_HAS_ASSIMP)
        ok = assimp_backend::LoadWithAssimp(path, model);
#else
        LogError("model: '.%s' needs the Assimp backend (build with NEXIUM_HAS_ASSIMP)",
                 extension.c_str());
#endif
    }

    if (!ok) {
        UnloadModel(model);
        return nullptr;
    }
    LogInfo("model: '%s' — %d parts, %d materials", path, model->partCount, model->materialCount);
    return model;
}

void UnloadModel(Model* model) {
    if (!model) return;
    for (int i = 0; i < model->partCount; ++i) {
        if (model->parts[i].mesh) UnloadMesh(model->parts[i].mesh);
    }
    for (int i = 0; i < model->materialCount; ++i) {
        if (model->materials[i].albedo) UnloadTexture(model->materials[i].albedo);
    }
    delete[] model->parts;
    delete[] model->materials;
    delete model;
}

void DrawModelEx(const Model* model, const Mat4& transform, Color tint) {
    if (!model || !model->loaded) return;
    for (int i = 0; i < model->partCount; ++i) {
        const ModelPart& part = model->parts[i];
        if (!part.mesh) continue;
        const Material* material =
            (part.materialIndex >= 0 && part.materialIndex < model->materialCount)
                ? &model->materials[part.materialIndex]
                : nullptr;
        Texture* texture = material && material->albedo ? material->albedo : GetDefaultTexture();
        const float metallic = material ? material->metallic : 0.0f;
        const float roughness = material ? material->roughness : 0.8f;
        const Color color = material ? material->baseColor : colors::White;
        internal::Renderer::Get().DrawMesh(part.mesh, part.transform * transform,
                                           Color{color.r * tint.r, color.g * tint.g,
                                                 color.b * tint.b, color.a * tint.a},
                                           texture, metallic, roughness);
    }
}

void DrawModel(const Model* model, Vec3 position, Vec3 rotationDegrees, Vec3 scale, Color tint) {
    const Mat4 world = Compose(position,
                               Vec3{Radians(rotationDegrees.x), Radians(rotationDegrees.y),
                                    Radians(rotationDegrees.z)},
                               scale);
    DrawModelEx(model, world, tint);
}

Bounds ModelWorldBounds(const Model* model, Vec3 position, Vec3 rotationDegrees, Vec3 scale) {
    if (!model || !model->loaded) return Bounds{};
    const Mat4 world = Compose(position,
                               Vec3{Radians(rotationDegrees.x), Radians(rotationDegrees.y),
                                    Radians(rotationDegrees.z)},
                               scale);

    // Transform the eight corners of the local box and grow a box around them.
    Bounds result;
    const Vec3 corners[8] = {
        Vec3{model->bounds.min.x, model->bounds.min.y, model->bounds.min.z},
        Vec3{model->bounds.max.x, model->bounds.min.y, model->bounds.min.z},
        Vec3{model->bounds.min.x, model->bounds.max.y, model->bounds.min.z},
        Vec3{model->bounds.max.x, model->bounds.max.y, model->bounds.min.z},
        Vec3{model->bounds.min.x, model->bounds.min.y, model->bounds.max.z},
        Vec3{model->bounds.max.x, model->bounds.min.y, model->bounds.max.z},
        Vec3{model->bounds.min.x, model->bounds.max.y, model->bounds.max.z},
        Vec3{model->bounds.max.x, model->bounds.max.y, model->bounds.max.z},
    };
    for (const Vec3& corner : corners) result.Grow(TransformPoint(world, corner));
    return result;
}

} // namespace n3d
