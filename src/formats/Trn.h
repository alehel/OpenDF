// .TRN terrain descriptors: plain-text key/value files naming the PCX maps
// and atmosphere settings for one Voxel Space terrain.
#pragma once

#include <string>
#include <vector>

namespace df {

struct TerrainColorType {
    int r, g, b;
    std::string material;  // grass, dirt, cement, packed_dirt, ...
};

struct TerrainDesc {
    std::string name, creator;
    std::string colorMap, elevMap, detailMap;
    std::string charData;   // Delta Force 2: char_data, a .CAL surface class per detail tile
    std::string detailColor, detailElev, detailShade;
    std::string skyMap, skyPalette;
    std::string waterMap, waterPalette;
    float skyHeight = 1236.f;   // DF.EXE default (0x42fbe8); metres
    float waterHeight = 0.f;
    float waterOpacity = 0.f;
    float horizon = 1.f;
    std::string camouflage;
    int filter[3] = {128, 128, 128};
    int gamma = 128;
    int saturation = 128;
    int sunSlope = 45;
    // Delta Force 2 (parser 0x465ab0, record +0x15C / +0x160): fog distance
    // 2^fog_level m, 0 = no fog; fog_color R,G,B, default (192,192,192)
    // (0x4639db). No TRN on the disc has them; missions override them.
    int fogLevel = 0;
    int fogColor[3] = {192, 192, 192};
    // Delta Force 2 only (GOG Df2.exe parser 0x46f9b0, record +0x144..+0x14c
    // and +0x118): turbidity R,G,B, default (23, 8, 4); height_scale,
    // default 0. Read by Df2.exe's renderer (0x470aa0, 0x472440); OpenDF
    // keeps them but does not use them yet.
    int turbidity[3] = {23, 8, 4};
    int heightScale = 0;
    std::vector<TerrainColorType> types;
};

// Delta Force 2's TRN record defaults (GOG Df2.exe 0x46d890, CD 0x463990,
// run by 0x4700f0 before each parse): everything 0 except sky_height 1236,
// sun_slope 60, horizon 1, turbidity (23, 8, 4) and fog colour 192s.
TerrainDesc df2TerrainDefaults();

// Delta Force 2: a mission's copy of a SUNSETS.DEF entry (BMS header 0xB8..),
// replacing the TRN's sky and colour grading when both names are set
// (DF2.EXE 0x4654b6..0x465590).
struct SkyOverride {
    bool on = false;
    std::string clouds, gradient;
    float height = 0, horizon = 1;
    int gamma = 128, saturation = 128, filter[3] = {128, 128, 128};
};

// df2: Df2.exe's parser 0x46f9b0 (CD 0x465ab0): keys compared with
// _strcmpi, plus turbidity and height_scale.
bool parseTrn(const std::string& text, TerrainDesc& out, bool df2 = false);

} // namespace df
