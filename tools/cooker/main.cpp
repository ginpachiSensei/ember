// ember_cooker: turns source assets (glTF, HDR) into the engine's cooked binary formats.
//
//   ember_cooker --content <dir> --out <dir> [--file <relative path>] [--force]
//
// For every supported file under --content it:
//   1. makes sure a .meta file with a permanent Guid sits next to it (creating one if needed),
//   2. hashes the source; if the manifest says that exact content was already cooked, skips it,
//   3. otherwise imports it, precomputes everything the runtime would otherwise do (mip chains, ...)
//      and writes <out>/<guid>.<ext>,
//   4. writes <out>/manifest.json.
// Files are cooked in parallel on the job system.

#include "ember/asset/AssetDatabase.h"
#include "ember/asset/Cooked.h"
#include "ember/core/Jobs.h"
#include "ember/core/Log.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

using namespace ember;
using namespace ember::asset;
namespace fs = std::filesystem;

namespace {

// Bump when the cooked output changes, so everything re-cooks once.
constexpr u32 kCookerVersion = 1;

struct Task {
    fs::path source;      // absolute
    std::string relative; // relative to the content dir, '/' separators
    AssetMeta meta;
    std::string hash;
    bool cooked = false;
    bool failed = false;
};

bool cook(const Task& task, const fs::path& outDir)
{
    const fs::path output = outDir / (task.meta.guid.toString() + cookedExtension(task.meta.type));
    if (task.meta.type == AssetType::Model) {
        std::optional<Model> model = loadGltf(task.source);
        if (!model) {
            return false;
        }
        generateMipChains(*model);
        return writeCookedModel(output, *model);
    }
    if (task.meta.type == AssetType::Environment) {
        std::optional<HdrImage> image = loadHdr(task.source);
        return image && writeCookedEnvironment(output, *image);
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    fs::path contentDir;
    fs::path outDir;
    std::string onlyFile;
    bool force = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--content" && i + 1 < argc) {
            contentDir = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else if (arg == "--file" && i + 1 < argc) {
            onlyFile = argv[++i];
        } else if (arg == "--force") {
            force = true;
        } else {
            EMBER_LOG_ERROR("Unknown argument '{}'", arg);
            return 2;
        }
    }
    if (contentDir.empty() || outDir.empty()) {
        EMBER_LOG_ERROR("Usage: ember_cooker --content <dir> --out <dir> [--file <relative path>] [--force]");
        return 2;
    }
    const auto start = std::chrono::steady_clock::now();
    std::error_code ec;
    fs::create_directories(outDir, ec);

    // 1. Find sources and make sure each has a meta file (= a stable Guid).
    std::vector<Task> tasks;
    for (const auto& entry : fs::recursive_directory_iterator(contentDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const AssetType type = assetTypeForPath(entry.path());
        if (type == AssetType::Unknown) {
            continue;
        }
        Task task;
        task.source = entry.path();
        task.relative = fs::relative(entry.path(), contentDir).generic_string();
        if (!onlyFile.empty() && task.relative != onlyFile) {
            continue;
        }
        std::optional<AssetMeta> meta = readMeta(task.source);
        if (!meta) {
            meta = AssetMeta{Guid::generate(), type};
            writeMeta(task.source, *meta);
            EMBER_LOG_INFO("new asset   {} -> {}", task.relative, meta->guid.toString());
        }
        task.meta = *meta;
        tasks.push_back(std::move(task));
    }

    // 2. Skip what's up to date, cook the rest in parallel.
    Manifest manifest = Manifest::load(outDir);
    std::vector<Task*> toCook;
    for (Task& task : tasks) {
        task.hash = hashFile(task.source);
        const ManifestEntry* existing = manifest.find(task.meta.guid);
        const fs::path cookedFile = outDir / (task.meta.guid.toString() + cookedExtension(task.meta.type));
        const bool upToDate = existing && existing->sourceHash == task.hash && existing->cookerVersion == kCookerVersion &&
                              fs::exists(cookedFile);
        if (upToDate && !force) {
            continue;
        }
        toCook.push_back(&task);
    }

    {
        JobSystem jobs;
        jobs.parallelFor(static_cast<u32>(toCook.size()), 1, [&](u32 begin, u32 end) {
            for (u32 i = begin; i < end; ++i) {
                Task& task = *toCook[i];
                task.cooked = cook(task, outDir);
                task.failed = !task.cooked;
                EMBER_LOG_INFO("{} {}", task.cooked ? "cooked     " : "FAILED     ", task.relative);
            }
        });
    }

    // 3. Update the manifest. Entries for files that weren't scanned (or no longer exist) are kept
    //    when cooking a single file, and dropped on a full cook.
    if (onlyFile.empty()) {
        std::erase_if(manifest.entries, [&](const ManifestEntry& e) {
            return std::none_of(tasks.begin(), tasks.end(), [&](const Task& t) { return t.meta.guid == e.guid; });
        });
    }
    int failures = 0;
    for (const Task& task : tasks) {
        if (task.failed) {
            ++failures;
            continue;
        }
        if (!task.cooked) {
            continue; // unchanged
        }
        std::erase_if(manifest.entries, [&](const ManifestEntry& e) { return e.guid == task.meta.guid; });
        manifest.entries.push_back({task.meta.guid, task.meta.type, task.relative,
                                    task.meta.guid.toString() + cookedExtension(task.meta.type), task.hash, kCookerVersion});
    }
    std::sort(manifest.entries.begin(), manifest.entries.end(),
              [](const ManifestEntry& a, const ManifestEntry& b) { return a.source < b.source; });
    manifest.save(outDir);

    const f64 seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - start).count();
    EMBER_LOG_INFO("Cooker: {} assets, {} cooked, {} up to date, {} failed ({:.2f} s)", tasks.size(),
                   toCook.size() - static_cast<usize>(failures), tasks.size() - toCook.size(), failures, seconds);
    return failures == 0 ? 0 : 1;
}
