// DF.EXE's move table (0x4e585c, 0x34-byte records: name, number at +0x20,
// +0x28 how visible the move makes a soldier (0x46a804: stand 1, walk 2,
// run 4, crouch 0.5, prone 0.08), +0x2C how hard it is to aim at (0x46b838)).
#include "Moves.h"

namespace df {

namespace {
const struct { int number; const char* name; float visibility, aim; } kMoves[] = {
    {0, "Null", 1.f, 1.f}, {1, "Standing_1HW", 1.f, 1.f}, {2, "Standing_2HW", 1.f, 1.f},
    {3, "Standing_SLW", 1.f, 1.f}, {4, "Standing_SCW", 1.f, 1.f}, {5, "Standing_BIN", 1.f, 1.f},
    {6, "Standing_SWTCH", 1.f, 1.f}, {7, "Standing_KNIFE", 1.f, 1.f}, {10, "Standing_S_1HW", 1.f, 1.f},
    {11, "Standing_S_2HW", 1.f, 1.f}, {12, "Standing_S_SLW", 1.f, 1.f}, {13, "Standing_S_SCW", 1.f, 1.f},
    {14, "Standing_S_KNIFE", 1.f, 1.f},
    // Delta Force 2 only (DF2.EXE's table 0x4ab758, records 13..17; DF.EXE
    // has no numbers 15..19): the ladder moves.
    {15, "Ladder_Stand", 1.f, 1.f}, {16, "Ladder_Up", 1.f, 1.f}, {17, "Ladder_Down", 1.f, 1.f},
    {18, "Ladder_Left", 1.f, 1.f}, {19, "Ladder_Right", 1.f, 1.f},
    {20, "Walking0_1HW", 2.f, 2.f}, {21, "Walking1_1HW", 2.f, 2.f},
    {22, "Walking2_1HW", 2.f, 2.f}, {23, "Walking3_1HW", 2.f, 2.f}, {24, "Walking4_1HW", 2.f, 2.f},
    {25, "Walking5_1HW", 2.f, 2.f}, {26, "Walking6_1HW", 2.f, 2.f}, {27, "Walking7_1HW", 2.f, 2.f},
    {28, "Walking0_2HW", 2.f, 2.f}, {29, "Walking1_2HW", 2.f, 2.f}, {30, "Walking2_2HW", 2.f, 2.f},
    {31, "Walking3_2HW", 2.f, 2.f}, {32, "Walking4_2HW", 2.f, 2.f}, {33, "Walking5_2HW", 2.f, 2.f},
    {34, "Walking6_2HW", 2.f, 2.f}, {35, "Walking7_2HW", 2.f, 2.f}, {40, "Walking0_S_1HW", 2.f, 2.f},
    {41, "Walking1_S_1HW", 2.f, 2.f}, {42, "Walking2_S_1HW", 2.f, 2.f}, {43, "Walking3_S_1HW", 2.f, 2.f},
    {44, "Walking4_S_1HW", 2.f, 2.f}, {45, "Walking5_S_1HW", 2.f, 2.f}, {46, "Walking6_S_1HW", 2.f, 2.f},
    {47, "Walking7_S_1HW", 2.f, 2.f}, {48, "Walking0_S_2HW", 2.f, 2.f}, {49, "Walking1_S_2HW", 2.f, 2.f},
    {50, "Walking2_S_2HW", 2.f, 2.f}, {51, "Walking3_S_2HW", 2.f, 2.f}, {52, "Walking4_S_2HW", 2.f, 2.f},
    {53, "Walking5_S_2HW", 2.f, 2.f}, {54, "Walking6_S_2HW", 2.f, 2.f}, {55, "Walking7_S_2HW", 2.f, 2.f},
    {60, "Running0_1HW", 4.f, 4.f}, {61, "Running1_1HW", 4.f, 4.f}, {62, "Running2_1HW", 4.f, 4.f},
    {63, "Running3_1HW", 4.f, 4.f}, {64, "Running4_1HW", 4.f, 4.f}, {65, "Running5_1HW", 4.f, 4.f},
    {66, "Running6_1HW", 4.f, 4.f}, {67, "Running7_1HW", 4.f, 4.f}, {68, "Running0_2HW", 4.f, 4.f},
    {69, "Running1_2HW", 4.f, 4.f}, {70, "Running2_2HW", 4.f, 4.f}, {71, "Running3_2HW", 4.f, 4.f},
    {72, "Running4_2HW", 4.f, 4.f}, {73, "Running5_2HW", 4.f, 4.f}, {74, "Running6_2HW", 4.f, 4.f},
    {75, "Running7_2HW", 4.f, 4.f}, {80, "Running0_S_1HW", 4.f, 4.f}, {81, "Running1_S_1HW", 4.f, 4.f},
    {82, "Running2_S_1HW", 4.f, 4.f}, {83, "Running3_S_1HW", 4.f, 4.f}, {84, "Running4_S_1HW", 4.f, 4.f},
    {85, "Running5_S_1HW", 4.f, 4.f}, {86, "Running6_S_1HW", 4.f, 4.f}, {87, "Running7_S_1HW", 4.f, 4.f},
    {88, "Running0_S_2HW", 4.f, 4.f}, {89, "Running1_S_2HW", 4.f, 4.f}, {90, "Running2_S_2HW", 4.f, 4.f},
    {91, "Running3_S_2HW", 4.f, 4.f}, {92, "Running4_S_2HW", 4.f, 4.f}, {93, "Running5_S_2HW", 4.f, 4.f},
    {94, "Running6_S_2HW", 4.f, 4.f}, {95, "Running7_S_2HW", 4.f, 4.f}, {100, "Crouched_1HW", 0.5f, 1.f},
    {101, "Crouched_2HW", 0.5f, 1.f}, {102, "Crouched_SLW", 0.5f, 1.f}, {103, "Crouched_SCW", 0.5f, 1.f},
    {104, "Crouched_BIN", 0.5f, 1.f}, {105, "Crouched_SWTCH", 0.5f, 1.f}, {106, "Crouched_KNIFE", 0.5f, 1.f},
    // GOG Df2.exe's table: 107 Crouched_EWeap, manning an emplaced gun (0x445980).
    {107, "Crouched_EWeap", 0.5f, 1.f},
    {110, "Prone_1HW", 0.08f, 1.f}, {111, "Prone_2HW", 0.08f, 1.f}, {112, "Prone_SLW", 0.08f, 1.f},
    {113, "Prone_SCW", 0.08f, 1.f}, {114, "Prone_BIN", 0.08f, 1.f}, {115, "Prone_SWTCH", 0.08f, 1.f},
    {116, "Prone_KNIFE", 0.08f, 1.f}, {120, "Crawling_CR0", 0.2f, 1.2f}, {121, "Crawling_CR1", 0.2f, 1.2f},
    {122, "Crawling_CR2", 0.2f, 1.2f}, {123, "Crawling_CR3", 0.2f, 1.2f}, {124, "Crawling_CR4", 0.2f, 1.2f},
    {125, "Crawling_CR5", 0.2f, 1.2f}, {126, "Crawling_CR6", 0.2f, 1.2f}, {127, "Crawling_CR7", 0.2f, 1.2f},
    {130, "Jumping_1HW", 4.f, 4.f}, {131, "Jumping_2HW", 4.f, 4.f}, {132, "Jumping_S_1HW", 4.f, 4.f},
    {133, "Jumping_S_2HW", 4.f, 4.f}, {140, "Floating", 0.2f, 2.f}, {141, "Swimming", 0.2f, 2.f},
    {150, "F1", 1.f, 1.f}, {151, "F2", 1.f, 1.f}, {152, "F3", 1.f, 1.f},
    {153, "F4", 1.f, 1.f}, {154, "F5", 1.f, 1.f}, {155, "F6", 1.f, 1.f},
    {156, "F7", 1.f, 1.f}, {157, "F8", 1.f, 1.f}, {158, "F9", 1.f, 1.f},
    {159, "F10", 1.f, 1.f}, {160, "F11", 1.f, 1.f}, {161, "F12", 1.f, 1.f},
    {170, "F1_C", 1.f, 1.f}, {171, "F2_C", 1.f, 1.f}, {172, "F3_C", 1.f, 1.f},
    {173, "F4_C", 1.f, 1.f}, {174, "F5_C", 1.f, 1.f}, {175, "F6_C", 1.f, 1.f},
    {176, "F7_C", 1.f, 1.f}, {177, "F8_C", 1.f, 1.f}, {178, "F9_C", 1.f, 1.f},
    {179, "F10_C", 1.f, 1.f}, {180, "F11_C", 1.f, 1.f}, {181, "F12_C", 1.f, 1.f},
    {190, "Death_B_P01DF", 1.f, 1.f}, {191, "Death_B_P01DL", 1.f, 1.f}, {192, "Death_B_P01DB", 1.f, 1.f},
    {193, "Death_B_P01DR", 1.f, 1.f}, {194, "Death_B_P02DF", 1.f, 1.f}, {195, "Death_B_P02DL", 1.f, 1.f},
    {196, "Death_B_P02DB", 1.f, 1.f}, {197, "Death_B_P02DR", 1.f, 1.f}, {198, "Death_B_P03DF", 1.f, 1.f},
    {199, "Death_B_P03DL", 1.f, 1.f}, {200, "Death_B_P03DB", 1.f, 1.f}, {201, "Death_B_P03DR", 1.f, 1.f},
    {202, "Death_B_P04DF", 1.f, 1.f}, {203, "Death_B_P04DL", 1.f, 1.f}, {204, "Death_B_P04DB", 1.f, 1.f},
    {205, "Death_B_P04DR", 1.f, 1.f}, {206, "Death_B_P05DF", 1.f, 1.f}, {207, "Death_B_P05DL", 1.f, 1.f},
    {208, "Death_B_P05DB", 1.f, 1.f}, {209, "Death_B_P05DR", 1.f, 1.f}, {210, "Death_B_P06DF", 1.f, 1.f},
    {211, "Death_B_P06DL", 1.f, 1.f}, {212, "Death_B_P06DB", 1.f, 1.f}, {213, "Death_B_P06DR", 1.f, 1.f},
    {214, "Death_B_P07DF", 1.f, 1.f}, {215, "Death_B_P07DL", 1.f, 1.f}, {216, "Death_B_P07DB", 1.f, 1.f},
    {217, "Death_B_P07DR", 1.f, 1.f}, {218, "Death_B_P08DF", 1.f, 1.f}, {219, "Death_B_P08DL", 1.f, 1.f},
    {220, "Death_B_P08DB", 1.f, 1.f}, {221, "Death_B_P08DR", 1.f, 1.f}, {222, "Death_B_P09DF", 1.f, 1.f},
    {223, "Death_B_P09DL", 1.f, 1.f}, {224, "Death_B_P09DB", 1.f, 1.f}, {225, "Death_B_P09DR", 1.f, 1.f},
    {226, "Death_B_P10DF", 1.f, 1.f}, {227, "Death_B_P10DL", 1.f, 1.f}, {228, "Death_B_P10DB", 1.f, 1.f},
    {229, "Death_B_P10DR", 1.f, 1.f}, {230, "Death_B_P11DF", 1.f, 1.f}, {231, "Death_B_P11DL", 1.f, 1.f},
    {232, "Death_B_P11DB", 1.f, 1.f}, {233, "Death_B_P11DR", 1.f, 1.f}, {234, "Death_B_P12DF", 1.f, 1.f},
    {235, "Death_B_P12DL", 1.f, 1.f}, {236, "Death_B_P12DB", 1.f, 1.f}, {237, "Death_B_P12DR", 1.f, 1.f},
    {238, "Death_B_P13DF", 1.f, 1.f}, {239, "Death_B_P13DL", 1.f, 1.f}, {240, "Death_B_P13DB", 1.f, 1.f},
    {241, "Death_B_P13DR", 1.f, 1.f}, {242, "Death_B_P14DF", 1.f, 1.f}, {243, "Death_B_P14DL", 1.f, 1.f},
    {244, "Death_B_P14DB", 1.f, 1.f}, {245, "Death_B_P14DR", 1.f, 1.f}, {246, "Death_B_P15DF", 1.f, 1.f},
    {247, "Death_B_P15DL", 1.f, 1.f}, {248, "Death_B_P15DB", 1.f, 1.f}, {249, "Death_B_P15DR", 1.f, 1.f},
    {250, "Death_G_DF", 1.f, 1.f}, {251, "Death_G_DL", 1.f, 1.f}, {252, "Death_G_DB", 1.f, 1.f},
    {253, "Death_G_DR", 1.f, 1.f}, {254, "Death_Water", 1.f, 1.f}, {255, "Death_Fire", 1.f, 1.f},
};
} // namespace

const char* moveName(int number) {
    for (const auto& m : kMoves)
        if (m.number == number) return m.name;
    return nullptr;
}

int moveNumber(const std::string& name) {
    for (const auto& m : kMoves)
        if (name == m.name) return m.number;
    return -1;
}

float moveVisibility(int number) {
    for (const auto& m : kMoves)
        if (m.number == number) return m.visibility;
    return 1;
}

float moveAimFactor(int number) {
    for (const auto& m : kMoves)
        if (m.number == number) return m.aim;
    return 1;
}

} // namespace df
