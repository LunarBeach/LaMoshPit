#pragma once
#include <vector>
#include <string>

class BitstreamEditor {
public:
    bool loadFile(const std::string& filename, std::vector<uint8_t>& buffer);
    void changeSliceType(size_t nalOffset, int newSliceType);  // Core destructive function
    // TODO: deleteFrame, copyFrame, pasteFrame, modifyMacroblock...
};