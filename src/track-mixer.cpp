// OBSLink - routage des sources audio vers une piste privée (voir track-mixer.h).
#include "track-mixer.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include "json.hpp"
#include <cstdio>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#define MTAG "[OBSLink] "

namespace obslink {

namespace {
constexpr int kNumTracks = 6;

struct EnumCtx {
    std::vector<AudioSourceInfo>* list;
};

bool IsMixerSource(obs_source_t* src)
{
    return (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) && obs_source_active(src);
}

uint32_t MixersUsedByMainOutputs()
{
    uint32_t used = 0;
    if (obs_output_t* o = obs_frontend_get_streaming_output()) {
        used |= obs_output_get_mixers(o);
        obs_output_release(o);
    }
    if (obs_output_t* o = obs_frontend_get_recording_output()) {
        used |= obs_output_get_mixers(o);
        obs_output_release(o);
    }
    return used;
}
} // namespace

std::vector<AudioSourceInfo> ListAudioSources()
{
    std::vector<AudioSourceInfo> list;
    EnumCtx ctx{&list};
    obs_enum_sources([](void* param, obs_source_t* src) {
        auto* c = static_cast<EnumCtx*>(param);
        if (!IsMixerSource(src))
            return true;
        AudioSourceInfo info;
        info.name = obs_source_get_name(src);
        info.mutedInObs = obs_source_muted(src);
        c->list->push_back(std::move(info));
        return true;
    }, &ctx);
    return list;
}

struct SourceRoute::Impl {
    int track = 5;
    uint32_t bit = 0;

    bool journalDirty = false;
    std::mutex applyLock;   // Apply() est appelé par le fil de rattrapage et par SetMuted()
    std::mutex lock;
    std::set<std::string> muted;

    struct Orig {
        obs_weak_source_t* weak = nullptr;
        bool hadBit = false;
        std::string name;
    };

    // Journal sur disque : permet de réparer l'affectation des pistes si OBS s'arrête avant la libération.
    std::string JournalPath() const
    {
        char dir[512];
        if (os_get_config_path(dir, sizeof(dir), "obs-studio/plugin_config/OBSLink") <= 0)
            return {};
        os_mkdirs(dir);
        return std::string(dir) + "/routes-" + std::to_string(track) + ".json";
    }
    void WriteJournal()
    {
        auto path = JournalPath();
        if (path.empty())
            return;
        nlohmann::json j;
        j["track"] = track;
        nlohmann::json arr = nlohmann::json::array();
        for (auto& kv : originals)
            arr.push_back({ {"name", kv.second.name}, {"hadBit", kv.second.hadBit} });
        j["sources"] = arr;
        if (FILE* f = fopen(path.c_str(), "wb")) {
            auto s = j.dump();
            fwrite(s.data(), 1, s.size(), f);
            fclose(f);
        }
    }
    void DeleteJournal()
    {
        auto path = JournalPath();
        if (!path.empty())
            remove(path.c_str());
    }
    std::map<uintptr_t, Orig> originals;

    std::atomic<bool> stop{false};
    std::thread worker;

    void Apply()
    {
        std::lock_guard<std::mutex> applyGuard(applyLock);
        std::set<std::string> mutedCopy;
        {
            std::lock_guard<std::mutex> g(lock);
            mutedCopy = muted;
        }
        struct Ctx { Impl* self; const std::set<std::string>* muted; } ctx{this, &mutedCopy};
        obs_enum_sources([](void* param, obs_source_t* src) {
            auto* c = static_cast<Ctx*>(param);
            if (!IsMixerSource(src))
                return true;
            Impl* self = c->self;
            const uintptr_t key = reinterpret_cast<uintptr_t>(src);
            const uint32_t cur = obs_source_get_audio_mixers(src);

            auto it = self->originals.find(key);
            if (it == self->originals.end()) {
                Orig o;
                o.weak = obs_source_get_weak_source(src);
                o.hadBit = (cur & self->bit) != 0;
                o.name = obs_source_get_name(src);
                it = self->originals.emplace(key, o).first;
                self->journalDirty = true;
            }
            const bool want = c->muted->count(obs_source_get_name(src)) == 0;
            const uint32_t next = want ? (cur | self->bit) : (cur & ~self->bit);
            if (next != cur)
                obs_source_set_audio_mixers(src, next);
            return true;
        }, &ctx);
        if (journalDirty) {
            journalDirty = false;
            WriteJournal();
        }
    }

