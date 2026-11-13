// DF.EXE's move names (0x4e585c, 0x34-byte records: name, number at +0x20):
// the numbers the game code uses for moves (0x46c920 deaths, 0x43752e
// swimming, ...) and the names the .ANM files define them by.
#include "Moves.h"

namespace df {

namespace {
const struct { int number; const char* name; } kMoves[] = {
    {0, "Null"}, {1, "Standing_1HW"}, {2, "Standing_2HW"}, {3, "Standing_SLW"},
    {4, "Standing_SCW"}, {5, "Standing_BIN"}, {6, "Standing_SWTCH"}, {7, "Standing_KNIFE"},
    {10, "Standing_S_1HW"}, {11, "Standing_S_2HW"}, {12, "Standing_S_SLW"}, {13, "Standing_S_SCW"},
    {14, "Standing_S_KNIFE"}, {20, "Walking0_1HW"}, {21, "Walking1_1HW"}, {22, "Walking2_1HW"},
    {23, "Walking3_1HW"}, {24, "Walking4_1HW"}, {25, "Walking5_1HW"}, {26, "Walking6_1HW"},
    {27, "Walking7_1HW"}, {28, "Walking0_2HW"}, {29, "Walking1_2HW"}, {30, "Walking2_2HW"},
    {31, "Walking3_2HW"}, {32, "Walking4_2HW"}, {33, "Walking5_2HW"}, {34, "Walking6_2HW"},
    {35, "Walking7_2HW"}, {40, "Walking0_S_1HW"}, {41, "Walking1_S_1HW"}, {42, "Walking2_S_1HW"},
    {43, "Walking3_S_1HW"}, {44, "Walking4_S_1HW"}, {45, "Walking5_S_1HW"}, {46, "Walking6_S_1HW"},
    {47, "Walking7_S_1HW"}, {48, "Walking0_S_2HW"}, {49, "Walking1_S_2HW"}, {50, "Walking2_S_2HW"},
    {51, "Walking3_S_2HW"}, {52, "Walking4_S_2HW"}, {53, "Walking5_S_2HW"}, {54, "Walking6_S_2HW"},
    {55, "Walking7_S_2HW"}, {60, "Running0_1HW"}, {61, "Running1_1HW"}, {62, "Running2_1HW"},
    {63, "Running3_1HW"}, {64, "Running4_1HW"}, {65, "Running5_1HW"}, {66, "Running6_1HW"},
    {67, "Running7_1HW"}, {68, "Running0_2HW"}, {69, "Running1_2HW"}, {70, "Running2_2HW"},
    {71, "Running3_2HW"}, {72, "Running4_2HW"}, {73, "Running5_2HW"}, {74, "Running6_2HW"},
    {75, "Running7_2HW"}, {80, "Running0_S_1HW"}, {81, "Running1_S_1HW"}, {82, "Running2_S_1HW"},
    {83, "Running3_S_1HW"}, {84, "Running4_S_1HW"}, {85, "Running5_S_1HW"}, {86, "Running6_S_1HW"},
    {87, "Running7_S_1HW"}, {88, "Running0_S_2HW"}, {89, "Running1_S_2HW"}, {90, "Running2_S_2HW"},
    {91, "Running3_S_2HW"}, {92, "Running4_S_2HW"}, {93, "Running5_S_2HW"}, {94, "Running6_S_2HW"},
    {95, "Running7_S_2HW"}, {100, "Crouched_1HW"}, {101, "Crouched_2HW"}, {102, "Crouched_SLW"},
    {103, "Crouched_SCW"}, {104, "Crouched_BIN"}, {105, "Crouched_SWTCH"}, {106, "Crouched_KNIFE"},
    {110, "Prone_1HW"}, {111, "Prone_2HW"}, {112, "Prone_SLW"}, {113, "Prone_SCW"},
    {114, "Prone_BIN"}, {115, "Prone_SWTCH"}, {116, "Prone_KNIFE"}, {120, "Crawling_CR0"},
    {121, "Crawling_CR1"}, {122, "Crawling_CR2"}, {123, "Crawling_CR3"}, {124, "Crawling_CR4"},
    {125, "Crawling_CR5"}, {126, "Crawling_CR6"}, {127, "Crawling_CR7"}, {130, "Jumping_1HW"},
    {131, "Jumping_2HW"}, {132, "Jumping_S_1HW"}, {133, "Jumping_S_2HW"}, {140, "Floating"},
    {141, "Swimming"}, {150, "F1"}, {151, "F2"}, {152, "F3"},
    {153, "F4"}, {154, "F5"}, {155, "F6"}, {156, "F7"},
    {157, "F8"}, {158, "F9"}, {159, "F10"}, {160, "F11"},
    {161, "F12"}, {170, "F1_C"}, {171, "F2_C"}, {172, "F3_C"},
    {173, "F4_C"}, {174, "F5_C"}, {175, "F6_C"}, {176, "F7_C"},
    {177, "F8_C"}, {178, "F9_C"}, {179, "F10_C"}, {180, "F11_C"},
    {181, "F12_C"}, {190, "Death_B_P01DF"}, {191, "Death_B_P01DL"}, {192, "Death_B_P01DB"},
    {193, "Death_B_P01DR"}, {194, "Death_B_P02DF"}, {195, "Death_B_P02DL"}, {196, "Death_B_P02DB"},
    {197, "Death_B_P02DR"}, {198, "Death_B_P03DF"}, {199, "Death_B_P03DL"}, {200, "Death_B_P03DB"},
    {201, "Death_B_P03DR"}, {202, "Death_B_P04DF"}, {203, "Death_B_P04DL"}, {204, "Death_B_P04DB"},
    {205, "Death_B_P04DR"}, {206, "Death_B_P05DF"}, {207, "Death_B_P05DL"}, {208, "Death_B_P05DB"},
    {209, "Death_B_P05DR"}, {210, "Death_B_P06DF"}, {211, "Death_B_P06DL"}, {212, "Death_B_P06DB"},
    {213, "Death_B_P06DR"}, {214, "Death_B_P07DF"}, {215, "Death_B_P07DL"}, {216, "Death_B_P07DB"},
    {217, "Death_B_P07DR"}, {218, "Death_B_P08DF"}, {219, "Death_B_P08DL"}, {220, "Death_B_P08DB"},
    {221, "Death_B_P08DR"}, {222, "Death_B_P09DF"}, {223, "Death_B_P09DL"}, {224, "Death_B_P09DB"},
    {225, "Death_B_P09DR"}, {226, "Death_B_P10DF"}, {227, "Death_B_P10DL"}, {228, "Death_B_P10DB"},
    {229, "Death_B_P10DR"}, {230, "Death_B_P11DF"}, {231, "Death_B_P11DL"}, {232, "Death_B_P11DB"},
    {233, "Death_B_P11DR"}, {234, "Death_B_P12DF"}, {235, "Death_B_P12DL"}, {236, "Death_B_P12DB"},
    {237, "Death_B_P12DR"}, {238, "Death_B_P13DF"}, {239, "Death_B_P13DL"}, {240, "Death_B_P13DB"},
    {241, "Death_B_P13DR"}, {242, "Death_B_P14DF"}, {243, "Death_B_P14DL"}, {244, "Death_B_P14DB"},
    {245, "Death_B_P14DR"}, {246, "Death_B_P15DF"}, {247, "Death_B_P15DL"}, {248, "Death_B_P15DB"},
    {249, "Death_B_P15DR"}, {250, "Death_G_DF"}, {251, "Death_G_DL"}, {252, "Death_G_DB"},
    {253, "Death_G_DR"}, {254, "Death_Water"}, {255, "Death_Fire"},
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

} // namespace df
