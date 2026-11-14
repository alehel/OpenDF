#include "Sound.h"

#include "../core/Pff.h"
#include "../render/Terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace df {

namespace {

constexpr float kPi = 3.14159265f;

// DF.EXE's sound table (0x4EB194), used if DF.EXE cannot be read:
// id, name, flags (1 = SNIPER.PWF definition, 2 = LOOP.DEF loop).
const struct { int id; const char* name; int flags; } kBuiltinSounds[] = {
    {1, "ENV_BRIDGE", 1}, {2, "ENV_TOWER", 1}, {10, "ENV_TREE_UZBEK", 1}, {11, "ENV_SKY_UZBEK", 1},
    {12, "ENV_GRND_UZBEK", 1}, {13, "ENV_PEST_UZBEK", 1}, {15, "ENV_TREE_INDOT", 1}, {16, "ENV_SKY_INDO", 1},
    {17, "ENV_GRND_INDO", 1}, {18, "ENV_PEST_INDO", 1}, {20, "ENV_TREE_ZEMLYA", 1}, {21, "ENV_SKY_ZEMLYA", 1},
    {22, "ENV_GRND_ZEMLYA", 1}, {23, "ENV_PEST_ZEMLYA", 1}, {25, "ENV_TREE_CHAD", 1}, {26, "ENV_SKY_CHAD", 1},
    {27, "ENV_GRND_CHAD", 1}, {28, "ENV_PEST_CHAD", 1}, {30, "ENV_TREE_PERU", 1}, {31, "ENV_SKY_PERU", 1},
    {32, "ENV_GRND_PERU", 1}, {33, "ENV_FLY_PERU", 1}, {35, "ENV_TREE_MPDSRT", 1}, {36, "ENV_SKY_MPDSRT", 1},
    {37, "ENV_GRND_MPDSRT", 1}, {38, "ENV_FLY_MPDSRT", 1}, {39, "ENV_CRICKETS1", 1}, {40, "ENV_CRICKETS2", 1},
    {41, "ENV_CRICKETS3", 1}, {42, "ENV_WAVES", 1}, {43, "ENV_FROG1", 1}, {44, "ENV_FROG2", 1},
    {45, "ENV_FROG3", 1}, {50, "EXPLO_M240HE", 1}, {51, "EXPLO_SMOKEGRENAD", 1}, {52, "EXPLO_FRAGGRENADE", 1},
    {53, "EXPLO_WATER", 1}, {54, "BULLET_WOOD", 1}, {55, "BULLET_METAL", 1}, {56, "BULLET_BUSH", 1},
    {57, "BULLET_FLESH", 1}, {58, "BULLET_SAND", 1}, {59, "BULLET_WATER", 1}, {60, "BULLET_DIRT", 1},
    {61, "BULLET_STONE", 1}, {62, "BULLET_ARMOR", 1}, {63, "BULLET_CANVAS", 1}, {64, "BULLET_GRASS", 1},
    {65, "BULLET_BY", 1}, {66, "BULLET_GRASS", 1}, {67, "ENV_SEAGULL_CALL", 1}, {68, "ENV_SEAGULL_DEATH", 1},
    {69, "ENV_VULTURE_CALL", 1}, {70, "ENV_VULTURE_DEATH", 1}, {71, "ENV_VULTURE_CIRCLE", 1}, {72, "ENV_SEAGULL_THUD", 1},
    {73, "ENV_PIRANHA_BITE", 1}, {74, "PENGUIN_INTO_WATER", 1}, {75, "PENGUIN_OUTOF_WTR", 1}, {480, "PENGUIN_VOCALS", 1},
    {481, "PENGUIN_DEATH", 1}, {76, "ENV_SPARROW_CALL", 1}, {300, "PALM_EXPLODE", 1}, {301, "PALM_FALL", 1},
    {302, "PALM_TRUNK", 1}, {303, "PALM_TOP", 1}, {304, "CACTUS_EXPLODE", 1}, {77, "SHRAPNEL_ARMOR", 1},
    {78, "SHRAPNEL_STONE", 1}, {79, "SHRAPNEL_DIRT", 1}, {80, "GS_M203_SHOT", 1}, {81, "GS_M16_BURST", 1},
    {82, "GS_M16_SHOT", 1}, {83, "GF_M16_RELOAD", 1}, {84, "GF_M16_CHARGE", 1}, {85, "GF_M16_DRYFIRE", 1},
    {86, "GS_M19_SHOT", 1}, {87, "GF_M19_DRYFIRE", 1}, {88, "GF_M19_RELOAD", 1}, {89, "GF_M19_CHARGE", 1},
    {90, "GS_BARRETT_SHOT", 1}, {91, "GF_BARRETT_DRYFIRE", 1}, {92, "GF_BARRETT_RELOAD", 1}, {93, "GF_BARRETT_CHARGE", 1},
    {94, "GS_MP5_BURST", 1}, {95, "GF_MP5_DRYFIRE", 1}, {96, "GF_MP5_RELOAD", 1}, {97, "GF_MP5_CHARGE", 1},
    {98, "GS_MP5_SHOT", 1}, {99, "GS_SAW_SHOT", 1}, {100, "GF_SAW_DRYFIRE", 1}, {101, "GF_SAW_RELOAD", 1},
    {102, "GF_SAW_CHARGE", 1}, {103, "GS_M40_SHOT", 1}, {104, "GF_M40_DRYFIRE", 1}, {105, "GF_M40_RELOAD", 1},
    {106, "GF_M40_CHARGE", 1}, {107, "GS_AK47_BURST", 1}, {108, "GS_AK74_BURST", 1}, {109, "GS_HS22_SHOT", 1},
    {110, "GF_HS22_DRYFIRE", 1}, {111, "GF_HS22_RELOAD", 1}, {112, "GF_HS22_CHARGE", 1}, {113, "GS_M203_RELOAD", 1},
    {114, "GS_M203_DRYFIRE", 1}, {115, "GS_LAW_SHOT", 1}, {116, "GF_LAW_DRYFIRE", 1}, {117, "GF_LAW_RELOAD", 1},
    {118, "GS_762", 1}, {119, "GS_50CAL", 1}, {120, "CLAYMORE_SET", 1}, {121, "SATCHEL_SET", 1},
    {130, "FS_DIRT_R", 1}, {131, "FS_DIRT_L", 1}, {132, "FS_METAL_R", 1}, {133, "FS_METAL_L", 1},
    {134, "FS_SNOW_R", 1}, {135, "FS_SNOW_L", 1}, {136, "FS_WATER_L", 1}, {137, "FS_WATER_L", 1},
    {138, "FS_WATER_SLOSH", 1}, {139, "FS_GRASS_L", 1}, {140, "FS_GRASS_L", 1}, {150, "AIVPU1_ATTACK", 1},
    {151, "AIVPU1_SHOTAT", 1}, {152, "AIVPU1_PERIM", 1}, {153, "AIVPU1_WARN", 1}, {154, "AIVPU1_GOTONE", 1},
    {155, "AIVPU1_SHOOTING", 1}, {156, "AIVPU1_DEATH", 1}, {157, "AIVPU1_DEATH_SUDN", 1}, {158, "AIVPU1_DEATH_STRUG", 1},
    {159, "AIVPU1_DEATH_CRAWL", 1}, {160, "AIVPU1_DEATH_BLOWN", 1}, {161, "AIVPU2_ATTACK", 1}, {162, "AIVPU2_SHOTAT", 1},
    {163, "AIVPU2_PERIM", 1}, {164, "AIVPU2_WARN", 1}, {165, "AIVPU2_GOTONE", 1}, {166, "AIVPU2_SHOOTING", 1},
    {167, "AIVPU2_DEATH", 1}, {168, "AIVPU2_DEATH_SUDN", 1}, {169, "AIVPU2_DEATH_STRUG", 1}, {170, "AIVPU2_DEATH_CRAWL", 1},
    {171, "AIVPU2_DEATH_BLOWN", 1}, {172, "AIVPU3_ATTACK", 1}, {173, "AIVPU3_SHOTAT", 1}, {174, "AIVPU3_PERIM", 1},
    {175, "AIVPU3_WARN", 1}, {176, "AIVPU3_GOTONE", 1}, {177, "AIVPU3_SHOOTING", 1}, {178, "AIVPU3_DEATH", 1},
    {179, "AIVPU3_DEATH_SUDN", 1}, {180, "AIVPU3_DEATH_STRUG", 1}, {181, "AIVPU3_DEATH_CRAWL", 1}, {182, "AIVPU3_DEATH_BLOWN", 1},
    {183, "AIVIN1_ATTACK", 1}, {184, "AIVIN1_SHOTAT", 1}, {185, "AIVIN1_PERIM", 1}, {186, "AIVIN1_WARN", 1},
    {187, "AIVIN1_GOTONE", 1}, {188, "AIVIN1_SHOOTING", 1}, {189, "AIVIN1_DEATH", 1}, {190, "AIVIN1_DEATH_SUDN", 1},
    {191, "AIVIN1_DEATH_STRUG", 1}, {192, "AIVIN1_DEATH_CRAWL", 1}, {193, "AIVIN1_DEATH_BLOWN", 1}, {194, "AIVIN2_ATTACK", 1},
    {195, "AIVIN2_SHOTAT", 1}, {196, "AIVIN2_PERIM", 1}, {197, "AIVIN2_WARN", 1}, {198, "AIVIN2_GOTONE", 1},
    {199, "AIVIN2_SHOOTING", 1}, {200, "AIVIN2_DEATH", 1}, {201, "AIVIN2_DEATH_SUDN", 1}, {202, "AIVIN2_DEATH_STRUG", 1},
    {203, "AIVIN2_DEATH_CRAWL", 1}, {204, "AIVIN2_DEATH_BLOWN", 1}, {205, "AIVIN3_ATTACK", 1}, {206, "AIVIN3_SHOTAT", 1},
    {207, "AIVIN3_PERIM", 1}, {208, "AIVIN3_WARN", 1}, {209, "AIVIN3_GOTONE", 1}, {210, "AIVIN3_SHOOTING", 1},
    {211, "AIVIN3_DEATH", 1}, {212, "AIVIN3_DEATH_SUDN", 1}, {213, "AIVIN3_DEATH_STRUG", 1}, {214, "AIVIN3_DEATH_CRAWL", 1},
    {215, "AIVIN3_DEATH_BLOWN", 1}, {216, "AIVRS1_ATTACK", 1}, {217, "AIVRS1_SHOTAT", 1}, {218, "AIVRS1_PERIM", 1},
    {219, "AIVRS1_WARN", 1}, {220, "AIVRS1_GOTONE", 1}, {221, "AIVRS1_SHOOTING", 1}, {222, "AIVRS1_DEATH", 1},
    {223, "AIVRS1_DEATH_SUDN", 1}, {224, "AIVRS1_DEATH_STRUG", 1}, {225, "AIVRS1_DEATH_CRAWL", 1}, {226, "AIVRS1_DEATH_BLOWN", 1},
    {227, "AIVRS2_ATTACK", 1}, {228, "AIVRS2_SHOTAT", 1}, {229, "AIVRS2_PERIM", 1}, {230, "AIVRS2_WARN", 1},
    {231, "AIVRS2_GOTONE", 1}, {232, "AIVRS2_SHOOTING", 1}, {233, "AIVRS2_DEATH", 1}, {234, "AIVRS2_DEATH_SUDN", 1},
    {235, "AIVRS2_DEATH_STRUG", 1}, {236, "AIVRS2_DEATH_CRAWL", 1}, {237, "AIVRS2_DEATH_BLOWN", 1}, {238, "AIVRS3_ATTACK", 1},
    {239, "AIVRS3_SHOTAT", 1}, {240, "AIVRS3_PERIM", 1}, {241, "AIVRS3_WARN", 1}, {242, "AIVRS3_GOTONE", 1},
    {243, "AIVRS3_SHOOTING", 1}, {244, "AIVRS3_DEATH", 1}, {245, "AIVRS3_DEATH_SUDN", 1}, {246, "AIVRS3_DEATH_STRUG", 1},
    {247, "AIVRS3_DEATH_CRAWL", 1}, {248, "AIVRS3_DEATH_BLOWN", 1}, {249, "AIVAR1_ATTACK", 1}, {250, "AIVAR1_SHOTAT", 1},
    {251, "AIVAR1_PERIM", 1}, {252, "AIVAR1_WARN", 1}, {253, "AIVAR1_GOTONE", 1}, {254, "AIVAR1_SHOOTING", 1},
    {255, "AIVAR1_DEATH", 1}, {256, "AIVAR1_DEATH_SUDN", 1}, {257, "AIVAR1_DEATH_STRUG", 1}, {258, "AIVAR1_DEATH_CRAWL", 1},
    {259, "AIVAR1_DEATH_BLOWN", 1}, {260, "AIVAR2_ATTACK", 1}, {261, "AIVAR2_SHOTAT", 1}, {262, "AIVAR2_PERIM", 1},
    {263, "AIVAR2_WARN", 1}, {264, "AIVAR2_GOTONE", 1}, {265, "AIVAR2_SHOOTING", 1}, {266, "AIVAR2_DEATH", 1},
    {268, "AIVAR2_DEATH_STRUG", 1}, {269, "AIVAR2_DEATH_CRAWL", 1}, {270, "AIVAR2_DEATH_BLOWN", 1}, {271, "AIVAR3_ATTACK", 1},
    {272, "AIVAR3_SHOTAT", 1}, {273, "AIVAR3_PERIM", 1}, {274, "AIVAR3_WARN", 1}, {275, "AIVAR3_GOTONE", 1},
    {276, "AIVAR3_SHOOTING", 1}, {277, "AIVAR3_DEATH", 1}, {278, "AIVAR3_DEATH_SUDN", 1}, {279, "AIVAR3_DEATH_STRUG", 1},
    {280, "AIVAR3_DEATH_CRAWL", 1}, {281, "AIVAR3_DEATH_BLOWN", 1}, {305, "EXPLO_SATCHEL", 1}, {306, "EXPLO_CLAYMORE", 1},
    {307, "EXPLO_COPPERHEAD", 1}, {308, "EXPLO_LAW", 1}, {309, "EXPLO_BARREL", 1}, {310, "EXPLO_BARRELS", 1},
    {311, "EXPLO_DISH", 1}, {312, "EXPLO_GENERATOR", 1}, {313, "EXPLO_CRATE_SMALL", 1}, {314, "EXPLO_CRATE_MEDIUM", 1},
    {315, "EXPLO_CRATE_LARGE", 1}, {316, "EXPLO_OILTANK", 1}, {317, "EXPLO_SA2", 1}, {318, "EXPLO_SILKWORM", 1},
    {319, "EXPLO_RADARTRAILER", 1}, {320, "EXPLO_CHAIR", 1}, {321, "EXPLO_TABLE", 1}, {322, "EXPLO_VEHICLE_SMAL", 1},
    {323, "EXPLO_VEHICLE_LARG", 1}, {324, "EXPLO_HELO_CRASH", 1}, {325, "EXPLO_TOWERFALL", 1}, {326, "EXPLO_SIREN", 1},
    {350, "FLAG_PU_P", 1}, {351, "FLAG_PU_T", 1}, {352, "FLAG_PU_O", 1}, {353, "FLAG_DO_P", 1},
    {354, "FLAG_DO_T", 1}, {355, "FLAG_DO_O", 1}, {356, "PU_AMMO", 1}, {357, "HEALTH_PU", 1},
    {358, "SPGOAL_PU", 1}, {359, "KILLED_A_GUY", 1}, {400, "DEATH_WATER", 1}, {401, "AIVTM1_DEATH", 1},
    {402, "AIVTM1_DEATH_SUDN", 1}, {403, "AIVTM1_DEATH_STRUG", 1}, {404, "AIVTM1_DEATH_CRAWL", 1}, {405, "AIVTM1_DEATH_BLOWN", 1},
    {411, "AIVPL1_DEATH", 1}, {412, "AIVPL1_DEATH_SUDN", 1}, {413, "AIVPL1_DEATH_STRUG", 1}, {414, "AIVPL1_DEATH_CRAWL", 1},
    {415, "AIVPL1_DEATH_BLOWN", 1}, {421, "AIVEND_DEATH", 1}, {422, "AIVEND_DEATH_SUDN", 1}, {423, "AIVEND_DEATH_STRUG", 1},
    {424, "AIVEND_DEATH_CRAWL", 1}, {425, "AIVEND_DEATH_BLOWN", 1}, {430, "AIVCOMD_ARTY_CALL", 1}, {431, "AIVMISC_ARTY_AFFIR", 1},
    {432, "AIVMISC_ARTY_DENY", 1}, {433, "AIVMISC_ARTY_SHOT", 1}, {499, "FANFARE", 1}, {500, "ROCKET1", 2},
    {501, "FIRE1", 2}, {502, "gazelle", 2}, {503, "blackhawk", 2}, {504, "FIRE1", 2},
    {505, "FIRE1", 2}, {506, "DIESEL", 2},
};


// Volume by distance (0x4F31F0): index = distance / range * 256.
const int kCurve[256] = {
    255, 238, 223, 210, 199, 189, 180, 172, 164, 158, 152, 146, 141, 136, 132, 127, 124, 120, 116, 113, 110, 107, 104, 101,
    99, 96, 94, 91, 89, 87, 85, 83, 81, 80, 78, 76, 75, 73, 72, 70, 69, 67, 66, 65, 63, 62, 61, 60,
    59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 47, 46, 45, 44, 43, 43, 42, 41, 41, 40, 39,
    39, 38, 38, 37, 36, 36, 35, 35, 34, 34, 33, 33, 32, 32, 31, 31, 30, 30, 29, 29, 28, 28, 27, 27,
    27, 26, 26, 25, 25, 25, 24, 24, 24, 23, 23, 23, 22, 22, 22, 21, 21, 21, 20, 20, 20, 19, 19, 19,
    19, 18, 18, 18, 18, 17, 17, 17, 17, 16, 16, 16, 16, 15, 15, 15, 15, 14, 14, 14, 14, 14, 13, 13,
    13, 13, 13, 12, 12, 12, 12, 12, 11, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 9, 9, 9,
    9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0
};

// Pan (0x4AA019) by direction byte: (right, left) as signed levels of
// 127; 0 ahead, 64 left, 128 behind (one channel inverted), 192 right.
const int kPan[256][2] = {
    {64,63}, {63,64}, {62,65}, {61,66}, {60,67}, {60,67}, {59,68}, {58,69},
    {57,70}, {56,71}, {56,71}, {55,72}, {54,73}, {53,74}, {52,75}, {52,75},
    {51,76}, {50,77}, {49,78}, {48,79}, {48,79}, {47,80}, {46,81}, {45,82},
    {44,83}, {43,84}, {43,84}, {42,85}, {41,86}, {40,87}, {39,88}, {38,89},
    {37,90}, {36,91}, {35,92}, {34,93}, {33,94}, {33,94}, {32,95}, {31,96},
    {30,97}, {29,98}, {28,99}, {26,101}, {25,102}, {24,103}, {23,104}, {22,105},
    {21,106}, {20,107}, {19,108}, {18,109}, {16,111}, {15,112}, {14,113}, {13,114},
    {11,116}, {10,117}, {9,118}, {7,120}, {6,121}, {5,122}, {3,124}, {2,125},
    {0,127}, {-2,125}, {-3,124}, {-5,122}, {-6,121}, {-7,120}, {-9,118}, {-10,117},
    {-11,116}, {-13,114}, {-14,113}, {-15,112}, {-16,111}, {-18,109}, {-19,108}, {-20,107},
    {-21,106}, {-22,105}, {-23,104}, {-24,103}, {-25,102}, {-26,101}, {-28,99}, {-29,98},
    {-30,97}, {-31,96}, {-32,95}, {-33,94}, {-33,94}, {-34,93}, {-35,92}, {-36,91},
    {-37,90}, {-38,89}, {-39,88}, {-40,87}, {-41,86}, {-42,85}, {-43,84}, {-43,84},
    {-44,83}, {-45,82}, {-46,81}, {-47,80}, {-48,79}, {-48,79}, {-49,78}, {-50,77},
    {-51,76}, {-52,75}, {-52,75}, {-53,74}, {-54,73}, {-55,72}, {-56,71}, {-56,71},
    {-57,70}, {-58,69}, {-59,68}, {-60,67}, {-60,67}, {-61,66}, {-62,65}, {-63,64},
    {-63,64}, {64,-63}, {65,-62}, {66,-61}, {67,-60}, {67,-60}, {68,-59}, {69,-58},
    {70,-57}, {71,-56}, {71,-56}, {72,-55}, {73,-54}, {74,-53}, {75,-52}, {75,-52},
    {76,-51}, {77,-50}, {78,-49}, {79,-48}, {79,-48}, {80,-47}, {81,-46}, {82,-45},
    {83,-44}, {84,-43}, {84,-43}, {85,-42}, {86,-41}, {87,-40}, {88,-39}, {89,-38},
    {90,-37}, {91,-36}, {92,-35}, {93,-34}, {94,-33}, {94,-33}, {95,-32}, {96,-31},
    {97,-30}, {98,-29}, {99,-28}, {101,-26}, {102,-25}, {103,-24}, {104,-23}, {105,-22},
    {106,-21}, {107,-20}, {108,-19}, {109,-18}, {111,-16}, {112,-15}, {113,-14}, {114,-13},
    {116,-11}, {117,-10}, {118,-9}, {120,-7}, {121,-6}, {122,-5}, {124,-3}, {125,-2},
    {127,0}, {125,2}, {124,3}, {122,5}, {121,6}, {120,7}, {118,9}, {117,10},
    {116,11}, {114,13}, {113,14}, {112,15}, {111,16}, {109,18}, {108,19}, {107,20},
    {106,21}, {105,22}, {104,23}, {103,24}, {102,25}, {101,26}, {99,28}, {98,29},
    {97,30}, {96,31}, {95,32}, {94,33}, {94,33}, {93,34}, {92,35}, {91,36},
    {90,37}, {89,38}, {88,39}, {87,40}, {86,41}, {85,42}, {84,43}, {84,43},
    {83,44}, {82,45}, {81,46}, {80,47}, {79,48}, {79,48}, {78,49}, {77,50},
    {76,51}, {75,52}, {75,52}, {74,53}, {73,54}, {72,55}, {71,56}, {71,56},
    {70,57}, {69,58}, {68,59}, {67,60}, {67,60}, {66,61}, {65,62}, {64,63}
};

// DF.EXE's distance (0x435bf8): largest component + 11/32 of the middle
// + 1/4 of the smallest.
float approxLength(Vec3 v) {
    float a = std::abs(v.x), b = std::abs(v.y), c = std::abs(v.z);
    if (a < b) std::swap(a, b);
    if (a < c) std::swap(a, c);
    if (b < c) std::swap(b, c);
    return a + b * 11.f / 32.f + c * 0.25f;
}

} // namespace

