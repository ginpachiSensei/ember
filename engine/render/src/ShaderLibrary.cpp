#include "ShaderLibrary.h"

#include "ember/core/Log.h"
#include "ember/core/Profile.h"

#include <cstdio>
#include <fstream>

namespace ember::render {

namespace fs = std::filesystem;

namespace {

std::vector<u32> readSpirv(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const auto size = static_cast<usize>(file.tellg());
    if (size == 0 || size % 4 != 0) {
        return {};
    }
    std::vector<u32> words(size / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));
    return words;
}

fs::file_time_type writeTime(const fs::path& path)
{
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    return ec ? fs::file_time_type{} : time;
}

} // namespace

ShaderLibrary::ShaderLibrary(fs::path binaryDir, fs::path sourceDir)
    : m_binaryDir(std::move(binaryDir))
    , m_sourceDir(std::move(sourceDir))
{
    // Remember the current source times: the build already compiled these versions.
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(m_sourceDir, ec)) {
        if (entry.path().extension() == ".slang") {
            m_sourceTimes[entry.path().stem().string()] = writeTime(entry.path());
        }
    }
    m_includeTime = newestIncludeTime();
}

const std::vector<u32>& ShaderLibrary::get(const std::string& name)
{
    auto it = m_cache.find(name);
    if (it == m_cache.end()) {
        std::vector<u32> words = readSpirv(m_binaryDir / (name + ".spv"));
        if (words.empty()) {
            EMBER_LOG_ERROR("Shader '{}' not found in {} (did the ember_shaders target build?)", name,
                            m_binaryDir.string());
        }
        it = m_cache.emplace(name, std::move(words)).first;
    }
    return it->second;
}

ShaderLibrary::Time ShaderLibrary::newestIncludeTime() const
{
    Time newest{};
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(m_sourceDir / "core", ec)) {
        if (entry.path().extension() == ".slang") {
            newest = std::max(newest, writeTime(entry.path()));
        }
    }
    return newest;
}

bool ShaderLibrary::compile(const std::string& name)
{
#if defined(EMBER_SLANGC_PATH)
    EMBER_PROFILE_FUNCTION();
    const fs::path source = m_sourceDir / (name + ".slang");
    const fs::path output = m_binaryDir / (name + ".spv");
    const fs::path temp = m_binaryDir / (name + ".spv.tmp");

    // Compile to a temp file first, so a failed compile never replaces a working shader.
    const std::string command = std::format(R"(""{}" "{}" {} -I "{}" -o "{}" 2>&1")", EMBER_SLANGC_PATH,
                                            source.string(), EMBER_SLANG_FLAGS, m_sourceDir.string(), temp.string());
    std::string output_text;
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) {
        m_lastError = "could not start slangc";
        return false;
    }
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe)) {
        output_text += buffer;
    }
    const int exitCode = _pclose(pipe);
    if (exitCode != 0) {
        m_lastError = std::format("{}.slang:\n{}", name, output_text);
        EMBER_LOG_ERROR("Shader compile failed: {}", m_lastError);
        return false;
    }

    std::error_code ec;
    fs::rename(temp, output, ec);
    if (ec) {
        m_lastError = std::format("could not replace {}: {}", output.string(), ec.message());
        return false;
    }
    m_cache.erase(name);
    EMBER_LOG_INFO("Recompiled shader {}", name);
    return true;
#else
    (void)name;
    m_lastError = "hot reload unavailable: built without EMBER_SLANGC_PATH";
    return false;
#endif
}

std::vector<std::string> ShaderLibrary::poll()
{
    std::vector<std::string> changed;
    const Time includeTime = newestIncludeTime();
    const bool includesChanged = includeTime != m_includeTime;
    m_includeTime = includeTime;

    bool anyFailed = false;
    bool anyCompiled = false;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(m_sourceDir, ec)) {
        if (entry.path().extension() != ".slang") {
            continue;
        }
        const std::string name = entry.path().stem().string();
        const Time time = writeTime(entry.path());
        Time& known = m_sourceTimes[name];
        if (time == known && !includesChanged) {
            continue;
        }
        known = time;
        anyCompiled = true;
        if (compile(name)) {
            changed.push_back(name);
        } else {
            anyFailed = true;
        }
    }
    if (anyCompiled && !anyFailed) {
        m_lastError.clear();
    }
    return changed;
}

} // namespace ember::render
