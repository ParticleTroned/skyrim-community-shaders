"""Exercise production water deduplication against distinct and aliased surfaces.

Native containers and mesh owners are observable doubles. The full production
comparison, selection and removal loop are compiled together without rewriting.
"""

from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

from extract_adaptive_balance_toggle import between, function


ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r'''
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <climits>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "RE/N/NiTransform.h"
#include "RE/V/VertexDesc.h"
#include "REX/REX/EnumSet.h"
namespace SKSE::stl {
template<class F> struct scope_exit {
    F fn;
    explicit scope_exit(F action) : fn(std::move(action)) {}
    ~scope_exit() { fn(); }
};
}
namespace RE {
struct RefObject {
    static inline unsigned live = 0;
    unsigned refs = 0;
    RefObject() { ++live; }
    virtual ~RefObject() { --live; }
    void IncRef() { ++refs; }
    void DecRef() { if (--refs == 0) delete this; }
};
template<class T> class NiPointer {
    T* value = nullptr;
public:
    NiPointer() = default;
    explicit NiPointer(T* ptr) : value(ptr) { if (value) value->IncRef(); }
    NiPointer(const NiPointer& other) : NiPointer(other.value) {}
    NiPointer(NiPointer&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    NiPointer& operator=(NiPointer other) { std::swap(value, other.value); return *this; }
    ~NiPointer() { if (value) value->DecRef(); }
    T* get() const { return value; }
    T* operator->() const { return value; }
    T& operator*() const { return *value; }
    explicit operator bool() const { return value != nullptr; }
    bool operator==(const NiPointer& rhs) const { return value == rhs.value; }
};
struct NiNode;
struct NiAVObject : RefObject {
    NiTransform world;
    NiNode* parent = nullptr;
    bool culled = false;
    void SetAppCulled(bool value) { culled = value; }
};
struct NiNode : NiAVObject {};
struct BSGeometry : NiAVObject {
    enum class Type { kTriShape, kDynamicTriShape };
    REX::EnumSet<Type, uint8_t> type = Type::kTriShape;
    struct Geometry {
        void* rendererData = nullptr;
        NiPointer<RefObject> shaderProperty, alphaProperty, skinInstance;
        BSGraphics::VertexDesc vertexDesc{};
    } geometry;
    const Geometry& GetGeometryRuntimeData() const { return geometry; }
    const auto& GetType() const { return type; }
};
struct BSTriShape : BSGeometry {
    struct Mesh { uint16_t vertexCount = 4, triangleCount = 2; } mesh;
    const Mesh& GetTrishapeRuntimeData() const { return mesh; }
};
struct TESWaterObject : RefObject {
    NiPointer<BSTriShape> shape;
    void* waterType = nullptr;
    struct { NiPoint3 normal = {0,0,1}; float constant = 0; } plane;
    uint8_t flags = 0;
};
struct TESWaterSystem {
    std::vector<NiPointer<TESWaterObject>> waterObjects;
    std::vector<BSTriShape*> removed;
    void RemoveWater(BSTriShape* shape) {
        assert(std::find(removed.begin(), removed.end(), shape) == removed.end());
        removed.push_back(shape);
        std::erase_if(waterObjects, [shape](const auto& object) { return object->shape.get() == shape; });
    }
};
}
'''

