#pragma once

#include <string>
#include <vector>
#include <map>
#include <ostream>

enum class PatchStatus {
    Unknown,
    Applied,
    NotApplied,
    NotApplicable,
    Error
};

struct PatchOperation {
    std::string id;
    int line = 0;
    std::vector<std::string> remove;
    std::vector<std::string> add;
};

struct PatchFile {
    std::string file;  // relative to Engine/Source/
    std::vector<PatchOperation> operations;
};

struct PatchVersion {
    std::vector<std::string> engineVersions;
    std::vector<PatchFile> files;
};

struct EnginePatch {
    std::string patchId;
    std::string description;
    std::string plugin;   // empty = always apply
    std::vector<PatchVersion> versions;
};

// Forward declaration
class EnginePatchManager;

// Sync all patches: apply if plugin enabled+NotApplied, unapply if disabled+Applied
void SyncPatches(
    const std::vector<EnginePatch>& patches,
    const std::map<std::string, bool>& pluginEnabled,   // pluginName -> enabled
    const std::string& engineDir,
    const std::string& engineVersion,
    std::ostream& log);
