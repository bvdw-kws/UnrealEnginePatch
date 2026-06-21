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

// ---------------------------------------------------------------------------
// File I/O helpers — handle UTF-16 LE (BOM: FF FE) transparently.
// UE engine source headers are frequently UTF-16 LE on Windows.
// We transcode to UTF-8 for all string operations, then write back in the
// original encoding so the compiler still sees a valid file.
// ---------------------------------------------------------------------------

static bool ReadFileLines(const std::string& filePath,
                          std::vector<std::string>& lines,
                          bool& outWasUtf16)
{
    outWasUtf16 = false;
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    // Peek at first two bytes to detect UTF-16 LE BOM (FF FE)
    unsigned char bom[2] = {};
    file.read(reinterpret_cast<char*>(bom), 2);

    if (bom[0] == 0xFF && bom[1] == 0xFE)
    {
        outWasUtf16 = true;
#ifdef _WIN32
        // Read remaining content as UTF-16 LE
        std::vector<char> rawBytes((std::istreambuf_iterator<char>(file)), {});
        file.close();

        // rawBytes.size() must be even; if odd, drop the trailing byte
        size_t wcharCount = rawBytes.size() / 2;
        const wchar_t* wdata = reinterpret_cast<const wchar_t*>(rawBytes.data());

        // Convert UTF-16 → UTF-8
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wdata, static_cast<int>(wcharCount),
                                          nullptr, 0, nullptr, nullptr);
        std::string utf8(utf8Len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wdata, static_cast<int>(wcharCount),
                            utf8.data(), utf8Len, nullptr, nullptr);

        std::istringstream ss(utf8);
        std::string line;
        while (std::getline(ss, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
#else
        // Non-Windows: no WinAPI — skip UTF-16 files (shouldn't occur outside Windows)
        file.close();
        return false;
#endif
    }
    else
    {
        // UTF-8 or plain ASCII — rewind and read normally
        file.seekg(0);
        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        file.close();
    }
    return true;
}

static bool WriteFileLines(const std::string& filePath,
                           const std::vector<std::string>& lines,
                           bool wasUtf16)
{
    std::ofstream outfile(filePath, std::ios::binary);
    if (!outfile.is_open()) return false;

    if (wasUtf16)
    {
#ifdef _WIN32
        // Write UTF-16 LE BOM
        const unsigned char bom[2] = {0xFF, 0xFE};
        outfile.write(reinterpret_cast<const char*>(bom), 2);

        // Join lines with \r\n (UE UTF-16 files use Windows line endings)
        std::string utf8;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            utf8 += lines[i];
            if (i + 1 < lines.size()) utf8 += "\r\n";
        }

        // Convert UTF-8 → UTF-16 LE
        int wcharCount = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                              static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring wstr(wcharCount, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                            wstr.data(), wcharCount);

        outfile.write(reinterpret_cast<const char*>(wstr.data()), wcharCount * 2);
#endif
    }
    else
    {
        for (size_t i = 0; i < lines.size(); ++i)
        {
            outfile << lines[i];
            if (i + 1 < lines.size()) outfile << '\n';
        }
    }
    outfile.close();
    return true;
}

// ---------------------------------------------------------------------------

std::string EnginePatchManager::Trim(const std::string& str)
{
    size_t start = 0;
    while (start < str.length() && std::isspace(static_cast<unsigned char>(str[start])))
        ++start;
    if (start == str.length()) return "";
    size_t end = str.length() - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(str[end])))
        --end;
    return str.substr(start, end - start + 1);
}

void EnginePatchManager::MakeWritable(const std::string& filePath)
{
#ifdef _WIN32
    DWORD dwAttrs = GetFileAttributesA(filePath.c_str());
    if (dwAttrs != INVALID_FILE_ATTRIBUTES && (dwAttrs & FILE_ATTRIBUTE_READONLY))
        SetFileAttributesA(filePath.c_str(), dwAttrs & ~FILE_ATTRIBUTE_READONLY);
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
    if (!result.empty() && result.back() != '/' && result.back() != '\\')
        result += '/';
    result += "Source/";
    result += relPath;
    return result;
}

bool EnginePatchManager::IsOperationApplied(const std::string& filePath,
                                             const std::string& patchId,
                                             const std::string& opId)
{
    std::vector<std::string> lines;
    bool wasUtf16 = false;
    if (!ReadFileLines(filePath, lines, wasUtf16)) return false;

    std::string beginMarker = MakeBeginMarker(patchId, opId);
    for (const auto& line : lines)
    {
        if (line.find(beginMarker) != std::string::npos) return true;
    }
    return false;
}

