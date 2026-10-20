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
            "void GrassBucketRenderer::Impl::UpdateLODDemand("))
        driver = r'''
#include "Features/GrassOptimizations/GrassLODCache.h"
#include "Features/GrassOptimizations/GrassPolicy.h"
#include <atomic>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
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
using Mesh = std::shared_ptr<Owner>;
namespace RE {
    using NiSourceTexture = int;
    template<class T> using NiPointer = std::shared_ptr<T>;
}
using LODLookup = std::tuple<std::string_view, uint64_t, uintptr_t, uint32_t>;
PRODUCTION_KEY;

struct Lifetime { std::atomic_bool alive{true}; std::atomic_uint64_t generation{0}; };
struct Source {
    std::string model = "test.nif";
    uint64_t descriptor = 7;
    std::shared_ptr<int> texture = std::make_shared<int>(1);
    std::shared_ptr<Lifetime> lifetime = std::make_shared<Lifetime>();
    uint64_t generation = 0;
};
struct Plan { std::array<Mesh, 3> meshes; };
struct Bucket { Plan plan; };
namespace logger { template<class... T> void warn(T&&...) {} }
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
        GrassLOD::Cache<LODKey, Mesh> lodMeshes;
        std::vector<Bucket> buckets{1}, dormantBuckets{1};
        void UpdateLODDemand(const GrassPolicy::Settings&);
    };
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    void EndFrame();
};
PRODUCTION_METHODS
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
        auto absent = lookup;
        std::get<1>(absent) += 1;
        Require(!state.lodMeshes.FindOrLoad(absent, load) && loads == 1);
        state.buckets[0].plan.meshes[1] = *mesh;
        state.dormantBuckets[0].plan.meshes[2] = *mesh;
        state.residents.erase(1);
        ++state.residentRevision;
        renderer.EndFrame();
        Require(Owner::alive == 1 && !state.buckets[0].plan.meshes[1] && !state.dormantBuckets[0].plan.meshes[2]);
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
        const auto far = keyFor(state.residents[1], 1);
        Require(state.lodMeshes.FindOrLoad(far, load) && Owner::alive == 2);
        settings.EnableMidLOD = true;
        settings.EnableFarLOD = false;
        renderer.EndFrame();
        Require(Owner::alive == 1 && !state.lodMeshes.FindOrLoad(far, load));
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
        std::cout << "Production LOD demand, shared users, frame plans, toggles and retirement passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''.replace("PRODUCTION_METHODS", methods).replace("PRODUCTION_KEY", block(source, "struct LODKey"))
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