    void Restore()
    {
        for (auto& kv : originals) {
            obs_source_t* src = obs_weak_source_get_source(kv.second.weak);
            if (src) {
                const uint32_t cur = obs_source_get_audio_mixers(src);
                const uint32_t next = kv.second.hadBit ? (cur | bit) : (cur & ~bit);
                if (next != cur)
                    obs_source_set_audio_mixers(src, next);
                obs_source_release(src);
            }
            obs_weak_source_release(kv.second.weak);
        }
        originals.clear();
        DeleteJournal();
    }
};

SourceRoute::SourceRoute() : impl_(new Impl) {}

SourceRoute::~SourceRoute()
{
    if (!impl_)
        return;
    impl_->stop = true;
    if (impl_->worker.joinable())
        impl_->worker.join();
    impl_->Restore();
    blog(LOG_INFO, MTAG "Piste %d rendue à OBS (affectation d'origine rétablie).", impl_->track + 1);
    delete impl_;
}

int SourceRoute::Track() const { return impl_->track; }

void SourceRoute::SetMuted(const std::vector<std::string>& mutedSources)
{
    {
        std::lock_guard<std::mutex> g(impl_->lock);
        impl_->muted = std::set<std::string>(mutedSources.begin(), mutedSources.end());
    }
    impl_->Apply(); // effet immédiat, même pendant le live
}

static std::mutex g_registryLock;
static std::map<std::string, std::weak_ptr<SourceRoute>> g_registry;

SourceRoutePtr AcquireRoute(const std::string& configId, const std::vector<std::string>& mutedSources)
{
    std::lock_guard<std::mutex> g(g_registryLock);

    auto it = g_registry.find(configId);
    if (it != g_registry.end()) {
        if (auto existing = it->second.lock()) {
            existing->SetMuted(mutedSources);
            return existing;
        }
    }

    // pistes déjà prises : sorties principales d'OBS (streaming / enregistrement) et autres routages OBSLink
    uint32_t used = MixersUsedByMainOutputs() | 1u; // la piste 1 n'est jamais réquisitionnée
    for (auto& kv : g_registry)
        if (auto r = kv.second.lock())
            used |= (1u << r->Track());

    int track = -1;
    for (int t = kNumTracks - 1; t >= 1; --t) {
        if (!(used & (1u << t))) {
            track = t;
            break;
        }
    }
    if (track < 0) {
        blog(LOG_ERROR, MTAG "Aucune piste libre pour isoler les sources de la cible.");
        return nullptr;
    }

    SourceRoutePtr route(new SourceRoute());
    route->impl_->track = track;
    route->impl_->bit = 1u << track;
    route->impl_->muted = std::set<std::string>(mutedSources.begin(), mutedSources.end());
    route->impl_->Apply();

    // les sources qui deviennent actives plus tard (changement de scène...) sont rattrapées chaque demi-seconde
    SourceRoute::Impl* impl = route->impl_;
    impl->worker = std::thread([impl]() {
        while (!impl->stop) {
            for (int i = 0; i < 5 && !impl->stop; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!impl->stop)
                impl->Apply();
        }
    });

    blog(LOG_INFO, MTAG "Piste %d réservée pour la cible (%zu source(s) désactivée(s)).", track + 1, mutedSources.size());
    g_registry[configId] = route;
    return route;
}

} // namespace obslink

namespace obslink {

void RepairInterruptedRoutes()
{
    char dir[512];
    if (os_get_config_path(dir, sizeof(dir), "obs-studio/plugin_config/OBSLink") <= 0)
        return;
    for (int t = 0; t < kNumTracks; ++t) {
        const std::string path = std::string(dir) + "/routes-" + std::to_string(t) + ".json";
        FILE* f = fopen(path.c_str(), "rb");
        if (!f)
            continue;
        std::string content;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            content.append(buf, n);
        fclose(f);

        bool inUse = false;
        {
            std::lock_guard<std::mutex> g(g_registryLock);
            for (auto& kv : g_registry)
                if (auto r = kv.second.lock())
                    if (r->Track() == t)
                        inUse = true;
        }
        if (inUse)
            continue; // routage vivant : ce n'est pas un reste d'arrêt brutal

        auto j = nlohmann::json::parse(content, nullptr, false);
        if (!j.is_discarded() && j.contains("sources") && j["sources"].is_array()) {
            const uint32_t bit = 1u << t;
            for (auto& e : j["sources"]) {
                if (!e.is_object() || !e.contains("name") || !e["name"].is_string())
                    continue;
                obs_source_t* src = obs_get_source_by_name(e["name"].get<std::string>().c_str());
                if (!src)
                    continue;
                const bool had = e.value("hadBit", false);
                const uint32_t cur = obs_source_get_audio_mixers(src);
                const uint32_t next = had ? (cur | bit) : (cur & ~bit);
                if (next != cur) {
                    obs_source_set_audio_mixers(src, next);
                    blog(LOG_INFO, MTAG "Réparation : piste %d de « %s » rétablie.", t + 1, e["name"].get<std::string>().c_str());
                }
                obs_source_release(src);
            }
        }
        remove(path.c_str());
    }
}

} // namespace obslink
