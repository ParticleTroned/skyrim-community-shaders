"""Exercise the production visibility fast path against native group mutations."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class GrassCaptureLifecycleTests(unittest.TestCase):
    def compile_and_run(self, driver, devbench):
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        with tempfile.TemporaryDirectory(prefix="grass-capture-lifecycle-") as temporary:
            directory = Path(temporary)
            cpp = directory / "driver.cpp"
            executable = directory / "driver.exe"
            cpp.write_text(driver, encoding="utf-8")
            command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX",
                       "/I" + str(ROOT / "src"), str(cpp), "/Fe:" + str(executable)]
            if devbench:
                command.append("/DDEVBENCH_BRIDGE_ENABLED")
            result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return subprocess.run([str(executable)], cwd=directory, capture_output=True, text=True, timeout=10)

    def test_cached_geometry_ignores_generation_metadata(self):
        source = (ROOT / "src/Features/GrassOptimizations/GrassBucketRenderer.cpp").read_text(encoding="utf-8")
        runtime = (ROOT / "src/Features/GrassOptimizations/GrassRuntime.h").read_text(encoding="utf-8")
        capture = block(source, "bool GrassBucketRenderer::CaptureVisible(")
        gate = capture[capture.index("auto& geometry ="):capture.index("auto property =")]
        gate = gate.replace("return GRASS_CAPTURE_REJECT(Geometry);", "return false;")
        buffers = capture[capture.index("const auto bytes ="):capture.index("const auto data =")]
        width_gate = block(buffers, "if (!GrassRuntime::MatchesInstanceBytes(")
        helpers = "\n".join(block(runtime, signature) for signature in (
            "inline bool HasInstanceData(", "inline bool MatchesInstanceBytes("))
        geometry = block(source, "bool CurrentGeometry(")
        driver = r'''
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "Features/GrassOptimizations/GrassPolicy.h"
constexpr uint32_t D3D11_RESOURCE_MISC_BUFFER_STRUCTURED = 0x40;
struct D3D11_BUFFER_DESC { uint32_t ByteWidth = 64, MiscFlags = 0; };
struct ID3D11Buffer {
    D3D11_BUFFER_DESC desc;
    void GetDesc(D3D11_BUFFER_DESC* destination) const { *destination = desc; }
};
ID3D11Buffer vertices, indices;
struct BufferHandle {
    ID3D11Buffer* pointer;
    auto get() const { return pointer; }
    auto operator->() const { return pointer; }
    void copy_from(ID3D11Buffer* source) { pointer = source; }
};
namespace RE {
    struct NiRTTI {};
    const NiRTTI grassRTTI;
    constexpr uintptr_t NiRTTI_BSGrassShaderProperty = 1;
    namespace BSGraphics {
        struct VertexBuffer { ID3D11Buffer* buffer; const void* m_data; uint64_t byteWidth; };
    }
    struct Property { const NiRTTI* GetRTTI() const { return &grassRTTI; } };
    struct BSMultiStreamInstanceTriShape {
        struct Runtime { uint32_t instanceSize = 0, instanceCount = 0; float renderDistance = 8000; } runtime;
        struct RendererData { void* vertexBuffer = &vertices; void* indexBuffer = &indices; } meshData;
        struct Geometry {
            std::shared_ptr<Property> shaderProperty = std::make_shared<Property>();
            RendererData* rendererData = nullptr;
            uint64_t vertexDesc = 1;
        } geometry;
        struct Mesh { uint32_t triangleCount = 12, vertexCount = 16; } mesh;
        struct Bound { std::array<float, 3> center{}; float radius = 30; };
        struct Model { Bound modelBound; } model;
        struct Transform { bool valid = true; std::array<float, 3> translate{}; } world;
        BSMultiStreamInstanceTriShape() { geometry.rendererData = &meshData; }
        auto& GetGeometryRuntimeData() { return geometry; }
        auto& GetMultiStreamTrishapeRuntimeData() { return runtime; }
        auto& GetTrishapeRuntimeData() { return mesh; }
        auto& GetModelData() { return model; }
    };
}
namespace GrassRuntime { BUFFER_HELPERS }
namespace REL {
    template<class T> struct Relocation {
        explicit Relocation(uintptr_t) {}
        T get() const { return &RE::grassRTTI; }
    };
}
bool Identity(const RE::BSMultiStreamInstanceTriShape::Transform& transform) { return transform.valid; }
struct Source {
    RE::BSMultiStreamInstanceTriShape* identity;
    std::shared_ptr<RE::Property> property;
    RE::BSMultiStreamInstanceTriShape::RendererData* mesh;
    BufferHandle vertices{&::vertices}, indices{&::indices};
    uint64_t descriptor = 1;
    uint32_t triangles = 12, vertexCount = 16;
    std::array<float, 3> origin{};
    RE::BSMultiStreamInstanceTriShape::Bound bound;
    float renderDistance = 8000;
};
bool CaptureGeometry(RE::BSMultiStreamInstanceTriShape* shape) {
    CAPTURE_GATE
    static_cast<void>(runtime);
    return true;
}
CURRENT_GEOMETRY
bool CaptureBuffer(RE::BSGraphics::VertexBuffer& buffer, uint32_t count, uint64_t instances) {
    struct Group { RE::BSGraphics::VertexBuffer* vertexBuffer; } storage{&buffer};
    auto* group = &storage;
    struct Snapshot { uint32_t count; BufferHandle buffer; } snapshot{count, {nullptr}};
#define GRASS_CAPTURE_REJECT(reason) false
    BUFFER_GUARDS
    }
#undef GRASS_CAPTURE_REJECT
    return true;
}
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        RE::BSMultiStreamInstanceTriShape shape;
        Source source{&shape, shape.geometry.shaderProperty, &shape.meshData};
        for (uint32_t size : {0u, 1u, 8u, 16u, 0xccccccccu}) {
            shape.runtime.instanceSize = size;
            for (uint32_t count : {0u, 12345u}) {
                shape.runtime.instanceCount = count;
                Require(CaptureGeometry(&shape), "Cached geometry depended on generation-only instance size");
                Require(CurrentGeometry(source), "Cached reuse depended on generation-only instance size");
            }
        }
        shape.runtime.renderDistance += 1;
        Require(!CurrentGeometry(source), "Changed native render distance retained stale geometry");
        shape.runtime.renderDistance = source.renderDistance;
        ++shape.mesh.triangleCount;
        Require(!CurrentGeometry(source), "Changed mesh retained stale geometry");
        shape.mesh.triangleCount = source.triangles;
        shape.world.valid = false;
        Require(!CaptureGeometry(&shape) && !CurrentGeometry(source), "Unsupported transform was admitted");
        shape.world.valid = true;
        shape.geometry.rendererData = nullptr;
        Require(!CaptureGeometry(&shape) && !CurrentGeometry(source), "Missing geometry was admitted");
        std::array<uint8_t, 64> records{};
        ID3D11Buffer gpu;
        for (bool gpuBacked : {false, true}) {
            for (uint64_t padding : {0ull, 0xdeadbeef00000000ull}) {
                RE::BSGraphics::VertexBuffer buffer{gpuBacked ? &gpu : nullptr,
                    gpuBacked ? nullptr : records.data(), padding | 64};
                Require(CaptureBuffer(buffer, 2, 2), "Valid cached CPU/GPU group was rejected");
                for (uint32_t bytes : {0u, 32u, 48u, 63u, 65u, 128u}) {
                    buffer.byteWidth = padding | bytes;
                    Require(!CaptureBuffer(buffer, 2, 2), "Malformed cached record layout was admitted");
                }
                buffer.byteWidth = padding | 64;
                Require(!CaptureBuffer(buffer, 0, 0), "Empty cached group was admitted");
                Require(!CaptureBuffer(buffer, 2, uint64_t(GrassPolicy::kMaxBatchInstances) + 1),
                    "Oversized cached batch was admitted");
                if (gpuBacked) {
                    gpu.desc.ByteWidth = 63;
                    Require(!CaptureBuffer(buffer, 2, 2), "Undersized GPU buffer was admitted");
                    gpu.desc.ByteWidth = 64;
                    gpu.desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
                    Require(!CaptureBuffer(buffer, 2, 2), "Unsupported structured GPU buffer was admitted");
                    gpu.desc.MiscFlags = 0;
                } else {
                    buffer.m_data = nullptr;
                    Require(!CaptureBuffer(buffer, 2, 2), "Missing CPU instance data was admitted");
                }
            }
        }
        std::cout << "Cached and generated geometry admission, reuse and rejection passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''
        for devbench in (False, True):
            for negative in (None, "capture", "reuse", "buffer"):
                capture_gate = gate
                current = geometry
                buffer_guards = buffers
                if negative == "capture":
                    capture_gate += "if (runtime.instanceSize != 16) return false;\n"
                elif negative == "reuse":
                    current = current.replace("source.renderDistance == runtime.renderDistance",
                                              "runtime.instanceSize == 16 && source.renderDistance == runtime.renderDistance")
                elif negative == "buffer":
                    buffer_guards = buffer_guards.replace(width_gate, "")
                code = driver.replace("CAPTURE_GATE", capture_gate).replace("CURRENT_GEOMETRY", current)
                code = code.replace("BUFFER_GUARDS", buffer_guards).replace("BUFFER_HELPERS", helpers)
                with self.subTest(devbench=devbench, negative=negative):
                    result = self.compile_and_run(code, devbench)
                    self.assertEqual(result.returncode, int(negative is not None), result.stdout + result.stderr)
                    if negative:
                        self.assertIn("Malformed cached record layout" if negative == "buffer"
                                      else "generation-only instance size", result.stderr)

    def test_visibility_recapture(self):
        source = (ROOT / "src/Features/GrassOptimizations/GrassBucketRenderer.cpp").read_text(encoding="utf-8")
        runtime = (ROOT / "src/Features/GrassOptimizations/GrassRuntime.h").read_text(encoding="utf-8")
        methods = "\n".join(block(source, signature) for signature in (
            "bool CurrentGroups(", "bool GrassBucketRenderer::QueueVisible("))
        layout = block(runtime, "struct GroupLayout") + ";\n" + block(runtime, "inline GroupLayout CaptureGroupLayout(")
        driver = r'''
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <vector>
namespace RE {
    struct BSMultiStreamInstanceTriShape {
        struct MULTISTREAM_TRISHAPE_RUNTIME_DATA {
            struct Groups {
                const void* storage = nullptr;
                uint32_t count = 0;
                const void* data() const { return storage; }
                uint32_t size() const { return count; }
            } instanceGroups;
            uint32_t activeGroupCount = 0, instanceCount = 0;
        } runtime;
        const auto& GetMultiStreamTrishapeRuntimeData() const { return runtime; }
    };
    struct NiCullingProcess {
        unsigned appended = 0;
        void AppendVirtual(BSMultiStreamInstanceTriShape&, int32_t) { ++appended; }
    };
}
namespace GrassRuntime { PRODUCTION_LAYOUT }
struct Lifetime {
    std::atomic_bool alive{true};
    std::atomic_uint64_t generation{0};
    std::atomic_uint32_t observedFrame{0};
};
struct QueueGroup {
    bool batching = true;
    std::atomic_bool compatible{true};
    std::vector<RE::BSMultiStreamInstanceTriShape*> members;
    uint32_t instances = 1, slices = 1;
    bool claimed = false;
    bool Claim(uint32_t, RE::BSMultiStreamInstanceTriShape*) { return !std::exchange(claimed, true); }
};
struct Source {
    RE::BSMultiStreamInstanceTriShape* identity = nullptr;
    std::shared_ptr<QueueGroup> queue = std::make_shared<QueueGroup>();
    std::shared_ptr<Lifetime> lifetime = std::make_shared<Lifetime>();
    uint64_t generation = 0;
    struct Group { uint32_t count = 1; };
    std::vector<Group> groups{1};
    GrassRuntime::GroupLayout groupLayout;
};
bool CurrentGeometry(const Source&) { return true; }
bool CurrentContract(const Source&) { return true; }
namespace globals {
    struct State { std::atomic_uint32_t frameCountAtomic{1}; } stateStorage;
    State* state = &stateStorage;
    namespace features {
        struct Feature { bool IsCrossCellBatchingEnabled() { return true; } } grassOptimizations;
    }
}
struct GrassBucketRenderer {
    struct Impl {
        std::shared_mutex captureMutex;
        std::map<RE::BSMultiStreamInstanceTriShape*, Source> residents;
        uint64_t residentRevision = 0;
        std::atomic_bool diagnostics{true};
        unsigned cachedSources = 0, nativeVisibilityBypassed = 0, queuedRepresentatives = 0, queueDuplicates = 0;
        void AssignQueue(Source& source) {
            source.queue = std::make_shared<QueueGroup>();
            source.queue->members.push_back(source.identity);
        }
    };
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    unsigned captures = 0;
    bool captureSucceeds = false;
    bool CaptureVisible(RE::BSMultiStreamInstanceTriShape* shape, bool) {
        ++captures;
        if (captureSucceeds) {
            auto& cached = impl->residents.at(shape);
            cached.groupLayout = GrassRuntime::CaptureGroupLayout(shape->runtime);
            cached.generation = cached.lifetime->generation;
        }
        return captureSucceeds;
    }
    bool QueueVisible(RE::BSMultiStreamInstanceTriShape*, RE::NiCullingProcess*, int32_t);
};
PRODUCTION_METHODS
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        for (unsigned mutation = 0; mutation < 6; ++mutation) {
            GrassBucketRenderer renderer;
            RE::BSMultiStreamInstanceTriShape shape;
            int originalStorage, replacementStorage;
            shape.runtime.instanceGroups = {&originalStorage, 2};
            shape.runtime.activeGroupCount = 1;
            auto& cached = renderer.impl->residents[&shape];
            cached.identity = &shape;
            cached.groupLayout = GrassRuntime::CaptureGroupLayout(shape.runtime);
            cached.queue->members = {&shape};
            RE::NiCullingProcess process;
            Require(renderer.QueueVisible(&shape, &process, 0) && renderer.captures == 0 && process.appended == 1,
                "An unchanged snapshot should queue without recapture");
            shape.runtime.instanceCount = 999;
            Require(renderer.QueueVisible(&shape, &process, 0) && renderer.captures == 0 && process.appended == 1,
                "Generated-only instance totals must not trigger recapture or duplicate representatives");
            switch (mutation) {
            case 0: ++shape.runtime.activeGroupCount; break;
            case 1: --shape.runtime.activeGroupCount; break;
            case 2: ++shape.runtime.instanceGroups.count; break;
            case 3: --shape.runtime.instanceGroups.count; break;
            case 4: shape.runtime.instanceGroups.storage = &replacementStorage; break;
            case 5: ++cached.lifetime->generation; break;
            }
            Require(!renderer.QueueVisible(&shape, &process, 0) && renderer.captures == 1,
                "Changed native groups must attempt recapture and retain native rendering when unavailable");
            Require(process.appended == 1, "Failed recapture queued an incomplete optimized representative");
            renderer.captureSucceeds = true;
            cached.queue->claimed = false;
            Require(renderer.QueueVisible(&shape, &process, 0) && renderer.captures == 2 && process.appended == 2,
                "A successfully recaptured snapshot should become eligible again");
            for (unsigned frame = 0; frame < 1000; ++frame)
                Require(renderer.QueueVisible(&shape, &process, 0) && renderer.captures == 2,
                    "Stable visibility repeated capture work");
        }
        std::cout << "Production visibility recapture, native fallback and stable reuse passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''.replace("PRODUCTION_LAYOUT", layout).replace("PRODUCTION_METHODS", methods)
        for devbench in (False, True):
            result = self.compile_and_run(driver, devbench)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
