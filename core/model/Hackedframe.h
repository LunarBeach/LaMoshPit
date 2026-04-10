#pragma once
#include "HackedMacroblock.h"
#include <vector>
#include <cstdint>

struct HackedFrame {
    int frameIndex = 0;
    int sliceType = 2;           // 0 = P, 1 = B, 2 = I
    bool isIDR = false;

    size_t nalStartOffset = 0;   // For destructive in-place patching
    size_t nalSize = 0;

    std::vector<HackedMacroblock> macroblocks;

    // P/B specific controls (will be expanded)
    int mvScale = 0;
    int refIndex = 0;
    bool bidirectional = false;  // For B-frames
};