bool SoundSystem::init(Vfs& vfs, const ExeImage* exe, Audio* audio, std::string* error) {
    audio_ = nullptr;
    std::vector<uint8_t> data;
    if (!vfs.read("SNIPER.PWF", data)) { if (error) *error = "SNIPER.PWF not found"; return false; }
    if (!bank_.load(std::move(data), error)) return false;
    names_.fill({});
    bool fromExe = false;
    if (exe && exe->loaded()) {
        // 0x200 records of 40 bytes: word id at +0, name at +0xE (22 bytes),
        // flags at +0x24. The table is sorted by id at load (0x472e70).
        int found = 0;
        for (uint32_t i = 0; i < 0x200; ++i) {
            const uint32_t rec = 0x4EB194 + i * 40;
            uint32_t w = 0, fl = 0;
            if (!exe->u32(rec, w) || !exe->u32(rec + 0x24, fl)) break;
            const int id = int(int16_t(w & 0xFFFF));
            std::string name;
            for (uint32_t k = 0; k < 24 && name.size() < 22; k += 4) {
                uint32_t v = 0;
                if (!exe->u32(rec + 0xC + k, v)) break;
                for (int b = 0; b < 4; ++b) {
                    const uint32_t off = k + uint32_t(b);
                    if (off < 2 || off >= 24) continue;   // name starts at +0xE
                    const char c = char((v >> (8 * b)) & 0xFF);
                    if (!c) { k = 24; break; }
                    name += c;
                }
            }
            if (id <= 0 || id >= 0x200 || name.empty() || !(fl & 3)) continue;
            if (names_[size_t(id)].empty()) names_[size_t(id)] = name;
            ++found;
        }
        fromExe = found > 200 && names_[0x32] == "EXPLO_M240HE";
    }
    if (!fromExe) {
        names_.fill({});
        for (const auto& s : kBuiltinSounds)
            if (names_[size_t(s.id)].empty()) names_[size_t(s.id)] = s.name;
    }
    for (int i = 0; i < 0x200; ++i) byId_[size_t(i)] = names_[size_t(i)].empty() ? nullptr : bank_.def(names_[size_t(i)]);
    // Voice sets (0x4F019C: 16 sets of 12 sound ids).
    phrases_.assign(16 * 12, 0);
    bool phrasesOk = false;
    if (exe && exe->loaded()) {
        phrasesOk = true;
        for (uint32_t i = 0; i < 16 * 12; ++i) {
            int32_t v = 0;
            if (!exe->i32(0x4F019C + 4 * i, v)) { phrasesOk = false; break; }
            phrases_[i] = v;
        }
        phrasesOk = phrasesOk && phrases_[12 + 1] == 150;
    }
    if (!phrasesOk) {
        // Built-in copy: sets 1-12 slots 1-11 = 150 + 11 (set - 1) + slot - 1
        // (set 11 slot 8 is 237 in DF.EXE); sets 13-15 slots 7-11 only.
        phrases_.assign(16 * 12, 0);
        for (int st = 1; st <= 12; ++st)
            for (int k = 1; k <= 11; ++k) phrases_[size_t(st * 12 + k)] = 150 + 11 * (st - 1) + k - 1;
        phrases_[11 * 12 + 8] = 237;
        for (int st = 13; st <= 15; ++st)
            for (int k = 7; k <= 11; ++k) phrases_[size_t(st * 12 + k)] = 401 + (st - 13) * 10 + k - 7;
    }
    // LOOP.DEF: "name" "wav file" volume clip maxdist pitcha pitchb.
    loopDefs_.clear();
    if (vfs.read("LOOP.DEF", data)) {
        std::string text(data.begin(), data.end()), line;
        std::istringstream in(text);
        while (std::getline(in, line)) {
            std::vector<std::string> tok;
            std::string cur;
            bool quoted = false;
            for (char c : line) {
                if (c == '"') { quoted = !quoted; if (!quoted) { tok.push_back(cur); cur.clear(); } continue; }
                if (!quoted && (c == ' ' || c == '\t' || c == ',' || c == '\r')) { if (!cur.empty()) tok.push_back(cur), cur.clear(); continue; }
                cur += c;
            }
            if (!cur.empty()) tok.push_back(cur);
            if (tok.size() < 5 || line.find('"') == std::string::npos) continue;
            LoopDef l;
            l.file = tok[1];
            l.volume = std::atoi(tok[2].c_str());
            l.clip = std::atoi(tok[3].c_str());
            l.maxDistance = float(std::atof(tok[4].c_str()));
            std::string key = tok[0];
            for (auto& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));
            loopDefs_[key] = l;
        }
    }
    vfs_ = &vfs;
    audio_ = audio;
    return true;
}

