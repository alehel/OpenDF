#include "Trn.h"

#include <cstdlib>
#include <sstream>

namespace df {

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

bool parseTrn(const std::string& text, TerrainDesc& d) {
    std::istringstream in(text);
    std::string line;
    bool any = false;
    while (std::getline(in, line)) {
        if (auto c = line.find("//"); c != std::string::npos) line.resize(c);
        line = trim(line);
        if (line.empty()) continue;
        size_t sp = line.find_first_of(" \t");
        std::string key = line.substr(0, sp);
        std::string val = sp == std::string::npos ? "" : trim(line.substr(sp));
        any = true;

        auto f = [&] { return float(std::atof(val.c_str())); };
        if (key == "terrain_name") d.name = unquote(val);
        else if (key == "terrain_creator") d.creator = unquote(val);
        else if (key == "color_map") d.colorMap = val;
        else if (key == "elev_map") d.elevMap = val;
        else if (key == "detail_map") d.detailMap = val;
        else if (key == "char_data") d.charData = val;
        else if (key == "detail_color") d.detailColor = val;
        else if (key == "detail_elev") d.detailElev = val;
        else if (key == "detail_shade") d.detailShade = val;
        else if (key == "sky_map") d.skyMap = val;
        else if (key == "sky_palette") d.skyPalette = val;
        else if (key == "sky_height") d.skyHeight = f();
        else if (key == "water_map") d.waterMap = val;
        else if (key == "water_palette") d.waterPalette = val;
        else if (key == "water_height") d.waterHeight = f();
        else if (key == "water_opacity") d.waterOpacity = f();
        else if (key == "horizon") d.horizon = f();
        else if (key == "camouflage") d.camouflage = val;
        else if (key == "gamma") d.gamma = std::atoi(val.c_str());
        else if (key == "saturation") d.saturation = std::atoi(val.c_str());
        else if (key == "sun_slope") d.sunSlope = std::atoi(val.c_str());
        else if (key == "filter") {
            std::string v = val;
            for (auto& ch : v) if (ch == ',') ch = ' ';
            std::istringstream fs(v);
            fs >> d.filter[0] >> d.filter[1] >> d.filter[2];
        } else if (key == "type") {
            std::istringstream ts(val);
            TerrainColorType t{};
            if (ts >> t.r >> t.g >> t.b >> t.material) d.types.push_back(t);
        }
    }
    return any && !d.colorMap.empty() && !d.elevMap.empty();
}

} // namespace df