PatchStatus EnginePatchManager::GetPatchStatus(const EnginePatch& patch,
                                                const std::string& engineDir,
                                                const std::string& engineVersion)
{
    const PatchVersion* matched = nullptr;
    for (const auto& version : patch.versions)
    {
        for (const auto& ev : version.engineVersions)
        {
            if (ev == engineVersion) { matched = &version; break; }
        }
        if (matched) break;
    }
    if (!matched) return PatchStatus::NotApplicable;

    int totalOps = 0, appliedOps = 0;
    for (const auto& pfile : matched->files)
    {
        std::string fullPath = ResolveEnginePath(engineDir, pfile.file);
        for (const auto& op : pfile.operations)
        {
            ++totalOps;
            if (IsOperationApplied(fullPath, patch.patchId, op.id)) ++appliedOps;
        }
    }

    if (totalOps == 0)            return PatchStatus::NotApplied;
    if (appliedOps == totalOps)   return PatchStatus::Applied;
    if (appliedOps == 0)          return PatchStatus::NotApplied;
    return PatchStatus::Error;
}

bool EnginePatchManager::ApplyOperation(std::vector<std::string>& lines,
                                         int& lineOffset,
                                         const std::string& patchId,
                                         const PatchOperation& op,
                                         std::string& outError)
{
    std::string beginMarker = MakeBeginMarker(patchId, op.id);
    std::string endMarker   = MakeEndMarker(patchId, op.id);

    for (const auto& line : lines)
    {
        if (line.find(beginMarker) != std::string::npos) return true; // already applied
    }

    int adjustedLine = (op.line - 1) + lineOffset;
    if (adjustedLine < 0 || adjustedLine + static_cast<int>(op.remove.size()) > static_cast<int>(lines.size()))
    {
        outError = "Line range out of bounds for operation " + op.id;
        return false;
    }

    for (size_t i = 0; i < op.remove.size(); ++i)
    {
        std::string expectedTrimmed = Trim(op.remove[i]);
        std::string actualTrimmed   = Trim(lines[adjustedLine + i]);
        if (expectedTrimmed != actualTrimmed)
        {
            outError = "Remove line mismatch at line " + std::to_string(adjustedLine + i) +
                       " for operation " + op.id +
                       ". Expected: '" + expectedTrimmed +
                       "' but got: '" + actualTrimmed + "'";
            return false;
        }
    }

    std::vector<std::string> block;
    block.push_back(beginMarker);
    for (const auto& removedLine : op.remove)
        block.push_back("// @@REMOVED: " + removedLine);
    for (const auto& addedLine : op.add)
        block.push_back(addedLine);
    block.push_back(endMarker);

    lines.erase(lines.begin() + adjustedLine,
                lines.begin() + adjustedLine + static_cast<int>(op.remove.size()));
    lines.insert(lines.begin() + adjustedLine, block.begin(), block.end());

    lineOffset += static_cast<int>(block.size()) - static_cast<int>(op.remove.size());
    return true;
}

bool EnginePatchManager::ApplyPatch(const EnginePatch& patch,
                                     const std::string& engineDir,
                                     const std::string& engineVersion,
                                     std::string& outError)
{
    const PatchVersion* matched = nullptr;
    for (const auto& version : patch.versions)
    {
        for (const auto& ev : version.engineVersions)
        {
            if (ev == engineVersion) { matched = &version; break; }
        }
        if (matched) break;
    }
    if (!matched) return true;

    for (const auto& pfile : matched->files)
    {
        std::string fullPath = ResolveEnginePath(engineDir, pfile.file);

        std::vector<std::string> lines;
        bool wasUtf16 = false;
        if (!ReadFileLines(fullPath, lines, wasUtf16))
        {
            outError = "Failed to open file: " + fullPath;
            return false;
        }

        int lineOffset = 0;
        for (const auto& op : pfile.operations)
        {
            if (!ApplyOperation(lines, lineOffset, patch.patchId, op, outError))
                return false;
        }

        MakeWritable(fullPath);
        if (!WriteFileLines(fullPath, lines, wasUtf16))
        {
            outError = "Failed to write file: " + fullPath;
            return false;
        }
    }
    return true;
}