int SoundSystem::direction(Vec3 at) const {
    const float bearing = std::atan2(at.x - listener_.x, -(at.z - listener_.z));
    float rel = bearing - listenerYaw_;
    rel -= std::floor(rel / (2 * kPi)) * 2 * kPi;
    return (255 - int(rel / (2 * kPi) * 256.f)) & 0xFF;
}

void SoundSystem::setLoop(int key, int id, Vec3 at, float pitch) {
    // The loop's pitch follows the thing (obj+0x20, 0x473bcd).
    for (auto& e : emitters_)
        if (e.key == key) { e.at = at; e.seen = true; e.pitch = pitch; return; }
    emitters_.push_back({key, id, at, true, 0, pitch});
}

void SoundSystem::sweepLoops() {
    for (auto& e : emitters_)
        if (!e.seen && e.voice && audio_) audio_->stop(e.voice);
    emitters_.erase(std::remove_if(emitters_.begin(), emitters_.end(), [](const Emitter& e) { return !e.seen; }),
                    emitters_.end());
    for (auto& e : emitters_) e.seen = false;
}

int SoundSystem::idOf(const std::string& name) const {
    auto up = [](std::string s) { for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; };
    const std::string n = up(name);
    for (int i = 1; i < 0x200; ++i)
        if (!names_[size_t(i)].empty() && up(names_[size_t(i)]) == n) return i;
    return 0;
}

