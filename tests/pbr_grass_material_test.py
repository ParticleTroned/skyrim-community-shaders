"""Exercise the production grass conversion before the engine interns it."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class PBRGrassMaterialTests(unittest.TestCase):
    def test_complete_material_and_safe_defaults(self):
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        source = (ROOT / "src/TruePBR.cpp").read_text(encoding="utf-8")
        methods = "\n".join(block(source, signature) for signature in (
            "bool TruePBR::IsPBRGrassMaterial(", "void TruePBR::SetupGrassMaterial("))
        driver = r'''
#include <cassert>
#include <memory>
#include <set>
namespace RE {
struct NiSourceTexture {};
struct BSShaderMaterial { virtual ~BSShaderMaterial() = default; };
struct BSLightingShaderMaterialBase : BSShaderMaterial {
    std::shared_ptr<NiSourceTexture> diffuseTexture;
    unsigned textureClampMode = 0;
};
struct BSShaderProperty {
    enum class EShaderPropertyFlag { kVertexLighting };
    struct Flags { bool authored = false; bool any(EShaderPropertyFlag) const { return authored; } } flags;
    BSShaderMaterial* material = nullptr;
};
struct BSLightingShaderProperty : BSShaderProperty {
    std::unique_ptr<BSShaderMaterial> owned;
    unsigned internCalls = 0;
    void SetMaterial(BSShaderMaterial*, bool);
};
}
struct BSLightingShaderMaterialPBR : RE::BSLightingShaderMaterialBase {
    std::shared_ptr<RE::NiSourceTexture> normalTexture, rmaosTexture, featuresTexture0;
    float roughness = 1, specular = .04f, subsurface = 0;
    struct Registry { std::set<BSLightingShaderMaterialPBR*> members;
        bool Contains(BSLightingShaderMaterialPBR* value) const { return members.contains(value); }
    };
    inline static Registry All;
    void CopyMembers(RE::BSShaderMaterial* source) {
        *this = *static_cast<BSLightingShaderMaterialPBR*>(source);
    }
};
void RE::BSLightingShaderProperty::SetMaterial(RE::BSShaderMaterial* source, bool intern) {
    assert(intern);
    ++internCalls;
    auto clone = std::make_unique<BSLightingShaderMaterialPBR>();
    clone->CopyMembers(source);
    BSLightingShaderMaterialPBR::All.members.insert(clone.get());
    material = clone.get(); owned = std::move(clone);
}
struct GraphicsState {
    std::shared_ptr<RE::NiSourceTexture> defaultTextureWhite = std::make_shared<RE::NiSourceTexture>();
    std::shared_ptr<RE::NiSourceTexture> defaultTextureNormalMap = std::make_shared<RE::NiSourceTexture>();
    const GraphicsState& GetRuntimeData() const { return *this; }
};
namespace globals::game { GraphicsState state; GraphicsState* graphicsState = &state; }
struct TruePBR {
    bool loaded = true;
    bool IsPBRGrassMaterial(const RE::BSShaderMaterial*) const;
    void SetupGrassMaterial(RE::BSLightingShaderProperty*, RE::BSLightingShaderProperty*);
};
METHODS
int main() {
    TruePBR pbr;
    BSLightingShaderMaterialPBR first, second;
    first.normalTexture = std::make_shared<RE::NiSourceTexture>();
    first.rmaosTexture = std::make_shared<RE::NiSourceTexture>();
    first.featuresTexture0 = std::make_shared<RE::NiSourceTexture>();
    first.roughness = .2f; first.subsurface = .3f;
    second.CopyMembers(&first);
    second.rmaosTexture = std::make_shared<RE::NiSourceTexture>();
    second.normalTexture = std::make_shared<RE::NiSourceTexture>();
    second.subsurface = .7f;
    BSLightingShaderMaterialPBR::All.members = { &first, &second };
    RE::BSLightingShaderProperty source;
    source.flags.authored = true;
    auto diffuse = std::make_shared<RE::NiSourceTexture>();
    RE::BSLightingShaderMaterialBase native;
    native.diffuseTexture = diffuse; native.textureClampMode = 3;
    RE::BSLightingShaderProperty grassA, grassB;
    grassA.material = grassB.material = &native;
    // Alternate creation order must retain the complete authored map sets.
    source.material = &second; pbr.SetupGrassMaterial(&source, &grassB);
    source.material = &first; pbr.SetupGrassMaterial(&source, &grassA);
    auto* a = static_cast<BSLightingShaderMaterialPBR*>(grassA.material);
    auto* b = static_cast<BSLightingShaderMaterialPBR*>(grassB.material);
    assert(a->diffuseTexture == diffuse && b->diffuseTexture == diffuse);
    assert(a->textureClampMode == 3 && b->textureClampMode == 3);
    assert(a->normalTexture == first.normalTexture && b->normalTexture == second.normalTexture);
    assert(a->rmaosTexture == first.rmaosTexture && b->rmaosTexture == second.rmaosTexture);
    assert(a->roughness == .2f && a->subsurface == .3f && b->subsurface == .7f);
    assert(a->featuresTexture0 == first.featuresTexture0);
    assert(grassA.internCalls == 1 && grassB.internCalls == 1);
    RE::BSLightingShaderMaterialBase emptyNative;
    RE::BSLightingShaderProperty defaults; defaults.material = &emptyNative;
    first.normalTexture.reset(); first.rmaosTexture = globals::game::state.defaultTextureWhite;
    pbr.SetupGrassMaterial(&source, &defaults);
    auto* fallback = static_cast<BSLightingShaderMaterialPBR*>(defaults.material);
    assert(fallback->diffuseTexture == globals::game::state.defaultTextureWhite);
    assert(fallback->normalTexture == globals::game::state.defaultTextureNormalMap);
    assert(!fallback->rmaosTexture);
    RE::BSLightingShaderProperty ordinary; ordinary.material = &native;
    source.material = &native; pbr.SetupGrassMaterial(&source, &ordinary);
    assert(ordinary.material == &native && ordinary.internCalls == 0);
    source.material = &first; pbr.loaded = false; pbr.SetupGrassMaterial(&source, &ordinary);
    assert(ordinary.internCalls == 0);
    pbr.SetupGrassMaterial(nullptr, &ordinary);
    pbr.SetupGrassMaterial(&source, nullptr);
}
'''.replace("METHODS", methods)
        self.compile_and_run(driver, compiler)

    def test_shader_pair_readiness(self):
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "MSVC compiler required")
        source = (ROOT / "src/Hooks.cpp").read_text(encoding="utf-8")
        method = block(source, "struct BSGrassShader_SetupTechnique") + ";"
        method = method.replace("static inline REL::Relocation<decltype(thunk)> func;",
                                "static inline auto func = NativeSetup;")
        driver = r'''
#include <cassert>
#include <cstdint>
struct ID3D11VertexShader {};
struct ID3D11PixelShader {};
namespace RE {
struct BSShader { enum class Type { Grass }; };
namespace BSGraphics {
enum { DIRTY_VERTEX_DESC };
struct VertexShader { ID3D11VertexShader* shader; };
struct PixelShader { ID3D11PixelShader* shader; };
}
}
struct State {
    uint32_t modifiedVertexDescriptor = 1, modifiedPixelDescriptor = 2;
    bool enabled = true;
    bool ShaderEnabled(RE::BSShader::Type) const { return enabled; }
};
struct Cache {
    bool enabled = true;
    RE::BSGraphics::VertexShader* vertex = nullptr;
    RE::BSGraphics::PixelShader* pixel = nullptr;
    bool IsEnabled() const { return enabled; }
    auto GetVertexShader(RE::BSShader&, uint32_t) { return vertex; }
    auto GetPixelShader(RE::BSShader&, uint32_t) { return pixel; }
};
struct Context {
    ID3D11VertexShader* vertex = nullptr;
    ID3D11PixelShader* pixel = nullptr;
    void VSSetShader(ID3D11VertexShader* value, void*, unsigned) { vertex = value; }
    void PSSetShader(ID3D11PixelShader* value, void*, unsigned) { pixel = value; }
};
struct Flags { bool dirty = false; void set(int) { dirty = true; } };
namespace globals {
State stateValue; State* state = &stateValue;
Cache cacheValue; Cache* shaderCache = &cacheValue;
namespace features {
struct Feature { bool loaded = true; } truePBR, grassLighting;
}
namespace d3d { Context contextValue; Context* context = &contextValue; }
namespace game {
RE::BSGraphics::VertexShader* vertex;
RE::BSGraphics::PixelShader* pixel;
auto currentVertexShader = &vertex;
auto currentPixelShader = &pixel;
Flags flags; Flags* stateUpdateFlags = &flags;
}
}
ID3D11VertexShader nativeVS, customVS;
ID3D11PixelShader nativePS, customPS;
RE::BSGraphics::VertexShader fixtureNativeVertex{&nativeVS}, customVertex{&customVS};
RE::BSGraphics::PixelShader fixtureNativePixel{&nativePS}, customPixel{&customPS};
bool nativeReady = true;
bool publishDuringSetup = false;
bool NativeSetup(RE::BSShader*, uint32_t) {
    globals::game::vertex = &fixtureNativeVertex;
    globals::game::pixel = &fixtureNativePixel;
    globals::d3d::contextValue.vertex = globals::cacheValue.vertex ? &customVS : &nativeVS;
    globals::d3d::contextValue.pixel = globals::cacheValue.pixel ? &customPS : &nativePS;
    if (publishDuringSetup) {
        globals::cacheValue.vertex = &customVertex;
        globals::cacheValue.pixel = &customPixel;
    }
    return nativeReady;
}
METHOD
int main() {
    RE::BSShader shader;
    // A partial asynchronous pair must restore both native stages.
    globals::cacheValue.vertex = &customVertex;
    assert(BSGrassShader_SetupTechnique::thunk(&shader, 0));
    assert(globals::d3d::contextValue.vertex == &nativeVS);
    assert(globals::d3d::contextValue.pixel == &nativePS);
    assert(globals::game::vertex == &fixtureNativeVertex);
    // Compilation can finish after the original stage-binding hooks run.
    globals::cacheValue.vertex = nullptr;
    publishDuringSetup = true;
    assert(BSGrassShader_SetupTechnique::thunk(&shader, 0));
    assert(globals::d3d::contextValue.vertex == &customVS);
    assert(globals::d3d::contextValue.pixel == &customPS);
    assert(globals::game::vertex == &customVertex && globals::game::pixel == &customPixel);
    assert(globals::game::flags.dirty);
    publishDuringSetup = false;
    nativeReady = false;
    assert(!BSGrassShader_SetupTechnique::thunk(&shader, 0));
    assert(globals::game::vertex == &fixtureNativeVertex && globals::game::pixel == &fixtureNativePixel);
    nativeReady = true;
    globals::features::truePBR.loaded = false;
    assert(BSGrassShader_SetupTechnique::thunk(&shader, 0));
    assert(globals::game::vertex == &fixtureNativeVertex && globals::game::pixel == &fixtureNativePixel);
}
'''.replace("METHOD", method)
        self.compile_and_run(driver, compiler)

    def compile_and_run(self, driver, compiler):
        with tempfile.TemporaryDirectory(prefix="csx-pbr-grass-") as temporary:
            directory = Path(temporary)
            cpp = directory / "grass.cpp"
            exe = directory / "grass.exe"
            cpp.write_text(driver, encoding="utf-8")
            compiled = subprocess.run(
                [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX",
                 str(cpp), f"/Fe:{exe}"], cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
