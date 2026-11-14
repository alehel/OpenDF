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
    std::vector<TerrainColorType> types;
};

// Delta Force 2: a mission's copy of a SUNSETS.DEF entry (BMS header 0xB8..),
// replacing the TRN's sky and colour grading when both names are set
// (DF2.EXE 0x4654b6..0x465590).
struct SkyOverride {
    bool on = false;
    std::string clouds, gradient;
    float height = 0, horizon = 1;
    int gamma = 128, saturation = 128, filter[3] = {128, 128, 128};
};

bool parseTrn(const std::string& text, TerrainDesc& out);

} // namespace df