void SoundSystem::clearLoop(int key) {
    for (auto& e : emitters_)
        if (e.key == key && e.voice && audio_) audio_->stop(e.voice);
    emitters_.erase(std::remove_if(emitters_.begin(), emitters_.end(), [&](const Emitter& e) { return e.key == key; }),
                    emitters_.end());
}

void SoundSystem::updateLoops() {
    if (!audio_ || (loopFrame_++ & 7)) return;
    struct Live { Emitter* e; int vol; };
    std::vector<Live> live;
    for (auto& e : emitters_) {
        const std::string& name = (e.id > 0 && e.id < 0x200) ? names_[size_t(e.id)] : std::string();
        std::string key = name;
        for (auto& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));
        auto it = loopDefs_.find(key);
        int vol = 0;
        if (it != loopDefs_.end()) {
            const float d = approxLength(e.at - listener_);
            if (d < it->second.maxDistance)
                vol = std::min(it->second.volume * kCurve[std::clamp(int(d / it->second.maxDistance * 256.f), 0, 255)] >> 8,
                               it->second.clip);
        }
        live.push_back({&e, vol});
    }
    std::stable_sort(live.begin(), live.end(), [](const Live& a, const Live& b) { return a.vol > b.vol; });
    for (size_t i = 0; i < live.size(); ++i) {
        Emitter& e = *live[i].e;
        const int vol = live[i].vol;
        if (i >= 6 || vol <= 0) {
            if (e.voice) audio_->stop(e.voice), e.voice = 0;
            continue;
        }
        const int p = direction(e.at);
        const float right = float(kPan[p][0]) * vol / (127.f * 255.f), left = float(kPan[p][1]) * vol / (127.f * 255.f);
        if (e.voice && audio_->playing(e.voice)) {
            audio_->setGains(e.voice, left, right);
            audio_->setPitch(e.voice, e.pitch);
        }
        else {
            std::string key = names_[size_t(e.id)];
            for (auto& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));
            const SoundClip* clip = vfs_ ? audio_->clip(*vfs_, loopDefs_[key].file) : nullptr;
            e.voice = audio_->play(clip, left, right, e.pitch, vol, true, 1);
        }
    }
}

