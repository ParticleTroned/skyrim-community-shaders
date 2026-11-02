"""Execute Water's production descriptor and resource-readiness macro policy."""

import os
from dataclasses import replace
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class WaterShaderContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CXX") or shutil.which("cl")
        if not compiler:
            raise RuntimeError("Run in the MSVC developer environment")
        cache = (ROOT / "src/ShaderCache.cpp").read_text(encoding="utf-8")
        header = (ROOT / "src/ShaderCache.h").read_text(encoding="utf-8")
        state = (ROOT / "src/State.cpp").read_text(encoding="utf-8")
        hooks = (ROOT / "src/Hooks.cpp").read_text(encoding="utf-8")
        driver = r'''
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <d3dcommon.h>
namespace RE { struct BSShader {
    enum class Type { Water, Lighting };
    struct Value {
        Type value;
        Type get() const { return value; }
        bool operator==(Type other) const { return value == other; }
    } shaderType;
}; }
struct ShaderCache { FLAGS; TECHNIQUES; };
struct Feature {
    bool loaded = true, enabled = true;
    virtual bool HasShaderDefine(RE::BSShader::Type) { return enabled; }
    virtual std::string_view GetShaderDefineName() = 0;
    static std::vector<Feature*> GetFeatureList();
};
namespace globals::features {
    struct Water : Feature {
        WATER_DEFINE
        bool ready = false;
        bool RequiresVanillaWaterShaders() const { return loaded && !ready; }
    } unifiedWater;
    struct Reverse : Feature {
        REVERSE_DEFINE
        bool activeThisBoot = false;
        void LatchBootState() {}
        bool HasShaderDefine(RE::BSShader::Type) override;
        bool IsActive() const { return activeThisBoot; }
    } reverseZ;
    struct Horizon : Feature {
        HORIZON_DEFINE
        HORIZON_PREDICATE
    } horizonFix;
}
std::vector<Feature*> Feature::GetFeatureList() {
    using namespace globals::features;
    return { &unifiedWater, &reverseZ, &horizonFix };
}
REVERSE_MACRO
NORMALIZE
void Warm(RE::BSShader* shader, uint32_t& vertex, uint32_t& pixel) {
    struct State {
        void ModifyShaderLookup(const RE::BSShader&, uint32_t& vs, uint32_t& ps, bool) { Normalize(vs, ps); }
    } storage;
    auto* state = &storage;
    PREPARE_LOOKUP;
    prepareLookup(vertex, pixel);
}
MACROS
NATIVE_SELECTION
bool Has(const std::array<D3D_SHADER_MACRO, 64>& macros, const char* name) {
    for (auto macro : macros) {
        if (!macro.Name) return false;
        if (std::string_view(macro.Name) == name) return true;
    }
    throw std::runtime_error("Water macro list overflowed");
}
void Require(bool condition) { if (!condition) throw std::runtime_error("Water contract failed"); }
int main(int argc, char**) {
    using namespace globals::features;
    constexpr uint32_t native = static_cast<uint32_t>(ShaderCache::WaterShaderFlags::NativeWater);
    RE::BSShader water{RE::BSShader::Type::Water}, other{RE::BSShader::Type::Lighting};
    if (argc > 1) {
        uint32_t descriptor;
        while (std::cin >> descriptor >> reverseZ.activeThisBoot >> unifiedWater.loaded >> unifiedWater.ready >> horizonFix.loaded) {
            auto vertex = descriptor, pixel = descriptor;
            Normalize(vertex, pixel);
            Require(vertex == pixel);
            std::array<D3D_SHADER_MACRO, 64> macros{};
            GetWaterShaderDefines(vertex, macros);
            std::cout << vertex;
            for (auto macro : macros) {
                if (!macro.Name) break;
                std::cout << ' ' << macro.Name << '=' << (macro.Definition ? macro.Definition : "");
            }
            std::cout << '\n';
        }
        return 0;
    }
    for (bool reversed : {false, true}) for (bool horizon : {false, true}) {
        reverseZ.activeThisBoot = reversed;
        horizonFix.loaded = horizon;
        for (uint32_t descriptor = 0; descriptor < native; ++descriptor) {
            unifiedWater.loaded = true;
            unifiedWater.ready = false;
            uint32_t vertex = descriptor, pixel = descriptor;
            Normalize(vertex, pixel);
            const auto pending = vertex;
            Require(vertex == pixel && (vertex & native));
            auto warmVertex = vertex, warmPixel = pixel;
            Warm(&water, warmVertex, warmPixel);
            Require(warmVertex == (vertex & ~native) && warmPixel == warmVertex);
            Require(UseNativeWaterShaders(water) == !reversed);
            Require(!UseNativeWaterShaders(other));
            std::array<D3D_SHADER_MACRO, 64> fallback{}, ready{}, delayed{};
            GetWaterShaderDefines(vertex, fallback);
            Require(!Has(fallback, "UNIFIED_WATER"));
            Require(Has(fallback, "REVERSE_Z") == reversed);
            Require(Has(fallback, "HORIZON_FIX") == horizon);
            unifiedWater.ready = true;
            Normalize(vertex, pixel);
            Require(vertex == pixel && !(vertex & native));
            Require(!UseNativeWaterShaders(water));
            GetWaterShaderDefines(vertex, ready);
            GetWaterShaderDefines(pending, delayed);
            Require(Has(ready, "UNIFIED_WATER") && !Has(delayed, "UNIFIED_WATER"));
            Require(Has(ready, "REVERSE_Z") == reversed);
            Require(Has(ready, "HORIZON_FIX") == horizon);
            unifiedWater.loaded = false;
            GetWaterShaderDefines(vertex, ready);
            Require(!Has(ready, "UNIFIED_WATER") && !UseNativeWaterShaders(water));
        }
    }
}
'''
        normalizer = block(state, "case RE::BSShader::Type::Water:").replace(
            "case RE::BSShader::Type::Water:",
            "void Normalize(uint32_t& a_vertexDescriptor, uint32_t& a_pixelDescriptor)",
        ).replace("SIE::ShaderCache", "ShaderCache")
        driver = driver.replace("FLAGS", block(header, "enum class WaterShaderFlags"))
        driver = driver.replace("TECHNIQUES", block(header, "enum class WaterShaderTechniques"))
        driver = driver.replace("NORMALIZE", normalizer)
        driver = driver.replace(
            "PREPARE_LOOKUP", block(hooks, "const auto prepareLookup =").replace("SIE::ShaderCache", "ShaderCache")
        )
        driver = driver.replace("MACROS", block(cache, "static void GetWaterShaderDefines("))
        driver = driver.replace("NATIVE_SELECTION", block(hooks, "bool UseNativeWaterShaders("))
        reverse = (ROOT / "src/Features/ReverseZ.cpp").read_text(encoding="utf-8")
        for placeholder, feature in (("WATER_DEFINE", "UnifiedWater"), ("REVERSE_DEFINE", "ReverseZ"),
                                     ("HORIZON_DEFINE", "HorizonFix")):
            feature_header = (ROOT / f"src/Features/{feature}.h").read_text(encoding="utf-8")
            driver = driver.replace(placeholder, block(feature_header, "std::string_view GetShaderDefineName()"))
        horizon_header = (ROOT / "src/Features/HorizonFix.h").read_text(encoding="utf-8")
        driver = driver.replace("HORIZON_PREDICATE", block(horizon_header, "bool HasShaderDefine("))
        driver = driver.replace("REVERSE_MACRO", block(reverse, "bool ReverseZ::HasShaderDefine(").replace(
            "ReverseZ::", "globals::features::Reverse::"))
        temporary = tempfile.TemporaryDirectory(prefix="csx-water-contract-")
        cls.addClassCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        cpp, cls.executable = directory / "water.cpp", directory / "water.exe"
        cpp.write_text(driver, encoding="utf-8")
        compiled = subprocess.run(
            [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX",
             str(cpp), f"/Fe:{cls.executable}"], cwd=directory, capture_output=True, text=True, timeout=60,
        )
        if compiled.returncode:
            raise RuntimeError(compiled.stdout + compiled.stderr)

    def test_readiness_variants_retain_their_compile_identity(self):
        result = subprocess.run([str(self.executable)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_cache_builder_matches_runtime_water_macros(self):
        import yaml
        from hlslkit.compile_shaders import parse_shader_configs
        from shader_cache_pack_builder_test import load_builder

        sys.path.insert(0, str(ROOT / "tools"))
        from shader_cache_manifest import compile_task_defines

        builder = load_builder()
        controlled = {"REVERSE_Z", "UNIFIED_WATER", "HORIZON_FIX"}
        other_features = {contract.shader_define for contract in builder.feature_contracts(ROOT).values()
                          if contract.shader_define} - controlled
        # Hold other features disabled so the matrix isolates these three contracts.
        profile = replace(builder.SHIPPED_CACHE_PROFILE, file_defines={},
                          excluded_defines=builder.SHIPPED_CACHE_PROFILE.excluded_defines | other_features)
        compared = 0
        with tempfile.TemporaryDirectory(prefix="csx-water-cache-identity-") as temporary:
            for runtime, config_name in (("SE", "shader-validation.yaml"), ("VR", "shader-validation-vr.yaml")):
                for reversed_depth in (False, True):
                    for unified in (False, True):
                        for horizon in (False, True):
                            with self.subTest(runtime=runtime, reversed=reversed_depth, unified=unified, horizon=horizon):
                                filtered = builder.filter_profile_defines(
                                    ROOT / ".github/configs" / config_name, Path(temporary) / "config.yaml", yaml,
                                    replace(profile, global_defines=("UNIFIED_WATER",) if unified else ()),
                                    feature_states={"ReverseZ": reversed_depth},
                                    additional_excluded_defines=frozenset({"UNIFIED_WATER", "HORIZON_FIX"}),
                                    additional_file_defines={"Water.hlsl": ("HORIZON_FIX",)} if horizon else {},
                                )
                                tasks = [task for task in parse_shader_configs(str(filtered)) if task[0] == "Water.hlsl"]
                                self.assertTrue(tasks)
                                requests = "".join(
                                    f"{int(entry.rsplit(':', 1)[1], 16)} {int(reversed_depth)} {int(unified)} 1 {int(horizon)}\n"
                                    for _, _, entry, _ in tasks)
                                result = subprocess.run([str(self.executable), "macros"], input=requests,
                                                        capture_output=True, text=True, timeout=30)
                                self.assertEqual(result.returncode, 0, result.stderr)
                                lines = result.stdout.splitlines()
                                self.assertEqual(len(lines), len(tasks))
                                for task, line in zip(tasks, lines):
                                    source, stage, entry, _ = task
                                    descriptor, *macros = line.split()
                                    self.assertEqual(int(descriptor), int(entry.rsplit(':', 1)[1], 16), entry)
                                    macros.append(stage + "=")
                                    if runtime == "VR":
                                        macros.append("VR=")
                                    runtime_task = (source, stage, entry, macros)
                                    self.assertEqual(compile_task_defines([runtime_task]), compile_task_defines([task]), entry)
                                    compared += 1
        print(f"Water cache identity: {compared} runtime/builder macro arrays matched")


if __name__ == "__main__":
    unittest.main()
