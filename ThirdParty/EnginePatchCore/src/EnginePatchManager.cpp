#include "EnginePatchManager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/stat.h>
#endif

namespace fs = std::filesystem;

std::string EnginePatchManager::Trim(const std::string& str)
{
    size_t start = 0;
    while (start < str.length() && std::isspace(static_cast<unsigned char>(str[start]))) {
        ++start;
    }
    if (start == str.length()) {
        return "";
    }
    size_t end = str.length() - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(str[end]))) {
        --end;
    }
    return str.substr(start, end - start + 1);
}

void EnginePatchManager::MakeWritable(const std::string& filePath)
{
#ifdef _WIN32
    DWORD dwAttrs = GetFileAttributesA(filePath.c_str());
    if (dwAttrs != INVALID_FILE_ATTRIBUTES && (dwAttrs & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesA(filePath.c_str(), dwAttrs & ~FILE_ATTRIBUTE_READONLY);
    }
#else
    chmod(filePath.c_str(), 0644);
#endif
}

std::string EnginePatchManager::MakeBeginMarker(const std::string& patchId, const std::string& opId)
{
    return "// @@PATCH_BEGIN(" + patchId + "::" + opId + ")";
}

std::string EnginePatchManager::MakeEndMarker(const std::string& patchId, const std::string& opId)
{
    return "// @@PATCH_END(" + patchId + "::" + opId + ")";
}

std::string EnginePatchManager::ResolveEnginePath(const std::string& engineDir, const std::string& relPath)
{
    std::string result = engineDir;
    if (!result.empty() && result.back() != '/' && result.back() != '\\') {
        result += '/';
    }
    result += "Source/";
    result += relPath;
    return result;
}

bool EnginePatchManager::IsOperationApplied(const std::string& filePath, const std::string& patchId, const std::string& opId)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::string beginMarker = MakeBeginMarker(patchId, opId);
    std::string line;
    while (std::getline(file, line)) {
        // Strip \r if present (for Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find(beginMarker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

PatchStatus EnginePatchManager::GetPatchStatus(
    const EnginePatch& patch,
    const std::string& engineDir,
    const std::string& engineVersion)
{
    // Find matching version
    const PatchVersion* matched = nullptr;
    for (const auto& version : patch.versions) {
        for (const auto& ev : version.engineVersions) {
            if (ev == engineVersion) {
                matched = &version;
                break;
            }
        }
        if (matched) break;
    }

    if (!matched) {
        return PatchStatus::NotApplicable;
    }

    int totalOps = 0;
    int appliedOps = 0;

    for (const auto& pfile : matched->files) {
        std::string fullPath = ResolveEnginePath(engineDir, pfile.file);
        for (const auto& op : pfile.operations) {
            ++totalOps;
            if (IsOperationApplied(fullPath, patch.patchId, op.id)) {
                ++appliedOps;
            }
        }
    }

    if (totalOps == 0) {
        return PatchStatus::NotApplied;
    }
    if (appliedOps == totalOps) {
        return PatchStatus::Applied;
    }
    if (appliedOps == 0) {
        return PatchStatus::NotApplied;
    }
    return PatchStatus::Error;
}

bool EnginePatchManager::ApplyOperation(
    std::vector<std::string>& lines,
    int& lineOffset,
    const std::string& patchId,
    const PatchOperation& op,
    std::string& outError)
{
    std::string beginMarker = MakeBeginMarker(patchId, op.id);
    std::string endMarker = MakeEndMarker(patchId, op.id);

    // Check if already applied
    for (const auto& line : lines) {
        if (line.find(beginMarker) != std::string::npos) {
            return true;  // Already applied, idempotent
        }
    }

    int adjustedLine = (op.line - 1) + lineOffset;

    // Validate the remove lines match
    if (adjustedLine < 0 || adjustedLine + static_cast<int>(op.remove.size()) > static_cast<int>(lines.size())) {
        outError = "Line range out of bounds for operation " + op.id;
        return false;
    }

    // Check that remove lines match
    for (size_t i = 0; i < op.remove.size(); ++i) {
        std::string expectedTrimmed = Trim(op.remove[i]);
        std::string actualTrimmed = Trim(lines[adjustedLine + i]);
        if (expectedTrimmed != actualTrimmed) {
            outError = "Remove line mismatch at line " + std::to_string(adjustedLine + i) +
                       " for operation " + op.id + ". Expected: '" + expectedTrimmed +
                       "' but got: '" + actualTrimmed + "'";
            return false;
        }
    }

    // Build the patch block
    std::vector<std::string> block;
    block.push_back(beginMarker);
    for (const auto& removedLine : op.remove) {
        block.push_back("@@REMOVED: " + removedLine);
    }
    for (const auto& addedLine : op.add) {
        block.push_back(addedLine);
    }
    block.push_back(endMarker);

    // Remove old lines and insert block
    lines.erase(lines.begin() + adjustedLine, lines.begin() + adjustedLine + static_cast<int>(op.remove.size()));
    lines.insert(lines.begin() + adjustedLine, block.begin(), block.end());

    // Update offset for next operation
    lineOffset += static_cast<int>(block.size()) - static_cast<int>(op.remove.size());

    return true;
}

bool EnginePatchManager::ApplyPatch(
    const EnginePatch& patch,
    const std::string& engineDir,
    const std::string& engineVersion,
    std::string& outError)
{
    // Find matching version
    const PatchVersion* matched = nullptr;
    for (const auto& version : patch.versions) {
        for (const auto& ev : version.engineVersions) {
            if (ev == engineVersion) {
                matched = &version;
                break;
            }
        }
        if (matched) break;
    }

    if (!matched) {
        return true;  // Not applicable, not an error
    }

    // Apply to each file
    for (const auto& pfile : matched->files) {
        std::string fullPath = ResolveEnginePath(engineDir, pfile.file);

        // Read file
        std::vector<std::string> lines;
        std::ifstream infile(fullPath, std::ios::binary);
        if (!infile.is_open()) {
            outError = "Failed to open file: " + fullPath;
            return false;
        }
        std::string line;
        while (std::getline(infile, line)) {
            // Strip \r if present (for Windows line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
        infile.close();

        // Apply operations
        int lineOffset = 0;
        for (const auto& op : pfile.operations) {
            if (!ApplyOperation(lines, lineOffset, patch.patchId, op, outError)) {
                return false;
            }
        }

        // Write file
        MakeWritable(fullPath);
        std::ofstream outfile(fullPath, std::ios::binary);
        if (!outfile.is_open()) {
            outError = "Failed to write file: " + fullPath;
            return false;
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            outfile << lines[i];
            if (i < lines.size() - 1) {
                outfile << '\n';
            }
        }
        outfile.close();
    }

    return true;
}

bool EnginePatchManager::UnpatchOperation(
    const std::string& filePath,
    const std::string& patchId,
    const PatchOperation& op,
    std::string& outError)
{
    if (!IsOperationApplied(filePath, patchId, op.id)) {
        return true;  // Not applied, nothing to do
    }

    std::string beginMarker = MakeBeginMarker(patchId, op.id);
    std::string endMarker = MakeEndMarker(patchId, op.id);

    // Read file
    std::vector<std::string> lines;
    std::ifstream infile(filePath, std::ios::binary);
    if (!infile.is_open()) {
        outError = "Failed to open file: " + filePath;
        return false;
    }
    std::string line;
    while (std::getline(infile, line)) {
        // Strip \r if present (for Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    infile.close();

    // Find BEGIN and END markers
    int beginIdx = -1;
    int endIdx = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(beginMarker) != std::string::npos) {
            beginIdx = i;
        }
        if (lines[i].find(endMarker) != std::string::npos) {
            endIdx = i;
        }
    }

    if (beginIdx == -1 || endIdx == -1 || beginIdx >= endIdx) {
        outError = "Could not find patch markers for operation " + op.id;
        return false;
    }

    // Collect @@REMOVED: lines
    std::vector<std::string> restoredLines;
    for (int i = beginIdx + 1; i < endIdx; ++i) {
        const std::string& currentLine = lines[i];
        if (currentLine.substr(0, 11) == "@@REMOVED: ") {
            restoredLines.push_back(currentLine.substr(11));
        }
    }

    // Remove the block and reinsert restored lines
    lines.erase(lines.begin() + beginIdx, lines.begin() + endIdx + 1);
    lines.insert(lines.begin() + beginIdx, restoredLines.begin(), restoredLines.end());

    // Write file
    MakeWritable(filePath);
    std::ofstream outfile(filePath, std::ios::binary);
    if (!outfile.is_open()) {
        outError = "Failed to write file: " + filePath;
        return false;
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        outfile << lines[i];
        if (i < lines.size() - 1) {
            outfile << '\n';
        }
    }
    outfile.close();

    return true;
}

bool EnginePatchManager::UnpatchPatch(
    const EnginePatch& patch,
    const std::string& engineDir,
    const std::string& engineVersion,
    std::string& outError)
{
    // Find matching version
    const PatchVersion* matched = nullptr;
    for (const auto& version : patch.versions) {
        for (const auto& ev : version.engineVersions) {
            if (ev == engineVersion) {
                matched = &version;
                break;
            }
        }
        if (matched) break;
    }

    if (!matched) {
        return true;  // Not applicable, not an error
    }

    // Unpatch each file
    for (const auto& pfile : matched->files) {
        std::string fullPath = ResolveEnginePath(engineDir, pfile.file);

        // Process operations in reverse order
        for (auto it = pfile.operations.rbegin(); it != pfile.operations.rend(); ++it) {
            if (!UnpatchOperation(fullPath, patch.patchId, *it, outError)) {
                return false;
            }
        }
    }

    return true;
}

// Free function implementation
void SyncPatches(
    const std::vector<EnginePatch>& patches,
    const std::map<std::string, bool>& pluginEnabled,
    const std::string& engineDir,
    const std::string& engineVersion,
    std::ostream& log)
{
    for (const auto& patch : patches) {
        bool enabled = true;
        if (!patch.plugin.empty()) {
            auto it = pluginEnabled.find(patch.plugin);
            enabled = (it != pluginEnabled.end()) ? it->second : false;
        }

        PatchStatus status = EnginePatchManager::GetPatchStatus(patch, engineDir, engineVersion);
        std::string err;

        if (enabled && status == PatchStatus::NotApplied) {
            bool ok = EnginePatchManager::ApplyPatch(patch, engineDir, engineVersion, err);
            log << "[APPLY] " << patch.patchId << (ok ? " OK" : " FAILED: " + err) << "\n";
        } else if (!enabled && status == PatchStatus::Applied) {
            bool ok = EnginePatchManager::UnpatchPatch(patch, engineDir, engineVersion, err);
            log << "[UNPATCH] " << patch.patchId << (ok ? " OK" : " FAILED: " + err) << "\n";
        } else {
            log << "[SKIP] " << patch.patchId << " (status=" << static_cast<int>(status) << " enabled=" << (enabled ? "true" : "false") << ")\n";
        }
    }
}