void SoundSystem::setListener(Vec3 pos, float yaw, const Terrain* terrain) {
    listener_ = pos;
    listenerYaw_ = yaw;
    terrain_ = terrain;
}

uint32_t SoundSystem::random() {
    // 0x49213c: seed = rol(rol(seed, 11) + seed, 3).
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    seed_ = rol(rol(seed_, 11) + seed_, 3);
    return seed_;
}

float SoundSystem::occlusion(Vec3 a, Vec3 b) const {
    // 0x473358 (a = the sound, b = the listener): nothing under 1 m apart
    // horizontally; else walk the longer horizontal axis (a tie: the other)
    // from the end with the smaller coordinate in exact 1 m steps, both ends
    // included, reading the nearest raw cell height; where the line is under
    // the ground add the whole metres (floor) it is under.
    if (!terrain_) return 0;
    const Vec3 d = b - a;
    if (std::abs(d.x) < 1.f && std::abs(d.z) < 1.f) return 0;
    const bool xMajor = std::abs(d.z) < std::abs(d.x);
    const float dm = xMajor ? d.x : d.z;
    const Vec3 start = (xMajor ? a.x <= b.x : a.z <= b.z) ? a : b;
    const float end = xMajor ? std::max(a.x, b.x) : std::max(a.z, b.z);
    const Vec3 step = d * (1.f / std::abs(dm)) * (dm < 0 ? -1.f : 1.f);   // +1 m along the major axis
    int sum = 0;
    for (Vec3 p = start; (xMajor ? p.x : p.z) <= end; p = p + step) {
        const float h = terrain_->cellHeight(p.x, p.z);
        if (p.y < h) sum += int(std::floor(h - p.y));
    }
    return float(sum);
}