CHECKS = r'''
struct Fixture {
    int meshData = 1, otherMeshData = 2, waterType = 3, otherWaterType = 4;
    RE::NiNode root, otherRoot;
    RE::NiPointer<RE::RefObject> property{new RE::RefObject};
    RE::NiPointer<RE::BSTriShape> first{new RE::BSTriShape}, second{new RE::BSTriShape};
    RE::NiPointer<RE::TESWaterObject> firstWater{new RE::TESWaterObject}, secondWater{new RE::TESWaterObject};
    RE::TESWaterSystem system;
    Fixture() {
        first->geometry.rendererData = second->geometry.rendererData = &meshData;
        first->geometry.shaderProperty = second->geometry.shaderProperty = property;
        first->parent = second->parent = &root;
        firstWater->shape = first; secondWater->shape = second;
        firstWater->waterType = secondWater->waterType = &waterType;
        system.waterObjects = {firstWater, secondWater};
    }
    void PreserveBoth(bool haveRoot = true) {
        RemoveDuplicateWaterSystemObjects(&system, haveRoot ? &root : nullptr);
        assert(system.removed.empty() && system.waterObjects.size() == 2);
        assert(!first->culled && !second->culled);
    }
};
int main() {
    RemoveDuplicateWaterSystemObjects(nullptr, nullptr);
    { Fixture f; f.PreserveBoth(false); }
    { Fixture f; f.second->geometry.rendererData = &f.otherMeshData; f.PreserveBoth(); }
    { Fixture f; f.secondWater->waterType = &f.otherWaterType; f.PreserveBoth(); }
    { Fixture f; f.second->world.rotate.entry[0][0] = -1; f.PreserveBoth(); }
    { Fixture f; f.second->world.translate.x = 0.1f; f.PreserveBoth(); }
    { Fixture f; f.second->world.scale = 1.0001f; f.PreserveBoth(); }
    { Fixture f; f.second->geometry.rendererData = nullptr; f.PreserveBoth(); }
    { Fixture f; f.second->geometry.shaderProperty = RE::NiPointer<RE::RefObject>(new RE::RefObject); f.PreserveBoth(); }
    { Fixture f; f.second->geometry.alphaProperty = RE::NiPointer<RE::RefObject>(new RE::RefObject); f.PreserveBoth(); }
    { Fixture f; f.second->geometry.skinInstance = RE::NiPointer<RE::RefObject>(new RE::RefObject); f.PreserveBoth(); }
    { Fixture f; f.second->geometry.vertexDesc.SetFlag(RE::BSGraphics::Vertex::VF_UV); f.PreserveBoth(); }
    { Fixture f; ++f.second->mesh.vertexCount; f.PreserveBoth(); }
    { Fixture f; ++f.second->mesh.triangleCount; f.PreserveBoth(); }
    { Fixture f; f.second->type = RE::BSGeometry::Type::kDynamicTriShape; f.PreserveBoth(); }
    { Fixture f; ++f.secondWater->flags; f.PreserveBoth(); }
    { Fixture f; f.secondWater->plane.normal.x = 1; f.PreserveBoth(); }
    { Fixture f; f.secondWater->plane.constant = 1; f.PreserveBoth(); }
    { Fixture f; f.first->parent = f.second->parent = &f.otherRoot; f.PreserveBoth(); }
    { Fixture f; f.secondWater->shape = f.first; f.PreserveBoth(); }
    { Fixture f; f.second->world.translate.x = std::numeric_limits<float>::quiet_NaN(); f.PreserveBoth(); }
    { Fixture f; f.second->world.scale = std::numeric_limits<float>::infinity(); f.PreserveBoth(); }
    {
        Fixture f;
        f.first->parent = &f.otherRoot;
        f.system.waterObjects.push_back(f.secondWater);
        RemoveDuplicateWaterSystemObjects(&f.system, &f.root);
        assert(f.system.removed.size() == 1 && f.system.removed[0] == f.second.get());
        assert(f.system.waterObjects.size() == 1 && !f.first->culled && f.second->culled);
        // Thread-local scratch must not retain references after this attachment.
        assert(f.second->refs == 2);
    }
    {
        Fixture f;
        f.second->parent = &f.otherRoot;
        RemoveDuplicateWaterSystemObjects(&f.system, &f.root);
        assert(f.system.removed.size() == 1 && f.system.removed[0] == f.first.get());
        assert(f.system.waterObjects.size() == 1 && f.first->culled && !f.second->culled);
    }
    {
        Fixture f;
        RemoveDuplicateWaterSystemObjects(&f.system, &f.root);
        assert(f.system.removed.size() == 1 && f.system.removed[0] == f.second.get());
        assert(!f.first->culled && f.second->culled);
    }
    assert(RE::RefObject::live == 0);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-revision", help="Read the implementation at a Git revision for regression verification")
    args = parser.parse_args()
    relative = "src/Features/UnifiedWater.cpp"
    source = subprocess.check_output(["git", "show", f"{args.source_revision}:{relative}"], cwd=ROOT, text=True) if args.source_revision else (ROOT / relative).read_text(encoding="utf-8")
    production = between(source, "struct WaterPositionKey", "static bool ShouldCullAtCell(")
    commonlib = ROOT / "extern/CommonLibSSE-NG"
    comparisons = "namespace RE {\n"
    for name in ("NiPoint3", "NiMatrix3", "NiTransform"):
        native = (commonlib / f"src/RE/N/{name}.cpp").read_text(encoding="utf-8")
        for operator in ("==", "!="):
            comparisons += function(native, f"\tbool {name}::operator{operator}(")
    comparisons += "}\n"
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++20 compiler is required for the water duplicate fixture")
    with tempfile.TemporaryDirectory(prefix="se-water-duplicates-") as temporary:
        output = Path(temporary)
        source_path = output / "fixture.cpp"
        source_path.write_text(PRELUDE + comparisons + production + CHECKS, encoding="utf-8")
        executable = output / ("fixture.exe" if os.name == "nt" else "fixture")
        if Path(compiler).stem.lower() in ("cl", "clang-cl"):
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", f"/I{commonlib / 'include'}", str(source_path), f"/Fe:{executable}"]
        else:
            command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(commonlib / "include"), str(source_path), "-o", str(executable)]
        subprocess.run(command, cwd=output, check=True)
        subprocess.run([str(executable)], cwd=output, check=True)
    print("Production water deduplication preserves distinct and aliased surfaces; equivalent LOD copies remove once without retained owners")


if __name__ == "__main__":
    main()
