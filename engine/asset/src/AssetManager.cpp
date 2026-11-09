#include "ember/asset/AssetManager.h"

#include "ember/asset/Cooked.h"
#include "ember/core/Log.h"
#include "ember/core/Profile.h"

#include <cstdlib>
#include <format>

namespace ember::asset {

namespace fs = std::filesystem;

namespace {

fs::file_time_type writeTime(const fs::path& path)
{
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    return ec ? fs::file_time_type{} : time;
}

} // namespace

const char* assetStateName(AssetState state)
{
    switch (state) {
    case AssetState::Unloaded: return "Unloaded";
    case AssetState::Loading: return "Loading";
    case AssetState::Ready: return "Ready";
    case AssetState::Failed: return "Failed";
    }
    return "?";
}

AssetManager::AssetManager(const AssetManagerDesc& desc, JobSystem& jobs)
    : m_desc(desc)
    , m_jobs(jobs)
    , m_manifest(Manifest::load(desc.cookedDir))
{
    EMBER_LOG_INFO("Asset manifest: {} assets in {}", m_manifest.entries.size(), desc.cookedDir.string());
}

AssetManager::~AssetManager()
{
    m_jobs.wait(m_inFlight);
    for (LoadJob* job : m_finished) {
        delete job;
    }
}

Guid AssetManager::findBySource(std::string_view source) const
{
    const ManifestEntry* entry = m_manifest.findBySource(source);
    return entry ? entry->guid : Guid{};
}

void AssetManager::acquire(const Guid& guid)
{
    Record& record = m_records[guid];
    if (record.refs++ == 0 && record.state == AssetState::Unloaded) {
        startLoad(guid);
    }
}

void AssetManager::release(const Guid& guid)
{
    const auto it = m_records.find(guid);
    if (it == m_records.end() || it->second.refs == 0) {
        return;
    }
    Record& record = it->second;
    if (--record.refs == 0 && record.state != AssetState::Loading) {
        record.model.reset();
        record.environment.reset();
        record.state = AssetState::Unloaded;
    }
}

AssetState AssetManager::state(const Guid& guid) const
{
    const auto it = m_records.find(guid);
    return it == m_records.end() ? AssetState::Unloaded : it->second.state;
}

u32 AssetManager::version(const Guid& guid) const
{
    const auto it = m_records.find(guid);
    return it == m_records.end() ? 0 : it->second.version;
}

const Model* AssetManager::model(const Guid& guid) const
{
    const auto it = m_records.find(guid);
    return it == m_records.end() ? nullptr : it->second.model.get();
}

const HdrImage* AssetManager::environment(const Guid& guid) const
{
    const auto it = m_records.find(guid);
    return it == m_records.end() ? nullptr : it->second.environment.get();
}

void AssetManager::startLoad(const Guid& guid)
{
    Record& record = m_records[guid];
    const ManifestEntry* entry = m_manifest.find(guid);
    if (!entry) {
        EMBER_LOG_ERROR("Asset {} is not in the manifest (run the cooker?)", guid.toString());
        record.state = AssetState::Failed;
        return;
    }
    record.state = AssetState::Loading;
    record.sourceTime = writeTime(m_desc.contentDir / entry->source);

    auto* job = new LoadJob{guid, entry->type, m_desc.cookedDir / entry->cooked};
    m_jobs.run(
        [this, job] {
            EMBER_PROFILE_SCOPE("Load asset");
            const auto start = std::chrono::steady_clock::now();
            if (job->type == AssetType::Model) {
                if (std::optional<Model> model = readCookedModel(job->path)) {
                    job->model = std::make_unique<Model>(std::move(*model));
                }
            } else if (job->type == AssetType::Environment) {
                if (std::optional<HdrImage> env = readCookedEnvironment(job->path)) {
                    job->environment = std::make_unique<HdrImage>(std::move(*env));
                }
            }
            job->ok = job->model || job->environment;
            job->milliseconds = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::lock_guard lock(m_finishedMutex);
            m_finished.push_back(job);
        },
        &m_inFlight);
}

void AssetManager::update()
{
    EMBER_PROFILE_FUNCTION();
    std::vector<LoadJob*> finished;
    {
        std::lock_guard lock(m_finishedMutex);
        finished.swap(m_finished);
    }
    for (LoadJob* job : finished) {
        Record& record = m_records[job->guid];
        if (record.refs == 0) {
            record.state = AssetState::Unloaded; // released while loading: drop it
        } else if (job->ok) {
            record.model = std::move(job->model);
            record.environment = std::move(job->environment);
            record.state = AssetState::Ready;
            ++record.version;
            const ManifestEntry* entry = m_manifest.find(job->guid);
            EMBER_LOG_INFO("Loaded {} in {:.1f} ms (worker thread)", entry ? entry->source : job->guid.toString(),
                           job->milliseconds);
        } else {
            // Keep whatever was loaded before (a broken re-cook shouldn't make the asset disappear).
            record.state = record.model || record.environment ? AssetState::Ready : AssetState::Failed;
        }
        delete job;
    }

    if (!m_desc.cookerExe.empty()) {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastPoll > std::chrono::milliseconds(500)) {
            m_lastPoll = now;
            pollSourceChanges();
        }
    }
}

void AssetManager::pollSourceChanges()
{
    for (auto& [guid, record] : m_records) {
        if (record.refs == 0 || record.state == AssetState::Loading) {
            continue;
        }
        const ManifestEntry* entry = m_manifest.find(guid);
        if (!entry) {
            continue;
        }
        const auto time = writeTime(m_desc.contentDir / entry->source);
        if (time != record.sourceTime) {
            EMBER_LOG_INFO("{} changed, re-cooking", entry->source);
            record.sourceTime = time;
            reload(guid);
        }
    }
}

bool AssetManager::runCooker(const std::string& source)
{
    // Development only: a blocking call to the cooker for one file. The cooker skips it if unchanged.
    const std::string command = std::format(R"(""{}" --content "{}" --out "{}" --file "{}"")", m_desc.cookerExe.string(),
                                            m_desc.contentDir.string(), m_desc.cookedDir.string(), source);
    const int result = std::system(command.c_str());
    if (result != 0) {
        EMBER_LOG_ERROR("Cooker failed for {} (exit code {})", source, result);
        return false;
    }
    m_manifest = Manifest::load(m_desc.cookedDir);
    return true;
}

void AssetManager::reload(const Guid& guid)
{
    const ManifestEntry* entry = m_manifest.find(guid);
    if (!entry) {
        return;
    }
    if (!m_desc.cookerExe.empty() && !runCooker(entry->source)) {
        return;
    }
    if (m_records[guid].refs > 0) {
        startLoad(guid);
    }
}

} // namespace ember::asset