void SoundSystem::start(SoundDef& def, float distance, int dir, bool positional, float callerPitch) {
    // 0x492210, per layer: a variant (in turn or at random), pitch =
    // (def base + rand) x (variant base + rand) x caller pitch, volume =
    // min(gain x curve[d / range x 256] >> 8, cap), nothing beyond the range.
    auto rnd = [&](float range) { return range * float(random() & 0xFF) / 256.f; };
    const float defPitch = def.pitchBase + rnd(def.pitchRand);
    for (auto& layer : def.layers) {
        if (!(layer.flags & 6) || layer.variants.empty()) continue;   // mask 0x4F31EC = 6
        if (layer.range > 0 && distance >= layer.range) continue;
        const int count = int(layer.variants.size());
        const int pick = (layer.flags & 0x10) ? (layer.next++ % count) : int((random() & 0xFF) * uint32_t(count) >> 8);
        const SoundVariant& v = layer.variants[size_t(pick)];
        // A layer without a range plays at the caller's volume (255).
        const int vol = layer.range > 0
                            ? std::min(v.gain * kCurve[std::clamp(int(distance / layer.range * 256.f), 0, 255)] >> 8, v.cap)
                            : 255;
        if (vol <= 0) continue;
        const float pitch = defPitch * (v.pitchBase + rnd(v.pitchRand)) * (callerPitch > 0 ? callerPitch : 1.f);
        const int p = (positional && (layer.flags & 1)) ? dir : 0;
        // Gain per channel = pan x volume (0x4a99b0), 127 x 255 = full.
        const float right = float(kPan[p][0]) * vol / (127.f * 255.f), left = float(kPan[p][1]) * vol / (127.f * 255.f);
        audio_->play(bank_.clip(v.sample), left, right, pitch, vol);
    }
}

