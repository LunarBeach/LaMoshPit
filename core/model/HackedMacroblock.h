#pragma once
#include <cstdint>

struct HackedMacroblock {
    int mbX = 0;
    int mbY = 0;
    int mbType = 0;
    int16_t mvX = 0;
    int16_t mvY = 0;
    bool forceIntra = false;
    bool zeroResiduals = false;
};