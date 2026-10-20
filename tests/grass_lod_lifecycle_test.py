"""Run production LOD demand and end-of-frame retirement with counted owners."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class GrassLODLifecycleTests(unittest.TestCase):
    def test_production_retirement(self):
        source = (ROOT / "src/Features/GrassOptimizations/GrassBucketRenderer.cpp").read_text(encoding="utf-8")
        methods = "\n".join(block(source, signature) for signature in (
            "void GrassBucketRenderer::EndFrame(",
            "void GrassBucketRenderer::Impl::UpdateLODDemand(",
            "std::array<const Mesh*, 2> GrassBucketRenderer::Impl::LODMeshes("))
        driver = r'''
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Features/GrassOptimizations/GrassLODCache.h"
#include "Features/GrassOptimizations/GrassPolicy.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
unsigned failAllocation = 0;
bool failAllAllocations = false;
void* operator new(std::size_t size) {
    if (failAllAllocations || (failAllocation && !--failAllocation)) throw std::bad_alloc{};
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
bool captureLocked = false;
struct Mutex {
    void lock_shared() { captureLocked = true; }
    void unlock_shared() { captureLocked = false; }
};
void Require(bool value) { if (!value) throw std::runtime_error("LOD ownership invariant failed"); }
struct Owner {
    static inline int alive = 0;
    Owner() { ++alive; }
    ~Owner() { Require(!captureLocked); --alive; }
};
namespace RE {
    using NiSourceTexture = int;
    template<class T> using NiPointer = std::shared_ptr<T>;
}
struct Mesh {
    std::shared_ptr<Owner> owner;
    std::shared_ptr<int> texture;
    uint64_t descriptor = 0;
    uint32_t tier = 0;
    bool vertices = true, indices = true;
    Mesh() = default;
    Mesh(std::shared_ptr<Owner> value) : owner(std::move(value)) {}
    explicit operator bool() const { return bool(owner); }
};
using LODLookup = std::tuple<std::string_view, uint64_t, uintptr_t, uint32_t>;
PRODUCTION_KEY;

struct Lifetime { std::atomic_bool alive{true}; std::atomic_uint64_t generation{0}; };
struct Property {
    int* texture;
    int* GetBaseTexture() const { return texture; }
};
struct Source {
    std::string model = "test.nif";
    uint64_t descriptor = 7;
    std::shared_ptr<int> texture = std::make_shared<int>(1);
    std::shared_ptr<Property> property = std::make_shared<Property>(texture.get());
    std::shared_ptr<Lifetime> lifetime = std::make_shared<Lifetime>();
    uint64_t generation = 0;
};
struct Plan {
    std::array<Mesh, 3> meshes;
    void* layout = nullptr;
    bool prepared = false, empty = false;
    std::array<float, 152> geometry{};
    PRODUCTION_RETIRE
};
struct Bucket { Plan plan; };
namespace logger {
    unsigned warnings = 0;
    template<class... T> void warn(T&&...) { ++warnings; }
}
namespace globals::features {
    struct Feature {
        bool loaded = true;
        GrassPolicy::Settings settings;
        auto GetSettings() { return settings; }
    } grassOptimizations;
}
struct GrassBucketRenderer {
    struct Impl {
        Mutex captureMutex;
        std::map<int, Source> residents;
        uint64_t residentRevision = 0, lodRevision = UINT64_MAX;
        uint32_t lodMode = 0;
        bool failed = false;
        GrassPolicy::Settings frameSettings;
        GrassLOD::Cache<LODKey, Mesh> lodMeshes;
        std::vector<Bucket> buckets{1}, dormantBuckets{1};
        void UpdateLODDemand(const GrassPolicy::Settings&);
        std::array<const Mesh*, 2> LODMeshes(const Source&);
        static inline std::array<unsigned, 2> loadCounts{}, loadModes{};
        static inline Property* changePropertyOnLoad = nullptr;
        static inline uint32_t changePropertyTier = 0;
        static Mesh LoadLODMesh(const LODKey& key) {
            ++loadCounts[key.tier];
            if (key.tier == changePropertyTier && changePropertyOnLoad) changePropertyOnLoad->texture = nullptr;
            Mesh mesh{std::make_shared<Owner>()};
            mesh.texture = key.texture;
            mesh.descriptor = key.descriptor;
            mesh.tier = key.tier;
            switch (loadModes[key.tier]) {
            case 1: return {};
            case 2: mesh.vertices = false; break;
            case 3: mesh.indices = false; break;
            case 4: ++mesh.descriptor; break;
            case 5: mesh.texture.reset(); break;
            case 6: mesh.texture = std::make_shared<int>(2); break;
            }
            return mesh;
        }
    };
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    void EndFrame();
};
PRODUCTION_METHODS
void CheckDemandAllocationFailure() {
    auto& settings = globals::features::grassOptimizations.settings;
    settings.Enabled = settings.EnableMeshLOD = settings.EnableMidLOD = settings.EnableFarLOD = true;
    for (bool endFrame : {false, true})
        for (unsigned allocation : {0u, 1u, 2u, 3u}) {
            GrassBucketRenderer renderer;
            auto& state = *renderer.impl;
            state.frameSettings = settings;
            state.residents[1] = Source{};
            state.UpdateLODDemand(settings);
            const auto& source = state.residents[1];
            const LODKey key{source.model, source.descriptor, source.texture, 0};
            const auto load = [](const LODKey&) { return std::make_shared<Owner>(); };
            Require(state.lodMeshes.FindOrLoad(key, load) && Owner::alive == 1);
            ++state.residentRevision;
            const auto warnings = logger::warnings;
            failAllocation = allocation;
            failAllAllocations = allocation == 0;
            try {
                if (endFrame) renderer.EndFrame();
                else state.UpdateLODDemand(settings);
            } catch (...) {
                failAllAllocations = false;
                throw;
            }
            failAllAllocations = false;
            Require(!failAllocation && !captureLocked && !state.failed && Owner::alive == 0);
            Require(state.lodMeshes.Entries().empty() && state.lodRevision == UINT64_MAX);
            Require(logger::warnings == warnings + 1);
            const auto selected = state.LODMeshes(source);
            Require(!selected[0] && !selected[1]);
            state.UpdateLODDemand(settings);
            Require(state.lodRevision == state.residentRevision);
            Require(state.lodMeshes.FindOrLoad(key, load) && Owner::alive == 1);
        }
    Require(Owner::alive == 0);
}
void CheckLODResolution() {
    auto& settings = globals::features::grassOptimizations.settings;
    settings.Enabled = settings.EnableMeshLOD = true;
    for (bool midEnabled : {false, true})
        for (bool farEnabled : {false, true})
            for (unsigned midMode = 0; midMode < 7; ++midMode)
                for (unsigned farMode = 0; farMode < 7; ++farMode) {
                    settings.EnableMidLOD = midEnabled;
                    settings.EnableFarLOD = farEnabled;
                    GrassBucketRenderer renderer;
                    auto& state = *renderer.impl;
                    state.frameSettings = settings;
                    state.residents[1] = Source{};
                    const auto& source = state.residents[1];
                    state.loadCounts = {};
                    state.loadModes = {midMode, farMode};
                    renderer.EndFrame();
                    const auto selected = state.LODMeshes(source);
                    Require(bool(selected[0]) == (midEnabled && midMode == 0));
                    Require(bool(selected[1]) == (farEnabled && (farMode == 0 || midMode == 0)));
                    if (selected[0]) Require(selected[0]->tier == 0);
                    if (selected[1]) Require(selected[1]->tier == (farMode == 0 ? 1u : 0u));
                    const std::array<unsigned, 2> expectedLoads{
                        midEnabled || (farEnabled && farMode != 0) ? 1u : 0u, farEnabled ? 1u : 0u};
                    Require(state.loadCounts == expectedLoads);
                    for (unsigned frame = 0; frame < 100; ++frame)
                        Require(state.LODMeshes(source) == selected && state.loadCounts == expectedLoads);
                    for (auto mesh : selected)
                        if (mesh) Require(mesh->owner.use_count() == 1);
                    int changedTexture = 3;
                    source.property->texture = &changedTexture;
                    const auto rejected = state.LODMeshes(source);
                    Require(!rejected[0] && !rejected[1]);
                }
    Require(Owner::alive == 0);
    for (uint32_t changedTier : {0u, 1u}) {
        settings.EnableMidLOD = settings.EnableFarLOD = true;
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        state.frameSettings = settings;
        state.residents[1] = Source{};
        const auto& source = state.residents[1];
        state.loadModes = {};
        state.changePropertyOnLoad = source.property.get();
        state.changePropertyTier = changedTier;
        renderer.EndFrame();
        const auto selected = state.LODMeshes(source);
        state.changePropertyOnLoad = nullptr;
        Require(!selected[0] && !selected[1]);
    }
    Require(Owner::alive == 0);
}
int main() {
    try {
        auto& settings = globals::features::grassOptimizations.settings;
        settings.EnableMeshLOD = true;
        GrassBucketRenderer renderer;
        auto& state = *renderer.impl;
        state.residents[1] = Source{};
        state.residents[2] = state.residents[1];
        state.residents[2].lifetime = std::make_shared<Lifetime>();
        const auto keyFor = [](const Source& s, uint32_t tier = 0) {
            return LODKey{s.model, s.descriptor, s.texture, tier};
        };
        const auto key = keyFor(state.residents[1]);
        int loads = 0;
        const auto load = [&](const LODKey&) { ++loads; return std::make_shared<Owner>(); };
        Require(!state.lodMeshes.FindOrLoad(key, load));
        renderer.EndFrame();
        auto mesh = state.lodMeshes.FindOrLoad(key, load);
        Require(mesh && *mesh && loads == 1 && Owner::alive == 1);
        Require(state.lodMeshes.FindOrLoad(key, load) == mesh && loads == 1);
        const auto lookup = key.Lookup();
        for (int frame = 0; frame < 1000; ++frame)
            Require(state.lodMeshes.FindOrLoad(lookup, load) == mesh && loads == 1);
        const auto warnings = logger::warnings;
        failAllAllocations = true;
        try {
            for (int frame = 0; frame < 1000; ++frame) {
                state.UpdateLODDemand(settings);
                renderer.EndFrame();
            }
        } catch (...) {
            failAllAllocations = false;
            throw;
        }
        failAllAllocations = false;
        Require(logger::warnings == warnings && Owner::alive == 1);
        Require(state.lodMeshes.FindOrLoad(lookup, load) == mesh && loads == 1);
        auto absent = lookup;
        std::get<1>(absent) += 1;
        Require(!state.lodMeshes.FindOrLoad(absent, load) && loads == 1);
        state.buckets[0].plan.meshes[1] = *mesh;
        state.dormantBuckets[0].plan.meshes[2] = *mesh;
        auto& plan = state.buckets[0].plan;
        plan.layout = &state;
        plan.prepared = plan.empty = true;
        plan.geometry.fill(42);
        state.residents.erase(1);
        ++state.residentRevision;
        renderer.EndFrame();
        Require(Owner::alive == 1 && !state.buckets[0].plan.meshes[1] && !state.dormantBuckets[0].plan.meshes[2]);
        Require(!plan.prepared && !plan.empty && !plan.layout && plan.geometry[0] == 42 && plan.geometry.back() == 42);
        state.residents.erase(2);
        ++state.residentRevision;
        renderer.EndFrame();
        Require(Owner::alive == 0 && state.lodMeshes.Entries().empty());
        state.residents[1] = Source{};
        ++state.residentRevision;
        renderer.EndFrame();
        const auto replacement = keyFor(state.residents[1]);
        Require(state.lodMeshes.FindOrLoad(replacement, load) && Owner::alive == 1);
        settings.Enabled = false;
        renderer.EndFrame();
        Require(Owner::alive == 0);
        settings.Enabled = true;
        settings.EnableMeshLOD = false;
        renderer.EndFrame();
        Require(!state.lodMeshes.FindOrLoad(replacement, load));
        settings.EnableMeshLOD = true;
        settings.EnableMidLOD = false;
        renderer.EndFrame();
        Require(state.lodMeshes.FindOrLoad(replacement, load) && Owner::alive == 1);
        const auto farKey = keyFor(state.residents[1], 1);
        Require(state.lodMeshes.FindOrLoad(farKey, load) && Owner::alive == 2);
        settings.EnableMidLOD = true;
        settings.EnableFarLOD = false;
        renderer.EndFrame();
        Require(Owner::alive == 1 && !state.lodMeshes.FindOrLoad(farKey, load));
        ++state.residents[1].lifetime->generation;
        ++state.residentRevision;
        renderer.EndFrame();
        Require(Owner::alive == 0);
        state.residents[1].generation = 1;
        ++state.residentRevision;
        renderer.EndFrame();
        int failures = 0;
        const auto rejected = [&](const LODKey&) -> Mesh {
            auto candidate = std::make_shared<Owner>();
            ++failures;
            return {};
        };
        Require(!*state.lodMeshes.FindOrLoad(replacement, rejected) && Owner::alive == 0);
        Require(!*state.lodMeshes.FindOrLoad(replacement, rejected) && failures == 1);
        state.residents[1].model = "replacement.nif";
        ++state.residentRevision;
        renderer.EndFrame();
        Require(state.lodMeshes.Entries().empty());
        const auto newKey = keyFor(state.residents[1]);
        Require(state.lodMeshes.FindOrLoad(newKey, load) && Owner::alive == 1);
        state.residents[1].texture = std::make_shared<int>(2);
        ++state.residentRevision;
        renderer.EndFrame();
        Require(Owner::alive == 0);
        Require(state.lodMeshes.FindOrLoad(keyFor(state.residents[1]), load) && Owner::alive == 1);
        state.residents[1].lifetime->alive = false;
        ++state.residentRevision;
        renderer.EndFrame();
        Require(Owner::alive == 0);
        CheckLODResolution();
        CheckDemandAllocationFailure();
        std::cout << "Production LOD demand, shared users, frame plans, toggles and retirement passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''.replace("PRODUCTION_METHODS", methods).replace("PRODUCTION_KEY", block(source, "struct LODKey")).replace(
            "PRODUCTION_RETIRE", block(source, "void Retire("))
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        with tempfile.TemporaryDirectory(prefix="grass-lod-lifecycle-") as temporary:
            directory = Path(temporary)
            cpp = directory / "driver.cpp"
            executable = directory / "driver.exe"
            cpp.write_text(driver, encoding="utf-8")
            command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX",
                       "/I" + str(ROOT / "src"), str(cpp), "/Fe:" + str(executable)]
            result = subprocess.run(command, cwd=directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], cwd=directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