void SoundSystem::play(int id, Vec3 at, float pitch) {
    if (!audio_ || id <= 0 || id >= 0x200 || !byId_[size_t(id)]) return;
    SoundDef& def = *byId_[size_t(id)];
    // 0x473220: distance, cut at the definition's maximum; terrain in the
    // way makes it seem farther (x (1 + metres / 512)), cut again.
    float d = approxLength(at - listener_);
    if (d > def.maxDistance) return;
    d *= 1.f + occlusion(listener_, at) / 512.f;
    if (d > def.maxDistance) return;
    // Direction byte: 0xFF - ((listener yaw - bearing) >> 24) in DF.EXE's
    // anticlockwise angles, i.e. the bearing relative to the heading,
    // anticlockwise, in 256ths of a turn.
    start(def, d, direction(at), true, pitch);
}

void SoundSystem::play2D(int id) {
    if (!audio_ || id <= 0 || id >= 0x200 || !byId_[size_t(id)]) return;
    start(*byId_[size_t(id)], 0, 0, false, 0);
}

float SoundSystem::range(int id) const {
    if (id <= 0 || id >= 0x200 || !byId_[size_t(id)]) return 0;
    return byId_[size_t(id)]->maxDistance;
}

bool SoundSystem::loadDialogBank(Vfs& vfs, const std::string& name) {
    std::vector<uint8_t> data;
    return vfs.read(name, data) && dialogs_.load(std::move(data));
}

