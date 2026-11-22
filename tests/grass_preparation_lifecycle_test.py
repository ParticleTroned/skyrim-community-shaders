"""Exercise production grass preparation publication with deterministic worker completion."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class GrassPreparationLifecycleTests(unittest.TestCase):
    def test_pending_stale_disabled_failed_and_shutdown(self):
        source = (ROOT / "src/Features/GrassOptimizations/GrassBucketRenderer.cpp").read_text(encoding="utf-8")
        fields = source[source.index("\tstruct PreparationJob\n"):source.index("#ifdef DEVBENCH_BRIDGE_ENABLED\n\tstd::atomic_bool diagnostics")]
        methods = "\n".join(block(source, signature) for signature in (
            "void GrassBucketRenderer::Impl::PrepareFrame(",
            "bool GrassBucketRenderer::Impl::CurrentBucket(",
            "void GrassBucketRenderer::Impl::ReindexBuckets(",
            "bool GrassBucketRenderer::Impl::PrepareBackground(",
            "void GrassBucketRenderer::Impl::SubmitBackgroundPreparation(",
            "void GrassBucketRenderer::Impl::FailBackgroundPreparation(",
            "void GrassBucketRenderer::Impl::EndBackgroundPreparation(",
            "void GrassBucketRenderer::EndFrame(",
            "GrassBucketRenderer::~GrassBucketRenderer("))
        methods = methods.replace("std::less<RE::BSMultiStreamInstanceTriShape*>", "std::less<uintptr_t>")
        methods += "\n#ifdef DEVBENCH_BRIDGE_ENABLED\n" + block(source, "void GrassBucketRenderer::SetBackgroundPreparationEnabled(") + "\n#endif\n"
        driver = r'''
#include "Features/GrassOptimizations/GrassBucketPreparation.h"
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
namespace SKSE::stl {
template<class F> struct scope_exit { F f; scope_exit(F fn): f(fn) {} ~scope_exit() { f(); } };
}
namespace logger { template<class... T> void warn(T&&...) {} }
namespace GrassCollisionMode { enum class Mode { Basic, Advanced }; }
struct GrassCollision { static constexpr float kBasicCollisionDistance = 512; };
enum class CaptureRejection { Destroyed, Stale };
namespace BS {
std::function<void()> pending;
std::thread worker;
int pools = 0;
bool failSubmission = false;
void Start() { if (pending) worker = std::thread(std::exchange(pending, {})); }
void Complete() { Start(); if (worker.joinable()) worker.join(); }
template<class... T> struct thread_pool {
    thread_pool(int threads) { if (threads != 1) throw std::runtime_error("Unbounded worker count"); ++pools; }
    ~thread_pool() { Complete(); }
    template<class F> auto submit_task(F fn) {
        if (pending) throw std::runtime_error("More than one pending job");
        if (failSubmission) throw std::runtime_error("Worker submission failed");
        auto task = std::make_shared<std::packaged_task<decltype(fn())()>>(fn);
        auto future = task->get_future(); pending = [task] { (*task)(); }; return future;
    }
};
}
struct Lifetime { std::atomic_bool alive{true}; std::atomic_uint64_t generation{0}; };
struct Queue { bool batching = true; std::vector<uintptr_t> members; std::atomic_bool compatible{true}; };
struct Group {
    uint32_t count = 1;
    std::array<float, 3> lo{}, hi{};
    float minVariance = 0, maxVariance = 0, maxBasisNorm = 0;
    bool boundsKnown = true;
    float birthTime = 0;
};
struct Source {
    uintptr_t identity;
    std::shared_ptr<Lifetime> lifetime = std::make_shared<Lifetime>();
    uint64_t generation = 0;
    std::shared_ptr<Queue> queue;
    std::vector<Group> groups{Group{}};
    std::shared_ptr<int> nativeOwner;
    bool contractValid = true;
    std::string model;
    int vertices = 0, indices = 0, material = 0, descriptor = 0, flags = 0, triangles = 0;
    int lightMask = 0;
    std::vector<int> lights;
    float renderDistance = 0, wavePeriod = 0;
    struct { float radius = 0; std::array<float, 3> center{}; } bound;
};
std::function<void()> beforeHash;
size_t CompatibilityHash(const Source&) { if (beforeHash) beforeHash(); return 0; }
bool CompatibleCaptured(const Source& a, const Source& b) { return a.model == b.model; }
bool CurrentContract(const Source& source) { return source.contractValid; }
bool CurrentGeometry(const Source&) { return true; }
bool Compatible(const Source& a, const Source& b) { return CurrentContract(a) && CurrentContract(b) && CompatibleCaptured(a, b); }
struct Admission { size_t bucket, source; };
struct Bucket {
    struct { void Retire() {} } plan;
    struct { void Clear() {} } outcomes;
    std::vector<Source> sources;
    std::vector<GrassPreparation::Run> runs;
    uint32_t instances = 0, recordsCapacity = 0;
    uint32_t RecordStride() const { return GrassPolicy::kRecordBytes; }
    std::unique_ptr<int> records;
    struct { std::unique_ptr<int> output; uint32_t capacity = 0, slots = 0; } result;
};
using QueuedGroup = int;
namespace globals {
    struct State { uint32_t frameCount = 1; } storage;
    auto* state = &storage;
    namespace features {
        struct { GrassPolicy::Settings settings; bool loaded = true; auto GetSettings() { return settings; } } grassOptimizations;
        struct {
            bool NeedsVertexDeformation() const { return false; }
            GrassCollisionMode::Mode GetVertexMode() const { return GrassCollisionMode::Mode::Advanced; }
            float GetCullingStrength() const { return 0; }
        } grassCollision;
    }
}
struct GrassBucketRenderer {
    struct Impl {
        FIELDS
        uint64_t residentRevision = 0, preparedRevision = UINT64_MAX, residentRecordBytes = 0, resultBytes = 0;
        bool recordLayoutDirty = true;
        std::atomic_uint32_t cullingControls{3};
        bool frameCombinedCulling = true, frameAlphaRejection = true;
        bool preparedBatching = true, failed = false;
        GrassPolicy::Settings frameSettings;
        uint32_t frame = 1;
        bool frameCollisionEnabled = false, frameCollisionBasic = false, cullingPrepared = false, drawShadersChecked = false;
        float frameCollisionStrength = 0;
        void* current = nullptr;
        struct {} frameWork;
        uint32_t telemetryViewportCount = 0;
        std::atomic_bool telemetryFailed{false};
        uint64_t unidentifiedModels = 0, sameModelPeers = 0, sameModelCompatible = 0;
        std::array<uint64_t, 7> sameModelMismatches{};
        std::mutex captureMutex;
        std::atomic_uint64_t publishedResultBytes{0};
        std::atomic_bool diagnostics{false};
        std::atomic_uint64_t bucketRebuilds{0}, admittedSources{0}, persistentBucketFrames{0};
        std::unordered_map<uintptr_t, Source> residents;
        std::unordered_multimap<size_t, std::weak_ptr<Queue>> queues;
        std::unordered_map<uintptr_t, Admission> admitted;
        std::vector<Bucket> buckets, dormantBuckets;
        std::vector<std::shared_ptr<const QueuedGroup>> queuedGroups;
        struct LODLoad { std::atomic_bool cancelled{false}; };
        std::vector<std::unique_ptr<LODLoad>> lodLoads;
        std::unique_ptr<int> lodWorker;
        void ClearDrawContext() {}
        void PrepareFrame();
        void PublishTelemetry() {}
        void PollCounters() {}
        void RejectCapture(CaptureRejection) {}
        void EraseResident(uintptr_t identity) { residents.erase(identity); }
        size_t cacheRunBuilds = 0, recordReusePasses = 0, fadeRefreshes = 0;
        void CacheRuns(Bucket& bucket) {
            ++cacheRunBuilds;
            GrassPreparation::BuildRuns(bucket.runs, bucket.sources.size(), [&](size_t index) -> const Source& { return bucket.sources[index]; });
        }
        void UpdateLODDemand(const GrassPolicy::Settings&) {}
        void RetireCulling() {}
        void PollLODLoads() {}
        struct { void EndFrame(uint32_t) {} } textureCache;
        void ReuseRecordBuffers(std::vector<Bucket>&) { ++recordReusePasses; }
        void RefreshFades() {
            ++fadeRefreshes;
            for (const auto& bucket : buckets) for (const auto& source : bucket.sources) {
                const auto& live = residents.at(source.identity);
                if (live.generation != source.generation) throw std::runtime_error("Stale source retained");
            }
        }
    };
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    ~GrassBucketRenderer();
    void EndFrame();
    void SetBackgroundPreparationEnabled(bool enabled);
};
METHODS
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
// Isolate publication tests; full-frame tests below exercise synchronous draw admission.
bool PrepareAndEndFrame(GrassBucketRenderer::Impl& state) {
    const bool ready = state.PrepareBackground();
    state.frame = globals::state->frameCount;
    state.EndBackgroundPreparation(state.frameSettings, !state.HasCurrentPreparation());
    return ready;
}
void CheckChangedModelAndSplitQueue() {
    GrassBucketRenderer renderer;
    auto& state = *renderer.impl;
    const auto queue = std::make_shared<Queue>(); queue->members = {1,2};
    for (uintptr_t id : {1,2}) {
        Source item{}; item.identity = id; item.queue = queue;
        state.residents.emplace(id, std::move(item));
    }
    PrepareAndEndFrame(state); BS::Complete();
    state.residents.at(1).model = "changed.nif"; ++state.residentRevision;
    PrepareAndEndFrame(state);
    Require(state.admitted.empty(), "Old model metadata was published after the resident model changed");
    Require(queue->compatible, "A stale job invalidated the newer queue");
    BS::Complete(); PrepareAndEndFrame(state);
    Require(state.admitted.empty() && !queue->compatible, "A split queue was left permanently eligible without complete batch coverage");
    const auto replacement = std::make_shared<Queue>(); replacement->members = {1};
    state.residents.at(1).queue = replacement;
    queue->members = {2}; queue->compatible = true; ++state.residentRevision;
    PrepareAndEndFrame(state); BS::Complete(); PrepareAndEndFrame(state);
    Require(state.admitted.size() == 2 && !state.preparationJob, "Reassigned queues did not recover batching");
}
void CheckLatePublicationKeepsCurrentPreparation() {
    for (bool newerRevision : {false, true})
    for (bool workerFails : {false, true}) {
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        Source item{}; item.identity = 1; item.queue = std::make_shared<Queue>(); item.queue->members = {1};
        state.residents.emplace(1, std::move(item));
        Require(!PrepareAndEndFrame(state) && state.preparationJob, "Unpublished work skipped synchronous recovery");
        if (newerRevision) ++state.residentRevision;
        state.preparedRevision = state.residentRevision;
        state.preparedBatching = state.frameSettings.CrossCellBatching;
        Bucket current;
        current.sources.push_back(state.residents.at(1)); current.instances = 1;
        current.records = std::make_unique<int>(73);
        const auto* records = current.records.get();
        state.buckets.push_back(std::move(current));
        state.ReindexBuckets();
        if (workerFails) beforeHash = [] { throw std::runtime_error("Obsolete preparation failed"); };
        BS::Complete(); beforeHash = {};
        Require(PrepareAndEndFrame(state) && !state.preparationJob && !state.preparationFailed,
            "A late worker result displaced synchronous recovery or disabled preparation");
        Require(state.preparedRevision == state.residentRevision && state.buckets.size() == 1 &&
                state.buckets.front().records.get() == records && state.admitted.contains(1),
            "A late snapshot replaced current admission or owned GPU resources");
#ifdef DEVBENCH_BRIDGE_ENABLED
        Require(state.preparationSubmitted == 1 && state.preparationPublished == 0 && state.preparationDiscarded == 1,
            "Obsolete publication created redundant work or incorrect telemetry");
#endif
    }
}
void CheckFramePreparationBeforeAdmission() {
    GrassBucketRenderer renderer;
    auto& state = *renderer.impl;
    auto& settings = globals::features::grassOptimizations.settings;
    const auto previousSettings = settings;
    const SKSE::stl::scope_exit restore([&] { settings = previousSettings; });
    settings = state.frameSettings;
    auto queue = std::make_shared<Queue>(); queue->members = {1,2};
    for (uintptr_t id : {1,2}) {
        Source item{}; item.identity = id; item.queue = queue;
        state.residents.emplace(id, std::move(item));
    }
    state.frame = globals::state->frameCount - 1;
    state.PrepareFrame();
    Require(!state.preparationJob && !state.preparationWorker && state.buckets.size() == 1 && state.admitted.size() == 2 &&
            state.preparedRevision == state.residentRevision,
        "First draw skipped synchronous preparation or duplicated it on a worker");
    state.buckets.front().records = std::make_unique<int>(73);
    const auto* records = state.buckets.front().records.get();
    ++globals::state->frameCount; state.PrepareFrame();
    Require(state.buckets.front().records.get() == records && state.admitted.size() == 2,
        "Unchanged frame rebuilt its current preparation");
    Source added{}; added.identity = 3; added.queue = queue;
    state.residents.emplace(3, std::move(added)); queue->members.push_back(3);
    ++state.residentRevision;
    ++globals::state->frameCount; state.PrepareFrame();
    Require(!state.preparationJob && state.buckets.size() == 1 && state.admitted.size() == 3 && state.admitted.contains(3) &&
            state.preparedRevision == state.residentRevision,
        "Newly visible grass waited for the older worker snapshot");
    settings.CrossCellBatching = false;
    for (auto& [id, source] : state.residents) {
        source.queue = std::make_shared<Queue>(); source.queue->batching = false; source.queue->members = {id};
    }
    ++globals::state->frameCount; state.PrepareFrame();
    Require(!state.preparationJob && state.buckets.size() == 3 && state.admitted.size() == 3 && !state.preparedBatching &&
            state.preparedRevision == state.residentRevision,
        "Changed batching mode skipped current synchronous preparation");
    state.buckets.front().records = std::make_unique<int>(91);
    records = state.buckets.front().records.get();
    BS::Complete();
    ++globals::state->frameCount; state.PrepareFrame();
    Require(!state.preparationJob && state.buckets.front().records.get() == records && state.admitted.size() == 3,
        "Unchanged frame displaced current preparation and its reusable records");
}
void CheckFrameEndPreparation() {
    for (int completion : {0, 1, 2, 3}) {
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        auto& settings = globals::features::grassOptimizations.settings;
        const auto previousSettings = settings;
        const SKSE::stl::scope_exit restore([&] { settings = previousSettings; BS::failSubmission = false; beforeHash = {}; });
        settings = state.frameSettings;
        auto queue = std::make_shared<Queue>(); queue->members = {1};
        Source first{}; first.identity = 1; first.queue = queue;
        state.residents.emplace(1, std::move(first));
        state.frame = globals::state->frameCount - 1;
        state.PrepareFrame();
        renderer.EndFrame();
        Require(!state.preparationJob && !state.preparationWorker && state.cacheRunBuilds == 1,
            "Current preparation submitted redundant end-of-frame work");

        Source added{}; added.identity = 2; added.queue = queue;
        state.residents.emplace(2, std::move(added)); queue->members.push_back(2);
        ++state.residentRevision;
        BS::failSubmission = completion == 3;
        renderer.EndFrame();
        BS::failSubmission = false;
        if (completion == 3) {
            Require(state.preparationFailed && !state.preparationJob,
                "Failed submission retained a borrowed snapshot or escaped EndFrame");
        } else {
            Require(state.preparationJob && state.preparedRevision != state.residentRevision,
                "Post-draw changes were not submitted ahead of the next frame");
            const auto* pending = state.preparationJob.get();
            renderer.EndFrame();
            Require(state.preparationJob.get() == pending, "Repeated EndFrame duplicated pending work");
            if (completion == 2) beforeHash = [] { throw std::runtime_error("Worker layout failed"); };
            if (completion != 1) BS::Complete();
            beforeHash = {};
        }

        ++globals::state->frameCount;
        state.PrepareFrame();
        Require(state.admitted.size() == 2 && state.HasCurrentPreparation(),
            "Draw admission did not recover current complete coverage");
        Require(state.cacheRunBuilds == (completion == 0 ? 1u : 2u),
            "Ready worker result was rebuilt or unfinished work skipped synchronous recovery");
        renderer.EndFrame();
        Require(bool(state.preparationJob) == (completion == 1),
            "Recovery submitted duplicate work or released an unfinished snapshot");
        BS::Complete();
        ++globals::state->frameCount;
        state.PrepareFrame();
        renderer.EndFrame();
        Require(!state.preparationJob && state.HasCurrentPreparation() &&
                state.cacheRunBuilds == (completion == 0 ? 1u : 2u),
            "Late completion displaced current preparation or restarted worker work");
#ifdef DEVBENCH_BRIDGE_ENABLED
        Require(state.preparationSubmitted == (completion == 3 ? 0u : 1u) &&
                state.preparationPublished == (completion == 0 ? 1u : 0u) &&
                state.preparationDiscarded == (completion == 1 ? 1u : 0u),
            "Submission, publication or discard telemetry did not match the frame lifecycle");
        Require(state.preparationFrames == 3 && state.preparationRenderNanoseconds > 0,
            "Render preparation accounting lost the draw frames or submission time");
#endif
    }
}
void CheckFrameEndSubmissionGates() {
    for (int reason : {0, 1, 2, 3, 4, 5}) {
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        auto& settings = globals::features::grassOptimizations.settings;
        const auto previousSettings = settings;
        const SKSE::stl::scope_exit restore([&] { settings = previousSettings; });
        settings = state.frameSettings;
        state.frame = globals::state->frameCount - 1;
        state.PrepareFrame();
        ++state.residentRevision;
        if (reason == 0) ++globals::state->frameCount;
        if (reason == 1) settings.Enabled = false;
        if (reason == 2) settings.CrossCellBatching = !state.frameSettings.CrossCellBatching;
        if (reason == 3) state.preparationRequest += 2;
        if (reason == 4) state.preparationRequest = 0;
        if (reason == 5) state.frameSettings.Enabled = false;
        renderer.EndFrame();
        Require(!state.preparationJob && !state.preparationWorker,
            "Inactive frame or unapplied settings submitted worker preparation");
    }
}
void CheckObsoleteFrameEndResult() {
    for (bool workerFails : {false, true}) {
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        auto& settings = globals::features::grassOptimizations.settings;
        const auto previousSettings = settings;
        const SKSE::stl::scope_exit restore([&] { settings = previousSettings; beforeHash = {}; });
        settings = state.frameSettings;
        const auto queue = std::make_shared<Queue>(); queue->members = {1};
        Source first{}; first.identity = 1; first.queue = queue;
        state.residents.emplace(1, std::move(first));
        state.frame = globals::state->frameCount - 1;
        state.PrepareFrame();
        Source second{}; second.identity = 2; second.queue = queue;
        state.residents.emplace(2, std::move(second)); queue->members.push_back(2); ++state.residentRevision;
        renderer.EndFrame();
        Require(bool(state.preparationJob), "Post-draw changes did not start preparation");
        if (workerFails) beforeHash = [] { throw std::runtime_error("Superseded layout failed"); };
        BS::Complete(); beforeHash = {};
        Source third{}; third.identity = 3; third.queue = queue;
        state.residents.emplace(3, std::move(third)); queue->members.push_back(3); ++state.residentRevision;
        ++globals::state->frameCount;
        state.PrepareFrame();
        renderer.EndFrame();
        Require(state.HasCurrentPreparation() && state.admitted.size() == 3 && !state.preparationJob && !state.preparationFailed,
            "Obsolete result or failure prevented current synchronous recovery");
        Require(state.cacheRunBuilds == 2 && state.recordReusePasses == 2 && state.fadeRefreshes == 2,
            "Obsolete preparation performed publication, validation or fade work before synchronous recovery");
#ifdef DEVBENCH_BRIDGE_ENABLED
        Require(state.preparationSubmitted == 1 && state.preparationPublished == 0 && state.preparationDiscarded == 1,
            "Obsolete revision was published or submitted again after recovery");
#endif
    }
}
void CheckInvalidatedFrameEndCapture() {
    for (bool removed : {false, true}) {
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        auto& settings = globals::features::grassOptimizations.settings;
        const auto previousSettings = settings;
        const SKSE::stl::scope_exit restore([&] { settings = previousSettings; });
        settings = state.frameSettings;
        Source first{}; first.identity = 1; first.queue = std::make_shared<Queue>(); first.queue->members = {1};
        state.residents.emplace(1, std::move(first));
        state.frame = globals::state->frameCount - 1;
        state.PrepareFrame();
        Source stale{}; stale.identity = 2; stale.queue = std::make_shared<Queue>(); stale.queue->members = {2};
        if (removed) stale.lifetime->alive = false;
        else ++stale.lifetime->generation;
        state.residents.emplace(2, std::move(stale)); ++state.residentRevision;
        renderer.EndFrame();
        Require(!state.preparationJob && !state.preparationWorker && !state.preparationFailed,
            "Known invalidated capture submitted worker work that the next draw must discard");
        ++globals::state->frameCount;
        state.PrepareFrame();
        renderer.EndFrame();
        Require(state.HasCurrentPreparation() && state.admitted.size() == 1 && state.admitted.contains(1) &&
                !state.residents.contains(2) && !state.preparationFailed,
            "Invalidated end-of-frame capture did not recover through draw-time pruning");
    }
}
void CheckBatchingChangeWithWorker() {
    for (bool initialBatching : {false, true})
    for (bool workerReady : {false, true}) {
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        auto& settings = globals::features::grassOptimizations.settings;
        const auto previousSettings = settings;
        const SKSE::stl::scope_exit restore([&] { settings = previousSettings; });
        settings = state.frameSettings;
        settings.CrossCellBatching = initialBatching;
        auto queue = std::make_shared<Queue>(); queue->batching = initialBatching;
        for (uintptr_t id : {1, 2, 3}) {
            if (!initialBatching) { queue = std::make_shared<Queue>(); queue->batching = false; }
            Source item{}; item.identity = id; item.queue = queue; queue->members.push_back(id);
            state.residents.emplace(id, std::move(item)); ++state.residentRevision;
            if (id == 2) {
                state.frame = globals::state->frameCount - 1;
                state.PrepareFrame();
            }
        }
        renderer.EndFrame();
        Require(bool(state.preparationJob), "Post-draw resident change did not submit a worker");
        if (workerReady) BS::Complete();
        settings.CrossCellBatching = !initialBatching;
        queue = std::make_shared<Queue>(); queue->batching = !initialBatching;
        for (auto& [id, item] : state.residents) {
            if (initialBatching) { queue = std::make_shared<Queue>(); queue->batching = false; }
            item.queue = queue; queue->members.push_back(id);
        }
        ++state.residentRevision;
        ++globals::state->frameCount;
        state.PrepareFrame();
        Require(state.HasCurrentPreparation() && state.preparedBatching == settings.CrossCellBatching &&
                state.admitted.size() == 3 && state.buckets.size() == (initialBatching ? 3u : 1u) &&
                bool(state.preparationJob) == !workerReady,
            "Changed batching mode reached draw admission without current complete preparation");
        state.buckets.front().records = std::make_unique<int>(91);
        const auto* records = state.buckets.front().records.get();
        renderer.EndFrame(); BS::Complete();
        ++globals::state->frameCount;
        state.PrepareFrame(); renderer.EndFrame();
        Require(state.HasCurrentPreparation() && state.admitted.size() == 3 && !state.preparationJob &&
                !state.preparationFailed && state.buckets.front().records.get() == records,
            "Old batching result replaced synchronous admission or its reusable GPU records");
#ifdef DEVBENCH_BRIDGE_ENABLED
        Require(state.preparationSubmitted == 1 && state.preparationPublished == 0 && state.preparationDiscarded == 1,
            "Changed batching mode published or duplicated obsolete worker work");
#endif
    }
}
#ifdef DEVBENCH_BRIDGE_ENABLED
void CheckUnusedRetirement() {
    for (int reason = 0; reason < 4; ++reason) {
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        auto& settings = globals::features::grassOptimizations.settings;
        settings = state.frameSettings;
        globals::state->frameCount = state.frame;
        Source item{}; item.identity = 1; item.queue = std::make_shared<Queue>(); item.queue->members = {1};
        const auto ownerThread = std::this_thread::get_id();
        bool releasedOnOwner = false, releasedOutsideLock = false;
        item.nativeOwner = std::shared_ptr<int>(new int, [&](int* p) {
            releasedOnOwner = std::this_thread::get_id() == ownerThread;
            releasedOutsideLock = state.captureMutex.try_lock();
            if (releasedOutsideLock) state.captureMutex.unlock();
            delete p;
        });
        auto owner = std::weak_ptr(item.nativeOwner);
        state.residents.emplace(1, std::move(item));
        renderer.SetBackgroundPreparationEnabled(true); PrepareAndEndFrame(state);
        state.residents.clear();
        renderer.EndFrame();
        Require(state.preparationPending, "EndFrame waited for unfinished preparation");
        ++globals::state->frameCount; renderer.EndFrame();
        Require(state.preparationPending && !owner.expired(), "Idle retirement waited for or released a borrowed snapshot");
        --globals::state->frameCount;
        BS::Complete(); renderer.EndFrame();
        Require(state.preparationPending && !owner.expired(), "Active same-frame result was discarded before publication");
        if (reason == 0) ++globals::state->frameCount;
        if (reason == 1) settings.Enabled = false;
        if (reason == 2) renderer.SetBackgroundPreparationEnabled(false);
        if (reason == 3) settings.CrossCellBatching = false;
        renderer.EndFrame();
        Require(!state.preparationPending && !state.preparationActive && state.preparationDiscarded == 1,
            "Unused completed snapshot was retained without further grass draws");
        Require(owner.expired() && releasedOnOwner && releasedOutsideLock, "Unused native owners retired on the wrong thread or under captureMutex");
    }
}
int main() {
    try {
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        Require(state.preparationRequest == 1, "Background preparation did not default on");
        state.frameSettings.Enabled = false;
        Require(!PrepareAndEndFrame(state) && !state.preparationWorker, "Disabled GO started a preparation worker");
        state.frameSettings.Enabled = true;
        auto a = std::make_shared<Queue>(); a->members = {1,2};
        auto b = std::make_shared<Queue>(); b->members = {3};
        for (uintptr_t id : {1,2,3}) {
            Source item{}; item.identity = id; item.queue = id == 3 ? b : a;
            state.residents.emplace(id, std::move(item));
        }
        const auto ownerThread = std::this_thread::get_id();
        bool releasedOnOwner = false;
        state.residents.at(2).nativeOwner = std::shared_ptr<int>(new int, [&](int* p) {
            releasedOnOwner = std::this_thread::get_id() == ownerThread; delete p;
        });
        std::weak_ptr<int> nativeOwner = state.residents.at(2).nativeOwner;
        renderer.SetBackgroundPreparationEnabled(true);
        Require(!PrepareAndEndFrame(state) && state.preparationPending && state.admitted.empty(), "Pending job skipped synchronous preparation");
        std::promise<void> entered, release;
        auto started = entered.get_future(); auto resume = release.get_future();
        bool gated = false;
        beforeHash = [&] {
            if (!gated) { gated = true; entered.set_value(); resume.wait(); }
        };
        BS::Start(); started.wait();
        state.residents.at(2).lifetime->alive = false;
        state.residents.erase(2); a->members = {1}; ++state.residentRevision;
        const bool remainedPending = !PrepareAndEndFrame(state) && state.preparationSubmitted == 1;
        release.set_value();
        BS::Complete();
        beforeHash = {};
        Require(remainedPending, "Pending work blocked or duplicated submission");
        Require(!nativeOwner.expired(), "Worker released a captured native owner");
        Require(!PrepareAndEndFrame(state) && state.admitted.empty(),
            "Obsolete revision was partially published before synchronous recovery");
        Require(a->compatible, "Stale partial coverage invalidated the newer queue membership");
        Require(nativeOwner.expired() && releasedOnOwner, "Native snapshot was not retired on its owner thread");
        Require(state.preparationDiscarded == 1 && state.preparationRejectedBuckets == 0 && state.preparationSubmitted == 2,
            "Obsolete revision was validated or did not coalesce into one replacement job");
        ++state.residents.at(3).lifetime->generation; ++state.residents.at(3).generation; ++state.residentRevision;
        Require(!PrepareAndEndFrame(state) && !state.admitted.contains(3), "Regenerated source retained old admission");
        BS::Complete(); PrepareAndEndFrame(state);
        Require(state.admitted.empty() && state.preparationDiscarded == 2, "Obsolete generation was partially published");
        const auto published = state.preparationPublished.load();
        const auto discarded = state.preparationDiscarded.load();
        renderer.SetBackgroundPreparationEnabled(false);
        Require(!PrepareAndEndFrame(state) && state.preparationPending, "Disabling waited for pending work");
        BS::Complete();
        Require(!PrepareAndEndFrame(state) && !state.preparationPending && state.preparationPublished == published && state.preparationDiscarded == discarded + 1,
            "Disabled worker result was published");
        state.diagnostics = true;
        renderer.SetBackgroundPreparationEnabled(true); PrepareAndEndFrame(state); BS::Complete(); PrepareAndEndFrame(state);
        Require(state.admitted.size() == 2 && !state.preparationPending, "Re-enabled preparation did not publish all current queues");
        Require(state.bucketRebuilds == 1 && state.persistentBucketFrames == 0, "Publication frame was also counted as reuse");
        const auto submitted = state.preparationSubmitted.load();
        PrepareAndEndFrame(state);
        Require(state.preparationSubmitted == submitted && BS::pools == 1, "Stable frame rebuilt or created another worker");
        Require(state.persistentBucketFrames == 1, "Stable frame was not counted as reuse");
        auto snapshot = state.buckets.front().sources;
        const auto identity = snapshot.front().identity;
        const auto lifetime = state.residents.at(identity).lifetime;
        state.residents.at(identity).lifetime = std::make_shared<Lifetime>();
        Require(!state.CurrentBucket(snapshot), "Reused shape address accepted the previous lifetime");
        state.residents.at(identity).lifetime = lifetime;
        snapshot.front().queue->members.push_back(99);
        Require(!state.CurrentBucket(snapshot), "Expanded queue accepted partial coverage");
        snapshot.front().queue->members.pop_back();
        snapshot.front().contractValid = false;
        Require(!state.CurrentBucket(snapshot), "Changed material contract was accepted");
        state.residents.at(1).groups.front().count = GrassPolicy::kMaxBatchInstances + 1; ++state.residentRevision;
        PrepareAndEndFrame(state); BS::Complete();
        Require(!PrepareAndEndFrame(state) && state.preparationFailed && !state.preparationPending, "Worker failure did not restore synchronous fallback");
        Require(!PrepareAndEndFrame(state), "Failed worker retried without a new toggle");
        renderer.SetBackgroundPreparationEnabled(false); renderer.SetBackgroundPreparationEnabled(true);
        PrepareAndEndFrame(state); BS::Complete();
        renderer.SetBackgroundPreparationEnabled(false); renderer.SetBackgroundPreparationEnabled(true);
        state.residents.at(1).groups.front().count = 1;
        Require(!PrepareAndEndFrame(state) && !state.preparationFailed && state.preparationPending,
            "Obsolete worker failure disabled a newly toggled run");
        BS::Complete(); PrepareAndEndFrame(state);
        Require(!state.preparationFailed && state.admitted.size() == 2, "Fresh run did not recover from obsolete failure");
        renderer.SetBackgroundPreparationEnabled(false); renderer.SetBackgroundPreparationEnabled(true);
        Require(!PrepareAndEndFrame(state) && !state.preparationFailed && state.preparationPending, "Fresh toggle did not recover after failure");
        {
            GrassBucketRenderer ending;
            renderer.impl.swap(ending.impl);
        }
        Require(!BS::pending, "Renderer shutdown did not drain its borrowed snapshot");
        CheckUnusedRetirement();
        CheckChangedModelAndSplitQueue();
        CheckLatePublicationKeepsCurrentPreparation();
        CheckFramePreparationBeforeAdmission();
        CheckFrameEndPreparation();
        CheckFrameEndSubmissionGates();
        CheckObsoleteFrameEndResult();
        CheckInvalidatedFrameEndCapture();
        CheckBatchingChangeWithWorker();
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
#else
int main() {
    try {
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        Require(state.preparationRequest == 1, "Production background preparation did not default on");
        state.frameSettings.Enabled = false;
        Require(!PrepareAndEndFrame(state) && !state.preparationWorker, "Disabled production GO created a worker");
        state.frameSettings.Enabled = true;
        Source item{}; item.identity = 1; item.queue = std::make_shared<Queue>(); item.queue->members = {1};
        state.residents.emplace(1, std::move(item));
        Require(!PrepareAndEndFrame(state) && state.preparationJob && state.admitted.empty(), "Default production path skipped synchronous preparation while pending");
        BS::Complete(); PrepareAndEndFrame(state);
        Require(state.admitted.contains(1) && !state.preparationJob, "Production worker result did not publish");
        PrepareAndEndFrame(state);
        Require(!state.preparationJob && BS::pools == 1, "Production stable frame created redundant work");
        state.residents.at(1).groups.front().count = GrassPolicy::kMaxBatchInstances + 1;
        ++state.residentRevision;
        PrepareAndEndFrame(state); BS::Complete();
        Require(!PrepareAndEndFrame(state) && state.preparationFailed && !state.preparationJob, "Production failure did not restore synchronous preparation");
        Require(!PrepareAndEndFrame(state), "Failed production worker retried without recovery");
        {
            GrassBucketRenderer ending;
            auto& pending = *ending.impl;
            Source retained{}; retained.identity = 2; retained.queue = std::make_shared<Queue>(); retained.queue->members = {2};
            const auto ownerThread = std::this_thread::get_id();
            bool releasedOnOwner = false;
            retained.nativeOwner = std::shared_ptr<int>(new int, [&](int* p) { releasedOnOwner = std::this_thread::get_id() == ownerThread; delete p; });
            auto owner = std::weak_ptr(retained.nativeOwner);
            pending.residents.emplace(2, std::move(retained));
            PrepareAndEndFrame(pending); pending.residents.clear();
            BS::Complete(); ++globals::state->frameCount;
            ending.EndFrame();
            Require(!pending.preparationJob && owner.expired() && releasedOnOwner, "Idle production snapshot did not retire on its owner thread");
            --globals::state->frameCount;
            pending.residents.emplace(2, Source{2}); ++pending.residentRevision;
            PrepareAndEndFrame(pending);
        }
        Require(!BS::pending, "Production shutdown did not drain its borrowed snapshot");
        CheckChangedModelAndSplitQueue();
        CheckLatePublicationKeepsCurrentPreparation();
        CheckFramePreparationBeforeAdmission();
        CheckFrameEndPreparation();
        CheckFrameEndSubmissionGates();
        CheckObsoleteFrameEndResult();
        CheckInvalidatedFrameEndCapture();
        CheckBatchingChangeWithWorker();
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
#endif
'''.replace("FIELDS", fields).replace("METHODS", methods)
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        with tempfile.TemporaryDirectory(prefix="grass-preparation-") as temporary:
            directory = Path(temporary)
            cpp = directory / "driver.cpp"
            executable = directory / "driver.exe"
            cpp.write_text(driver, encoding="utf-8")
            for devbench in (False, True):
                with self.subTest(devbench=devbench):
                    command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX",
                               "/I" + str(ROOT / "src"), str(cpp), "/Fe:" + str(executable)]
                    if devbench:
                        command.append("/DDEVBENCH_BRIDGE_ENABLED")
                    result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(executable)], cwd=directory, capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
