#pragma once

#include "EnginePatchTypes.h"
#include <string>
#include <vector>

class EnginePatchFileLoader {
public:
    static std::vector<EnginePatch> LoadPatchesFromDirectory(const std::string& directory);

private:
    static bool ParsePatchJson(const std::string& content, EnginePatch& outPatch, std::string& outError);
};
