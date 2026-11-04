#pragma once
#include "level_model.h"
#include "ui.h"

// Read-only catalogue of the disc's static geometry and textures. Placing an asset
// makes a portable OBJ/picture copy; originals and game archives are never changed.
namespace wb::assets {
enum class Kind { Model, Texture };
struct Asset {
    Kind kind;
    std::string archive, entry, folder, name;
    bool stage_piece = false;
    std::string Key() const { return archive + "|" + entry; }
};
struct Geometry {
    lv::ModelMesh mesh;
    std::vector<std::string> textures;  // t.con entry per material, empty if unresolved
};
const std::vector<Asset>& Catalogue();
Geometry ReadGeometry(const Asset& asset);
bool Thumbnail(const Asset& asset, Bitmap& out, uint32_t side = 128);
bool TextureFile(const std::string& entry, fs::path& png, std::string& error);
bool PlaceModel(lv::Level& level, const Asset& asset, float x, float z, std::string& error);
// Uses a native stage texture when available; other textures become portable pictures.
bool ApplyTexture(lv::Level& level, const Asset& asset, int target, int selected_box,
                  int selected_model, int material, std::string& error);
}  // namespace wb::assets
