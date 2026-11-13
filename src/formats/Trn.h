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
    std::string detailColor, detailElev, detailShade;
    std::string skyMap, skyPalette;
    std::string waterMap, waterPalette;
    float skyHeight = 1000.f;
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

bool parseTrn(const std::string& text, TerrainDesc& out);

} // namespace df
