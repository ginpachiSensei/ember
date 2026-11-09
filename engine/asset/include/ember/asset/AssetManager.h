#pragma once

// Runtime asset loading: cooked files are read on job-system worker threads, and results are handed
// to the game on the main thread in update(). Assets are reference counted by Guid.
//
//   assets.acquire(guid);                   // starts loading (once) and holds a reference
//   ...each frame: assets.update();
//   if (assets.state(guid) == AssetState::Ready) use(*assets.model(guid));
//   if (assets.version(guid) != lastSeen)  // the asset was hot-reloaded: re-upload it
//   assets.release(guid);                   // data is freed when the last reference goes
//
// Hot reload (development): update() notices edited source files, runs the cooker on them and
// reloads the result.

#include "ember/asset/AssetDatabase.h"
#include "ember/asset/Model.h"
#include "ember/core/Jobs.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ember::asset {

enum class AssetState : u8 { Unloaded, Loading, Ready, Failed };
const char* assetStateName(AssetState state);

struct AssetManagerDesc {
    std::filesystem::path contentDir;
    std::filesystem::path cookedDir;
    std::filesystem::path cookerExe; // empty = no hot reload
};

class AssetManager {
public:
    AssetManager(const AssetManagerDesc& desc, JobSystem& jobs);
    ~AssetManager(); // waits for loads in flight

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Guid of a source file ("models/Helmet.glb", relative to the content directory), or an invalid Guid.
    Guid findBySource(std::string_view source) const;

    void acquire(const Guid& guid);
    void release(const Guid& guid);

    AssetState state(const Guid& guid) const;
    u32 version(const Guid& guid) const; // increases each time new data becomes Ready
    const Model* model(const Guid& guid) const;
    const HdrImage* environment(const Guid& guid) const;

    void update();                // main thread, once per frame
    void reload(const Guid& guid); // re-cook and reload now

    const Manifest& manifest() const { return m_manifest; }
    u32 loadsInFlight() const { return m_inFlight.pending.load(); }

private:
    struct Record {
        u32 refs = 0;
        AssetState state = AssetState::Unloaded;
        u32 version = 0;
        std::unique_ptr<Model> model;
        std::unique_ptr<HdrImage> environment;
        std::filesystem::file_time_type sourceTime{};
    };
    struct LoadJob {
        Guid guid;
        AssetType type;
        std::filesystem::path path;
        std::unique_ptr<Model> model;
        std::unique_ptr<HdrImage> environment;
        bool ok = false;
        f64 milliseconds = 0.0;
    };

    void startLoad(const Guid& guid);
    void pollSourceChanges();
    bool runCooker(const std::string& source);

    AssetManagerDesc m_desc;
    JobSystem& m_jobs;
    Manifest m_manifest;
    std::unordered_map<Guid, Record> m_records;

    JobCounter m_inFlight;
    std::mutex m_finishedMutex;
    std::vector<LoadJob*> m_finished;
    std::chrono::steady_clock::time_point m_lastPoll{};
};

} // namespace ember::asset
