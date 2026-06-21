#include "EnginePatchFileLoader.h"
#include "nlohmann/json.hpp"

#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::vector<EnginePatch> EnginePatchFileLoader::LoadPatchesFromDirectory(const std::string& directory)
{
    std::vector<EnginePatch> patches;

    try {
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            return patches;
        }

        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::ifstream file(entry.path(), std::ios::binary);
                if (!file.is_open()) {
                    continue;
                }

                std::stringstream buffer;
                buffer << file.rdbuf();
                file.close();

                EnginePatch patch;
                std::string error;
                if (ParsePatchJson(buffer.str(), patch, error)) {
                    patches.push_back(patch);
                }
            }
        }
    } catch (...) {
        // Silently ignore filesystem errors
    }

    return patches;
}

bool EnginePatchFileLoader::ParsePatchJson(
    const std::string& content,
    EnginePatch& outPatch,
    std::string& outError)
{
    try {
        json j = json::parse(content);

        // Parse top-level fields
        if (!j.contains("patchId") || !j["patchId"].is_string()) {
            outError = "Missing or invalid 'patchId' field";
            return false;
        }
        outPatch.patchId = j["patchId"].get<std::string>();

        if (j.contains("description") && j["description"].is_string()) {
            outPatch.description = j["description"].get<std::string>();
        }

        if (j.contains("plugin") && j["plugin"].is_string()) {
            outPatch.plugin = j["plugin"].get<std::string>();
        }

        // Parse versions array
        if (!j.contains("versions") || !j["versions"].is_array()) {
            outError = "Missing or invalid 'versions' field";
            return false;
        }

        for (const auto& versionObj : j["versions"]) {
            PatchVersion version;

            // Parse engineVersions (array or legacy string)
            if (versionObj.contains("engineVersions")) {
                if (versionObj["engineVersions"].is_array()) {
                    for (const auto& ev : versionObj["engineVersions"]) {
                        if (ev.is_string()) {
                            version.engineVersions.push_back(ev.get<std::string>());
                        }
                    }
                } else if (versionObj["engineVersions"].is_string()) {
                    version.engineVersions.push_back(versionObj["engineVersions"].get<std::string>());
                }
            } else if (versionObj.contains("engineVersion") && versionObj["engineVersion"].is_string()) {
                // Legacy format support
                version.engineVersions.push_back(versionObj["engineVersion"].get<std::string>());
            }

            // Parse files array
            if (versionObj.contains("files") && versionObj["files"].is_array()) {
                for (const auto& fileObj : versionObj["files"]) {
                    PatchFile pfile;

                    if (!fileObj.contains("file") || !fileObj["file"].is_string()) {
                        continue;  // Skip invalid file entries
                    }
                    pfile.file = fileObj["file"].get<std::string>();

                    // Parse operations array
                    if (fileObj.contains("operations") && fileObj["operations"].is_array()) {
                        for (const auto& opObj : fileObj["operations"]) {
                            PatchOperation op;

                            if (!opObj.contains("id") || !opObj["id"].is_string()) {
                                continue;  // Skip invalid operation entries
                            }
                            op.id = opObj["id"].get<std::string>();

                            if (opObj.contains("line") && opObj["line"].is_number()) {
                                op.line = opObj["line"].get<int>();
                            }

                            // Parse remove array
                            if (opObj.contains("remove") && opObj["remove"].is_array()) {
                                for (const auto& removeLine : opObj["remove"]) {
                                    if (removeLine.is_string()) {
                                        op.remove.push_back(removeLine.get<std::string>());
                                    }
                                }
                            }

                            // Parse add array
                            if (opObj.contains("add") && opObj["add"].is_array()) {
                                for (const auto& addLine : opObj["add"]) {
                                    if (addLine.is_string()) {
                                        op.add.push_back(addLine.get<std::string>());
                                    }
                                }
                            }

                            pfile.operations.push_back(op);
                        }
                    }

                    version.files.push_back(pfile);
                }
            }

            outPatch.versions.push_back(version);
        }

        return true;
    } catch (const std::exception& e) {
        outError = std::string("JSON parse error: ") + e.what();
        return false;
    }
}
