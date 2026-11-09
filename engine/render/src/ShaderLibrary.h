#pragma once

#include "ember/core/Types.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ember::render {

// Loads compiled SPIR-V by shader name ("mesh" -> <binaryDir>/mesh.spv) and supports hot reload:
// poll() notices edited .slang sources, recompiles them with slangc and reports which ones changed.
// A change to a shared include (shaders/core/) recompiles every shader.
class ShaderLibrary {
public:
    ShaderLibrary(std::filesystem::path binaryDir, std::filesystem::path sourceDir);

    // The SPIR-V words for a shader, or an empty vector if it couldn't be loaded.
    const std::vector<u32>& get(const std::string& name);

    // Recompiles shaders whose sources changed since the last poll. Returns the names that compiled
    // successfully (their pipelines should be rebuilt). Compile errors go to lastError().
    std::vector<std::string> poll();
    const std::string& lastError() const { return m_lastError; }

private:
    using Time = std::filesystem::file_time_type;
    Time newestIncludeTime() const;
    bool compile(const std::string& name);

    std::filesystem::path m_binaryDir;
    std::filesystem::path m_sourceDir;
    std::unordered_map<std::string, std::vector<u32>> m_cache;
    std::unordered_map<std::string, Time> m_sourceTimes;
    Time m_includeTime{};
    std::string m_lastError;
};

} // namespace ember::render
