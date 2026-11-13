// DF.EXE's move numbers and names (0x4e585c).
#pragma once

#include <string>

namespace df {

// Name of a move number (e.g. 190 -> "Death_B_P01DF"); null if none.
const char* moveName(int number);
// Number of a move name; -1 if none.
int moveNumber(const std::string& name);
// Perception factor of a soldier in this move (+0x28) and the aim factor
// against them (+0x2C); 1 if unknown.
float moveVisibility(int number);
float moveAimFactor(int number);

} // namespace df