bool EnginePatchManager::UnpatchOperation(const std::string& filePath,
                                           const std::string& patchId,
                                           const PatchOperation& op,
                                           std::string& outError)
{
    if (!IsOperationApplied(filePath, patchId, op.id)) return true;

    std::string beginMarker = MakeBeginMarker(patchId, op.id);
    std::string endMarker   = MakeEndMarker(patchId, op.id);

    std::vector<std::string> lines;
    bool wasUtf16 = false;
    if (!ReadFileLines(filePath, lines, wasUtf16))
    {
        outError = "Failed to open file: " + filePath;
        return false;
    }

    int beginIdx = -1, endIdx = -1;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].find(beginMarker) != std::string::npos) beginIdx = static_cast<int>(i);
        if (lines[i].find(endMarker)   != std::string::npos) endIdx   = static_cast<int>(i);
    }

    if (beginIdx == -1 || endIdx == -1 || beginIdx >= endIdx)
    {
        outError = "Could not find patch markers for operation " + op.id;
        return false;
    }

    static const std::string kRemovedPrefix = "// @@REMOVED: ";
    std::vector<std::string> restoredLines;
    for (int i = beginIdx + 1; i < endIdx; ++i)
    {
        const std::string& cur = lines[i];
        if (cur.substr(0, kRemovedPrefix.size()) == kRemovedPrefix)
            restoredLines.push_back(cur.substr(kRemovedPrefix.size()));
    }

    lines.erase(lines.begin() + beginIdx, lines.begin() + endIdx + 1);
    lines.insert(lines.begin() + beginIdx, restoredLines.begin(), restoredLines.end());

    MakeWritable(filePath);
    if (!WriteFileLines(filePath, lines, wasUtf16))
    {
        outError = "Failed to write file: " + filePath;
        return false;
    }
    return true;
}

bool EnginePatchManager::UnpatchPatch(const EnginePatch& patch,
                                       const std::string& engineDir,
                                       const std::string& engineVersion,
                                       std::string& outError)
{
    const PatchVersion* matched = nullptr;
    for (const auto& version : patch.versions)
    {
        for (const auto& ev : version.engineVersions)
        {
            if (ev == engineVersion) { matched = &version; break; }
        }
        if (matched) break;
    }
    if (!matched) return true;

    for (const auto& pfile : matched->files)
    {
        std::string fullPath = ResolveEnginePath(engineDir, pfile.file);
        for (auto it = pfile.operations.rbegin(); it != pfile.operations.rend(); ++it)
        {
            if (!UnpatchOperation(fullPath, patch.patchId, *it, outError))
                return false;
        }
    }
    return true;
}

void SyncPatches(
    const std::vector<EnginePatch>& patches,
    const std::map<std::string, bool>& pluginEnabled,
    const std::string& engineDir,
    const std::string& engineVersion,
    std::ostream& log,
    bool reapply)
{
    if (reapply)
    {
        for (const auto& patch : patches)
        {
            PatchStatus status = EnginePatchManager::GetPatchStatus(patch, engineDir, engineVersion);
            if (status == PatchStatus::Applied || status == PatchStatus::Error)
            {
                std::string err;
                bool ok = EnginePatchManager::UnpatchPatch(patch, engineDir, engineVersion, err);
                log << "[REAPPLY-UNPATCH] " << patch.patchId << (ok ? " OK" : " FAILED: " + err) << "\n";
            }
        }
    }

    for (const auto& patch : patches)
    {
        bool enabled = true;
        if (!patch.plugin.empty())
        {
            auto it = pluginEnabled.find(patch.plugin);
            enabled = (it != pluginEnabled.end()) ? it->second : false;
        }

        PatchStatus status = EnginePatchManager::GetPatchStatus(patch, engineDir, engineVersion);
        std::string err;

        if (enabled && status == PatchStatus::NotApplied)
        {
            bool ok = EnginePatchManager::ApplyPatch(patch, engineDir, engineVersion, err);
            log << "[APPLY] " << patch.patchId << (ok ? " OK" : " FAILED: " + err) << "\n";
        }
        else if (!enabled && status == PatchStatus::Applied)
        {
            bool ok = EnginePatchManager::UnpatchPatch(patch, engineDir, engineVersion, err);
            log << "[UNPATCH] " << patch.patchId << (ok ? " OK" : " FAILED: " + err) << "\n";
        }
        else
        {
            log << "[SKIP] " << patch.patchId
                << " (status=" << static_cast<int>(status)
                << " enabled=" << (enabled ? "true" : "false") << ")\n";
        }
    }
}