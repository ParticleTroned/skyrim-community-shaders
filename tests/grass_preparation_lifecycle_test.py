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
        fields = source[source.index("\tstruct PreparationJob\n"):source.index("\tstd::atomic_bool diagnostics")]
        methods = "\n".join(block(source, signature) for signature in (
            "bool GrassBucketRenderer::Impl::CurrentBucket(",
            "void GrassBucketRenderer::Impl::ReindexBuckets(",
            "bool GrassBucketRenderer::Impl::PrepareBackground(",
            "void GrassBucketRenderer::SetBackgroundPreparationEnabled(",
            "GrassBucketRenderer::~GrassBucketRenderer("))
        driver = r'''
#include "Features/GrassOptimizations/GrassBucketPreparation.h"
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
namespace SKSE::stl {
template<class F> struct scope_exit { F f; scope_exit(F fn): f(fn) {} ~scope_exit() { f(); } };
}
namespace logger { template<class... T> void warn(T&&...) {} }
namespace BS {
std::function<void()> pending;
int pools = 0;
void Complete() { auto task = std::exchange(pending, {}); if (task) task(); }
template<class... T> struct thread_pool {
    thread_pool(int threads) { if (threads != 1) throw std::runtime_error("Unbounded worker count"); ++pools; }
    ~thread_pool() { Complete(); }
    template<class F> auto submit_task(F fn) {
        if (pending) throw std::runtime_error("More than one pending job");
        auto task = std::make_shared<std::packaged_task<decltype(fn())()>>(fn);
        auto future = task->get_future(); pending = [task] { (*task)(); }; return future;
    }
};
}
struct Lifetime { std::atomic_bool alive{true}; std::atomic_uint64_t generation{0}; };
struct Queue { bool batching = true; std::vector<uintptr_t> members; };
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
};
size_t CompatibilityHash(const Source&) { return 0; }
bool CompatibleCaptured(const Source&, const Source&) { return true; }
bool CurrentContract(const Source& source) { return source.contractValid; }
struct Admission { size_t bucket, source; };
struct Bucket {
    std::vector<Source> sources;
    std::vector<GrassPreparation::Run> runs;
    uint32_t instances = 0, recordsCapacity = 0;
    std::unique_ptr<int> records;
    struct { std::unique_ptr<int> output; uint32_t capacity = 0, slots = 0; } result;
};
struct GrassBucketRenderer {
    struct Impl {
        FIELDS
        uint64_t residentRevision = 0, preparedRevision = UINT64_MAX, residentRecordBytes = 0, resultBytes = 0;
        bool preparedBatching = true, failed = false;
        struct { bool CrossCellBatching = true, Enabled = true; } frameSettings;
        std::atomic_uint64_t publishedResultBytes{0};
        std::atomic_bool diagnostics{false};
        std::atomic_uint64_t bucketRebuilds{0}, admittedSources{0}, persistentBucketFrames{0};
        std::unordered_map<uintptr_t, Source> residents;
        std::unordered_map<uintptr_t, Admission> admitted;
        std::vector<Bucket> buckets, dormantBuckets;
        std::vector<int> queuedGroups;
        void ReuseRecordBuffers(std::vector<Bucket>&) {}
        void RefreshFades() {
            for (const auto& bucket : buckets) for (const auto& source : bucket.sources) {
                const auto& live = residents.at(source.identity);
                if (live.generation != source.generation) throw std::runtime_error("Stale source retained");
            }
        }
    };
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    ~GrassBucketRenderer();
    void SetBackgroundPreparationEnabled(bool enabled);
};
METHODS
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        Require(state.preparationRequest == 0 && !state.PrepareBackground(), "Experiment did not default off");
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
        Require(state.PrepareBackground() && state.preparationPending && state.admitted.empty(), "Pending job did not preserve native fallback");
        state.residents.at(2).lifetime->alive = false;
        state.residents.erase(2); a->members = {1}; ++state.residentRevision;
        Require(state.PrepareBackground() && state.preparationSubmitted == 1, "Pending work blocked or duplicated submission");
        BS::Complete();
        Require(!nativeOwner.expired(), "Worker released a captured native owner");
        Require(state.PrepareBackground() && state.admitted.size() == 1 && state.admitted.contains(3),
            "Stale partial queue published or unaffected queue was lost");
        Require(nativeOwner.expired() && releasedOnOwner, "Native snapshot was not retired on its owner thread");
        Require(state.preparationRejectedBuckets == 1 && state.preparationSubmitted == 2, "Revision change did not coalesce into one replacement job");
        ++state.residents.at(3).lifetime->generation; ++state.residents.at(3).generation; ++state.residentRevision;
        Require(state.PrepareBackground() && !state.admitted.contains(3), "Regenerated source retained old admission");
        BS::Complete(); state.PrepareBackground();
        Require(state.admitted.size() == 1 && state.admitted.contains(1), "Mixed stale publication lost complete current queues");
        const auto published = state.preparationPublished.load();
        renderer.SetBackgroundPreparationEnabled(false);
        Require(!state.PrepareBackground() && state.preparationPending, "Disabling waited for pending work");
        BS::Complete();
        Require(!state.PrepareBackground() && !state.preparationPending && state.preparationPublished == published && state.preparationDiscarded == 1,
            "Disabled worker result was published");
        renderer.SetBackgroundPreparationEnabled(true); state.PrepareBackground(); BS::Complete(); state.PrepareBackground();
        Require(state.admitted.size() == 2 && !state.preparationPending, "Re-enabled preparation did not publish all current queues");
        const auto submitted = state.preparationSubmitted.load();
        state.PrepareBackground();
        Require(state.preparationSubmitted == submitted && BS::pools == 1, "Stable frame rebuilt or created another worker");
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
        state.PrepareBackground(); BS::Complete();
        Require(!state.PrepareBackground() && state.preparationFailed && !state.preparationPending, "Worker failure did not restore synchronous fallback");
        Require(!state.PrepareBackground(), "Failed worker retried without a new toggle");
        state.residents.at(1).groups.front().count = 1;
        renderer.SetBackgroundPreparationEnabled(false); renderer.SetBackgroundPreparationEnabled(true);
        Require(state.PrepareBackground() && !state.preparationFailed && state.preparationPending, "Fresh toggle did not recover after failure");
        {
            GrassBucketRenderer ending;
            renderer.impl.swap(ending.impl);
        }
        Require(!BS::pending, "Renderer shutdown did not drain its borrowed snapshot");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''.replace("FIELDS", fields).replace("METHODS", methods)
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        with tempfile.TemporaryDirectory(prefix="grass-preparation-") as temporary:
            directory = Path(temporary)
            cpp = directory / "driver.cpp"
            executable = directory / "driver.exe"
            cpp.write_text(driver, encoding="utf-8")
            command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX", "/DDEVBENCH_BRIDGE_ENABLED",
                       "/I" + str(ROOT / "src"), str(cpp), "/Fe:" + str(executable)]
            result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], cwd=directory, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
