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
    def test_authored_mesh_layout_and_draw_bindings(self):
        source = (ROOT / "src/Features/GrassOptimizations/GrassBucketRenderer.cpp").read_text(encoding="utf-8")
        asynchronous = "bool GrassBucketRenderer::Impl::LoadLODMesh(" in source
        loader = "bool" if asynchronous else "Mesh"
        methods = "\n".join(block(source, signature) for signature in (
            "bool ValidMeshDescriptor(", f"{loader} GrassBucketRenderer::Impl::LoadLODMesh(",
            "ID3D11InputLayout* GrassBucketRenderer::Impl::Layout(",
            "void GrassBucketRenderer::Impl::EmitDraws("))
        driver = r'''
#include "Features/GrassOptimizations/GrassPolicy.h"
#include "../extern/CommonLibSSE-NG/include/RE/V/VertexDesc.h"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
using UINT = unsigned;
void Require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
struct Resource {
    unsigned references = 1;
    void AddRef() { Require(references > 0, "Retained a released resource"); ++references; }
    void Release() { Require(references > 0, "Released a resource twice"); --references; }
};
namespace winrt {
    template<class T> struct com_ptr {
        T* value = nullptr;
        com_ptr() = default;
        com_ptr(const com_ptr& other) { copy_from(other.value); }
        com_ptr(com_ptr&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
        com_ptr& operator=(const com_ptr& other) { copy_from(other.value); return *this; }
        com_ptr& operator=(com_ptr&& other) noexcept {
            if (this != &other) { if (value) value->Release(); value = std::exchange(other.value, nullptr); }
            return *this;
        }
        ~com_ptr() { if (value) value->Release(); }
        void copy_from(T* next) { if (next) next->AddRef(); if (value) value->Release(); value = next; }
        T* get() const { return value; }
        T* operator->() const { return value; }
        T** put() { Require(!value, "Overwrote an owned resource"); return &value; }
        explicit operator bool() const { return value != nullptr; }
    };
}
constexpr UINT D3D11_BIND_VERTEX_BUFFER = 1, D3D11_BIND_INDEX_BUFFER = 2;
struct D3D11_BUFFER_DESC { UINT ByteWidth = 0, BindFlags = 0; };
struct ID3D11Buffer : Resource {
    D3D11_BUFFER_DESC desc;
    void GetDesc(D3D11_BUFFER_DESC* output) const { *output = desc; }
};
enum DXGI_FORMAT { DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16_UINT };
constexpr UINT D3D11_INPUT_PER_VERTEX_DATA = 0, D3D11_INPUT_PER_INSTANCE_DATA = 1;
constexpr UINT D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4;
struct D3D11_INPUT_ELEMENT_DESC {
    const char* SemanticName; UINT SemanticIndex; DXGI_FORMAT Format; UINT InputSlot, AlignedByteOffset, InputSlotClass, InstanceDataStepRate;
};
struct ID3D11InputLayout : Resource { std::array<D3D11_INPUT_ELEMENT_DESC, 8> elements; };
using ID3D11ShaderResourceView = Resource;
struct Device {
    unsigned creates = 0, names = 0;
    bool fail = false;
    std::vector<std::unique_ptr<ID3D11InputLayout>> layouts;
    int CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void*, size_t, ID3D11InputLayout** output) {
        ++creates; *output = nullptr;
        if (fail) return -1;
        Require(count == 8, "Unexpected input element count");
        auto layout = std::make_unique<ID3D11InputLayout>();
        std::copy_n(elements, count, layout->elements.begin());
        layouts.push_back(std::move(layout)); *output = layouts.back().get();
        return 0;
    }
} device;
bool FAILED(int status) { return status < 0; }
namespace logger { template<class... T> void warn(T&&...) {} }
namespace Util {
    void SetResourceName(ID3D11InputLayout* layout, const char* name) {
        Require(layout && std::strcmp(name, "GrassOptimizations::InputLayout") == 0, "Unnamed layout");
        ++device.names;
    }
}
namespace RE {
    template<class T> using NiPointer = std::shared_ptr<T>;
    struct NiPoint3 { float x = 0, y = 0, z = 0; float Length() const { return std::sqrt(x*x + y*y + z*z); } };
    struct BSTriShape {
        struct Renderer { ID3D11Buffer* vertexBuffer = nullptr; ID3D11Buffer* indexBuffer = nullptr; } renderer;
        struct Geometry { Renderer* rendererData = nullptr; BSGraphics::VertexDesc vertexDesc{}; } geometry;
        struct Counts { uint32_t triangleCount = 1, vertexCount = 3; } counts;
        struct Model { struct { NiPoint3 center; float radius = 2; } modelBound; } model;
        auto& GetGeometryRuntimeData() { return geometry; }
        auto& GetTrishapeRuntimeData() { return counts; }
        auto& GetModelData() { return model; }
    };
    struct NiNode { BSTriShape* shape = nullptr; };
    namespace BSResource { enum class ErrorCode { kNone, kFailed }; }
    namespace BSModelDB {
        struct DBTraits { struct ArgsType { bool unk8 = true, unkA = true, postProcess = true; }; };
        NiPointer<NiNode> model;
        std::string requestedPath;
        BSResource::ErrorCode Demand(const char* path, NiPointer<NiNode>& output, const DBTraits::ArgsType& args) {
            Require(!args.unk8 && !args.unkA && !args.postProcess, "LOD load enabled model processing");
            requestedPath = path; output = model;
            return model ? BSResource::ErrorCode::kNone : BSResource::ErrorCode::kFailed;
        }
    }
    struct BSShader { enum class Type { Grass }; };
}
bool Finite(const RE::NiPoint3& point) { return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z); }
RE::BSTriShape* SingleShape(RE::NiNode* node) { return node ? node->shape : nullptr; }
PRODUCTION_MESH;
struct LODKey { std::string model = "meshes/grass/authored.nif"; uint32_t tier = 0; };
struct Blob { const void* GetBufferPointer() const { return this; } size_t GetBufferSize() const { return 1; } } blob;
struct Buffer { winrt::com_ptr<ID3D11Buffer> resource; winrt::com_ptr<ID3D11ShaderResourceView> srv; };
struct Bucket {
    struct Plan { std::array<Mesh, 3> meshes; std::array<winrt::com_ptr<ID3D11InputLayout>, 3> layouts;
        struct { UINT eyes = 1; } params; UINT slot = 0; } plan;
    struct Result { UINT capacity = 8; std::shared_ptr<Buffer> output, arguments, extras; } result;
    std::shared_ptr<Buffer> records;
};
struct Context {
    struct Draw { ID3D11InputLayout* layout; ID3D11Buffer* vertices; UINT stride, argumentOffset; };
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* vertices = nullptr;
    UINT stride = 0;
    std::vector<Draw> draws;
    void VSSetShader(Resource*, void*, UINT) {}
    void PSSetShader(Resource*, void*, UINT) {}
    void VSSetConstantBuffers1(UINT, UINT, ID3D11Buffer**, const UINT*, const UINT*) {}
    void VSSetShaderResources(UINT, UINT, ID3D11ShaderResourceView* const*) {}
    void IASetPrimitiveTopology(UINT) {}
    void IASetInputLayout(ID3D11InputLayout* next) { layout = next; }
    void IASetVertexBuffers(UINT slot, UINT, ID3D11Buffer** buffers, const UINT* strides, const UINT*) {
        if (slot == 0) { vertices = *buffers; stride = *strides; }
    }
    void IASetIndexBuffer(ID3D11Buffer*, DXGI_FORMAT format, UINT) { Require(format == DXGI_FORMAT_R16_UINT, "Wrong index format"); }
    void DrawIndexedInstancedIndirect(ID3D11Buffer*, UINT offset) { draws.push_back({layout, vertices, stride, offset}); }
};
namespace GrassD3D { struct DrawState { explicit DrawState(Context*) {} }; }
namespace SIE::ShaderCache {
    enum class GrassShaderTechniques { RenderDepth = 1 };
    enum class GrassShaderFlags { AlphaTest = 16 };
    UINT GetCharacterCategoryFlag(RE::BSShader::Type) { return 32; }
}
namespace globals {
    struct State { UINT modifiedPixelDescriptor = 0; } stateStorage;
    auto* state = &stateStorage;
    namespace d3d { auto* device = &::device; }
}
PRODUCTION_CONSTANTS
struct GrassBucketRenderer { struct Impl {
    std::unordered_map<uint64_t, winrt::com_ptr<ID3D11InputLayout>> layouts;
    Blob* signature = &blob;
    std::shared_ptr<Context> context = std::make_shared<Context>();
    std::array<winrt::com_ptr<Resource>, 2> drawVertexShaders;
    std::array<winrt::com_ptr<Resource>, 8> drawPixelShaders;
    std::vector<std::shared_ptr<Buffer>> frameConstants{std::make_shared<Buffer>()};
    void CompileSignature() {}
    ID3D11InputLayout* Layout(uint64_t);
    LOADER_DECLARATION
    void EmitDraws(const Bucket&);
}; };
PRODUCTION_METHODS
bool Load(const LODKey& key, Mesh& mesh) { LOADER_CALL }
uint64_t Descriptor(UINT stride, bool full, UINT uv, UINT normal, UINT color) {
    using V = RE::BSGraphics::Vertex;
    uint64_t value = uint64_t(stride / 4) | (uint64_t(V::VF_VERTEX | V::VF_UV | V::VF_NORMAL | V::VF_COLORS) << 44);
    auto descriptor = std::bit_cast<RE::BSGraphics::VertexDesc>(value);
    if (full) descriptor.SetFlag(V::VF_FULLPREC);
    descriptor.SetAttributeOffset(V::VA_TEXCOORD0, uv);
    descriptor.SetAttributeOffset(V::VA_NORMAL, normal);
    descriptor.SetAttributeOffset(V::VA_COLOR, color);
    return std::bit_cast<uint64_t>(descriptor);
}
int main() {
    try {
        GrassBucketRenderer::Impl state;
        RE::BSTriShape shape;
        ID3D11Buffer halfVertices, fullVertices, paddedVertices, indices;
        halfVertices.desc = {60, D3D11_BIND_VERTEX_BUFFER};
        fullVertices.desc = {84, D3D11_BIND_VERTEX_BUFFER};
        indices.desc = {6, D3D11_BIND_INDEX_BUFFER};
        shape.geometry.rendererData = &shape.renderer;
        shape.renderer.indexBuffer = &indices;
        RE::BSModelDB::model = std::make_shared<RE::NiNode>();
        RE::BSModelDB::model->shape = &shape;
        const auto half = Descriptor(20, false, 8, 12, 16);
        const auto full = Descriptor(28, true, 16, 20, 24);
        std::array<Mesh, 2> loaded;
        LODKey key;
        for (UINT tier = 0; tier < loaded.size(); ++tier) {
            key.tier = tier;
            shape.geometry.vertexDesc = std::bit_cast<RE::BSGraphics::VertexDesc>(tier ? full : half);
            shape.renderer.vertexBuffer = tier ? &fullVertices : &halfVertices;
            Require(Load(key, loaded[tier]), "Compatible geometry-only LOD rejected");
            Require(RE::BSModelDB::requestedPath == (tier ? "LOD\\Grass\\authored_LOD1.nif" : "LOD\\Grass\\authored_LOD0.nif"), "Wrong authored LOD path");
            const auto* layout = state.Layout(loaded[tier].descriptor);
            Require(layout && layout->elements[0].Format == (tier ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT), "Wrong position precision");
            Require(layout->elements[1].AlignedByteOffset == (tier ? 16u : 8u) &&
                layout->elements[2].AlignedByteOffset == (tier ? 20u : 12u) && layout->elements[3].AlignedByteOffset == (tier ? 24u : 16u), "Wrong authored attribute offsets");
            Require(state.Layout(loaded[tier].descriptor) == layout, "Layout cache did not retain identity");
        }
        Require(device.creates == 2 && device.names == 2, "Layout cache duplicated or failed to name resources");
        const auto* inferred = state.Layout(Descriptor(28, false, 16, 20, 24));
        Require(inferred && inferred->elements[0].Format == DXGI_FORMAT_R32G32B32A32_FLOAT, "Native full-position heuristic changed");
        for (const auto bad : {Descriptor(28, true, 12, 20, 24), Descriptor(28, true, 16, 20, 20), Descriptor(24, true, 16, 20, 24)}) {
            shape.geometry.vertexDesc = std::bit_cast<RE::BSGraphics::VertexDesc>(bad);
            Mesh rejected;
            const auto creates = device.creates;
            Require(!Load(key, rejected) && !state.Layout(bad) && device.creates == creates, "Invalid attributes reached draw layout creation");
        }
        shape.geometry.vertexDesc = std::bit_cast<RE::BSGraphics::VertexDesc>(full);
        for (UINT failure = 0; failure < 4; ++failure) {
            fullVertices.desc = {84, D3D11_BIND_VERTEX_BUFFER}; indices.desc = {6, D3D11_BIND_INDEX_BUFFER};
            if (failure == 0) --fullVertices.desc.ByteWidth;
            if (failure == 1) --indices.desc.ByteWidth;
            if (failure == 2) fullVertices.desc.BindFlags = 0;
            if (failure == 3) indices.desc.BindFlags = 0;
            Mesh rejected; Require(!Load(key, rejected), "Invalid LOD buffers accepted");
        }
        fullVertices.desc = {84, D3D11_BIND_VERTEX_BUFFER}; indices.desc = {6, D3D11_BIND_INDEX_BUFFER};
        paddedVertices.desc = {96, D3D11_BIND_VERTEX_BUFFER};
        shape.renderer.vertexBuffer = &paddedVertices;
        shape.geometry.vertexDesc = std::bit_cast<RE::BSGraphics::VertexDesc>(Descriptor(32, true, 16, 24, 28));
        Mesh padded;
        Require(Load(key, padded), "Padded authored LOD rejected");
        const auto* paddedLayout = state.Layout(padded.descriptor);
        Require(paddedLayout && paddedLayout->elements[2].AlignedByteOffset == 24 && paddedLayout->elements[3].AlignedByteOffset == 28,
            "Padded authored layout lost its attribute offsets");
        const auto unsupported = Descriptor(36, true, 16, 24, 28);
        device.fail = true;
        Require(!state.Layout(unsupported), "Failed optional layout accepted");
        const auto attempts = device.creates;
        device.fail = false;
        Require(!state.Layout(unsupported) && device.creates == attempts && state.Layout(loaded[0].descriptor), "Optional layout failure poisoned base or retried every draw");
        Bucket bucket;
        bucket.plan.meshes = {loaded[0], loaded[1], padded};
        for (UINT tier = 0; tier < 3; ++tier) bucket.plan.layouts[tier].copy_from(state.Layout(bucket.plan.meshes[tier].descriptor));
        bucket.result.output = bucket.result.arguments = bucket.result.extras = bucket.records = std::make_shared<Buffer>();
        const std::array<UINT, 3> expectedStrides{20, 28, 32};
        for (const UINT eyes : {1u, 2u}) {
            bucket.plan.params.eyes = eyes; state.context->draws.clear(); state.EmitDraws(bucket);
            Require(state.context->draws.size() == 3 * eyes, "Wrong per-eye tier draw count");
            for (UINT tier = 0; tier < 3; ++tier) for (UINT eye = 0; eye < eyes; ++eye) {
                const auto& draw = state.context->draws[tier * eyes + eye];
                Require(draw.layout == bucket.plan.layouts[tier].get() && draw.vertices == bucket.plan.meshes[tier].vertices.get() &&
                    draw.stride == expectedStrides[tier] && draw.argumentOffset == (tier * eyes + eye) * 32 + 12, "Draw mixed a tier layout, stride or eye slot");
            }
        }
        bucket.plan.meshes[1] = {}; bucket.plan.layouts[1] = {};
        state.context->draws.clear(); state.EmitDraws(bucket);
        Require(state.context->draws.size() == 4 && state.context->draws[2].argumentOffset == 140, "Missing optional mesh shifted surviving tier slots");
        std::cout << "Authored LOD loading, layouts, failures and per-eye draw bindings passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''.replace("PRODUCTION_METHODS", methods).replace("PRODUCTION_MESH", block(source, "struct Mesh")).replace(
            "PRODUCTION_CONSTANTS", "\n".join(line.strip() for line in source.splitlines() if
                                               "inline constexpr uint32_t kFrameSlotBytes" in line or
                                               "inline constexpr uint32_t kParametersOffset" in line)).replace(
            "LOADER_DECLARATION", "static bool LoadLODMesh(const LODKey&, Mesh&);" if asynchronous else
            "static Mesh LoadLODMesh(const LODKey&);").replace(
            "LOADER_CALL", "return GrassBucketRenderer::Impl::LoadLODMesh(key, mesh);" if asynchronous else
            "mesh = GrassBucketRenderer::Impl::LoadLODMesh(key); return mesh.vertices && mesh.indices;")
        self._run_driver(driver)

    def test_production_retirement(self):
        source = (ROOT / "src/Features/GrassOptimizations/GrassBucketRenderer.cpp").read_text(encoding="utf-8")
        methods = "\n".join(block(source, signature) for signature in (
            "void GrassBucketRenderer::Impl::ClearDrawContext(",
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
    void lock() { captureLocked = true; }
    void unlock() { captureLocked = false; }
    void lock_shared() { captureLocked = true; }
    void unlock_shared() { captureLocked = false; }
};
void Require(bool value) { if (!value) throw std::runtime_error("LOD ownership invariant failed"); }
struct Owner {
    static inline int alive = 0;
    Owner() { ++alive; }
    ~Owner() { Require(!captureLocked); --alive; }
};
using QueuedGroup = Owner;
namespace RE {
    using NiSourceTexture = int;
    using BSMultiStreamInstanceTriShape = int;
    using BSRenderPass = int;
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
    std::array<void*, 3> layouts{};
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
        RE::BSMultiStreamInstanceTriShape* current = nullptr;
        RE::BSRenderPass* currentSetupPass = nullptr;
        bool currentMainPass = false, currentDrawObserved = false;
        void ClearDrawContext() noexcept;
        Mutex captureMutex;
        std::vector<std::shared_ptr<const QueuedGroup>> queuedGroups;
        std::map<int, Source> residents;
        uint64_t residentRevision = 0, lodRevision = UINT64_MAX;
        uint32_t lodMode = 0;
        bool failed = false;
        GrassPolicy::Settings frameSettings;
        GrassLOD::Cache<LODKey, Mesh> lodMeshes;
        std::vector<Bucket> buckets{1}, dormantBuckets{1};
        void UpdateLODDemand(const GrassPolicy::Settings&);
        void EndBackgroundPreparation(const GrassPolicy::Settings&) {}
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
                    const bool midValid = midMode == 0 || midMode >= 4;
                    const bool farValid = farMode == 0 || farMode >= 4;
                    Require(bool(selected[0]) == (midEnabled && midValid));
                    Require(bool(selected[1]) == (farEnabled && (farValid || midValid)));
                    if (selected[0]) Require(selected[0]->tier == 0);
                    if (selected[1]) Require(selected[1]->tier == (farValid ? 1u : 0u));
                    const std::array<unsigned, 2> expectedLoads{
                        midEnabled || (farEnabled && !farValid) ? 1u : 0u, farEnabled ? 1u : 0u};
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
        {
            GrassBucketRenderer renderer;
            renderer.impl->queuedGroups.push_back(std::make_shared<QueuedGroup>());
            Require(Owner::alive == 1);
            renderer.EndFrame();
            Require(Owner::alive == 0 && renderer.impl->queuedGroups.empty());
        }
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
        int activeGeometry = 0, activePass = 0;
        state.current = &activeGeometry;
        state.currentSetupPass = &activePass;
        state.currentMainPass = state.currentDrawObserved = true;
        renderer.EndFrame();
        Require(!state.current && !state.currentSetupPass && !state.currentMainPass && !state.currentDrawObserved);
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
        plan.layouts.fill(&state);
        plan.prepared = plan.empty = true;
        plan.geometry.fill(42);
        state.residents.erase(1);
        ++state.residentRevision;
        renderer.EndFrame();
        Require(Owner::alive == 1 && !state.buckets[0].plan.meshes[1] && !state.dormantBuckets[0].plan.meshes[2]);
        Require(!plan.prepared && !plan.empty && plan.layouts == std::array<void*, 3>{} && plan.geometry[0] == 42 && plan.geometry.back() == 42);
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