bool SoundSystem::playDialogLine(const std::string& clip) {
    // 0x472f44: centred, pitch 1, volume dialog_volume (default 200) x 255 >> 8;
    // nothing at all when it is 0.
    if (!audio_ || dialogVolume_ <= 0) return false;
    const int i = dialogs_.sampleIndex(clip);
    if (i < 0) return false;
    const int vol = dialogVolume_ * 255 >> 8;
    const float g = 64.f * vol / (127.f * 255.f), g2 = 63.f * vol / (127.f * 255.f);
    return audio_->play(dialogs_.clip(i), g, g2, 1.f, vol) != 0;
}

int SoundSystem::dialogLineTicks(const std::string& clip) {
    const int i = dialogs_.sampleIndex(clip);
    const SoundClip* c = i >= 0 ? dialogs_.clip(i) : nullptr;
    return c && c->rate > 0 ? int(c->samples.size() * 60 / size_t(c->rate)) : 0;
}

void SoundSystem::voice(int set, int slot, Vec3 at) {
    if (set < 0 || set >= 16 || slot < 0 || slot >= 12) return;
    const int id = phrases_.empty() ? 0 : phrases_[size_t(set * 12 + slot)];
    if (id > 0) play(id, at + Vec3{0, 1.f, 0});
}

void SoundSystem::stopAll() {
    emitters_.clear();
    if (audio_) audio_->stopAll();
}

} // namespace df
