"""Exercise production ISL hook bodies with native/renderer fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from skylighting_settings_test import block

ROOT = Path(__file__).resolve().parents[1]

class Gates(unittest.TestCase):
    def test_smaller_capture_respects_limit_after_buffer_reuse(self):
        source = (ROOT / "src/Diagnostics/ISLDiagnostics.cpp").read_text()
        finish = block(source, "void Scope::Finish() noexcept")
        label = block(source, "void Scope::Label(std::string_view name) noexcept")
        driver = r'''
#include "Diagnostics/ISLDiagnostics.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace SKSE::stl {
template<class F> struct scope_exit { F callback; ~scope_exit() { callback(); } };
}
namespace CSX::Diagnostics::ISL {
namespace RM { int64_t now=20; int64_t Clock() { return now; } }
uint64_t CpuTime(bool& known) { known=true; return 100; }
int GetCurrentThread() { return 1; }
int QueryThreadCycleTime(int,uint64_t* value) { *value=200; return 1; }
struct Row { int64_t enter,exit; uint64_t generation,frame; uintptr_t caller; uint32_t thread; Metric metric; uint16_t label{}; uint64_t cpuStart100ns{}, cpu100ns{},cycles{}; bool cpuKnown{},cyclesKnown{}; };
struct Totals { uint64_t count{},timed{}; int64_t elapsed{},maximum{}; };
struct Slot {
    std::atomic_flag busy{};
    std::atomic_uint32_t thread{7};
    unsigned depth{},invocationDepth{};
    std::atomic_uint64_t invocationSequence{};
    std::atomic_bool invocationActive{};
    std::array<Totals,size_t(Metric::Count)> totals{};
    std::vector<Row> rows;
    std::array<Totals,128> labelTotals{};
    std::array<std::array<char,128>,128> labels{};
    uint64_t labelLoss{};
    uint64_t overflow{};
} fixture;
constexpr uint64_t FeaturePassStride=64;
constexpr int64_t SlowFeaturePassMicroseconds=2000;
struct DataState { size_t capacity=16; int64_t frequency=10000000; } state;
DataState& Data() { return state; }
Scope::Scope(Metric selected,bool,uintptr_t) noexcept: policy{},slot(&fixture),metric(selected),enter(10) {
    ++slot->depth; slot->busy.test_and_set();
}
Scope::~Scope() { Finish(); }
LABEL
FINISH
}
int main() {
    using namespace CSX::Diagnostics::ISL;
    fixture.rows.reserve(128);
    for (unsigned n=0;n<20;++n) { Scope scope(Metric::EngineUpdate); scope.Finish(); }
    if(fixture.rows.size()!=16 || fixture.overflow!=4 || fixture.depth || fixture.busy.test()) std::abort();
    if(fixture.totals[size_t(Metric::EngineUpdate)].timed!=20) std::abort();
    fixture.rows.clear();
    for(unsigned n=0;n<64;++n) { Scope scope(Metric::FeaturePass); scope.Label("draw"); }
    if(fixture.rows.size()!=1 || fixture.labelTotals[0].count!=64) std::abort();
    RM::now=50000;
    { Scope scope(Metric::FeaturePass); scope.Label("draw"); }
    if(fixture.rows.size()!=2 || fixture.labelTotals[0].count!=65 || fixture.depth || fixture.busy.test()) std::abort();
}
'''.replace('FINISH', finish).replace('LABEL', label)
        self.compile_driver(driver, (True,))

    def test_particle_subtractions_preserve_lock_and_native_admission(self):
        source = (ROOT / "src/Features/LightLimitFix.cpp").read_text()
        body = block(source, "void LightLimitFix::AddParticleLightLuminance(")
        wrapper = block(source, "float LightLimitFix::Hooks::AIProcess_CalculateLightValue_GetLuminance::thunk(")
        driver = r'''
#include "Diagnostics/ISLDiagnostics.h"
#include <chrono>
#include <cstdlib>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <vector>
namespace RE { struct NiPoint3 {}; struct ShadowSceneNode {}; struct NiLight {}; }
struct Cache { bool enabled=true; bool IsEnabled() { return enabled; } } cache;
namespace globals { Cache* shaderCache=&cache; }
struct LightLimitFix {
    struct Settings { bool EnableParticleLightsDetection=true; } settings;
    std::shared_mutex cachedParticleLightsMutex;
    std::vector<float> cachedParticleLights{2,3};
    float CalculateLuminance(float light, RE::NiPoint3&) { return light; }
    void AddParticleLightLuminance(RE::NiPoint3&,int&,float&);
    struct Hooks { struct AIProcess_CalculateLightValue_GetLuminance {
        static inline float (*func)(RE::ShadowSceneNode*,RE::NiPoint3&,int&,float&,float&,RE::NiLight&,int32_t);
        static float thunk(RE::ShadowSceneNode*,RE::NiPoint3&,int&,float&,float&,RE::NiLight&,int32_t);
    }; };
};
namespace globals::features { LightLimitFix lightLimitFix; }
namespace CSX::Diagnostics::ISL {
Policy selected;
Scope::Scope(Metric m,bool,uintptr_t) noexcept: policy(selected),metric(m) {} Scope::~Scope() {}
void Scope::Count(Metric,uint64_t) noexcept {} void Scope::Finish() noexcept {} void RecordCount(Metric,uint64_t) noexcept {}
}
BODY
WRAPPER
unsigned nativeCalls=0;
float Native(RE::ShadowSceneNode*,RE::NiPoint3&,int&,float& sun,float& level,RE::NiLight&,int32_t mask) {
    if(mask!=7) std::abort(); ++nativeCalls; sun=8; level=9; return 11;
}
void Check(bool value) { if(!value) std::abort(); }
int main() {
    using namespace CSX::Diagnostics::ISL;
    using namespace std::chrono_literals;
    auto& feature=globals::features::lightLimitFix;
    RE::NiPoint3 target;
    int hits=4; float level=6;
    selected.particle=Particle::BeforeLock;
    std::unique_lock lock(feature.cachedParticleLightsMutex);
    auto before=std::async(std::launch::async,[&] { feature.AddParticleLightLuminance(target,hits,level); });
    Check(before.wait_for(2s)==std::future_status::ready); before.get();
    Check(hits==4 && level==6 && feature.settings.EnableParticleLightsDetection);
    selected.particle=Particle::ScanOff;
    std::promise<void> called;
    auto scanned=std::async(std::launch::async,[&] { called.set_value(); feature.AddParticleLightLuminance(target,hits,level); });
    called.get_future().wait();
    Check(scanned.wait_for(30ms)==std::future_status::timeout);
    lock.unlock(); scanned.get();
    Check(hits==4 && level==6 && feature.settings.EnableParticleLightsDetection);
    selected.particle=Particle::Normal;
    feature.AddParticleLightLuminance(target,hits,level);
    Check(hits==6 && level==11);
    cache.enabled=false;
    lock.lock();
    auto disabled=std::async(std::launch::async,[&] { feature.AddParticleLightLuminance(target,hits,level); });
    Check(disabled.wait_for(2s)==std::future_status::ready); disabled.get();
    Check(hits==6 && level==11);
    lock.unlock(); cache.enabled=true;
    LightLimitFix::Hooks::AIProcess_CalculateLightValue_GetLuminance::func=&Native;
    RE::NiLight ref; float sun=0;
    for(auto mode : {Particle::BeforeLock,Particle::ScanOff}) {
        selected.particle=mode;
        const auto result=LightLimitFix::Hooks::AIProcess_CalculateLightValue_GetLuminance::thunk(nullptr,target,hits,sun,level,ref,7);
        Check(result==11 && hits==6 && sun==8 && level==9);
    }
    Check(nativeCalls==2 && feature.settings.EnableParticleLightsDetection);
}
'''.replace("BODY", body).replace("WRAPPER", wrapper)
        self.compile_driver(driver, (True,))

    def test_native_calls_output_and_side_effects(self):
        source = (ROOT / "src/Features/InverseSquareLighting.cpp").read_text()
        bodies = "\n".join(block(source, name) for name in (
            "float SanitizeRadius(", "void CaptureOriginalRadius(", "float RestoreOriginalRadius(", "void SetVanillaLightData(",
            "void InverseSquareLighting::ProcessLight(", "float InverseSquareLighting::CalculateRadius(",
            "inline float InverseSquareLighting::SmoothStep(", "float InverseSquareLighting::GetAttenuation(",
            "float EvaluateCustomLuminance(", "float InverseSquareLighting::BSLight_GetLuminance::thunk("))
        driver = r'''
#include "Diagnostics/ISLDiagnostics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <intrin.h>
#include <limits>
#include <string>
#include <utility>
struct Flags {
    unsigned value{};
    template<class... T> bool any(T... bits) const { return (value & (unsigned(bits) | ...)) != 0; }
    template<class... T> bool none(T... bits) const { return !any(bits...); }
    template<class... T> void set(T... bits) { value |= (unsigned(bits) | ...); }
    template<class... T> void reset(T... bits) { value &= ~(unsigned(bits) | ...); }
};
struct LightLimitFix {
    enum class LightFlags : unsigned { Initialised=1, InverseSquare=2, Linear=4, Disabled=8, Spot=16, OmniDirectional=32, Shadow=64, PortalStrict=128, AffectWater=256 };
    struct LightData { Flags lightFlags; struct Color { float x{},y{},z{}; } color; float radius{},invRadius{},fadeZone{},sizeBias{},fade{}; unsigned shadowMaskIndex=123; };
};
namespace RE { struct NiLight; }
namespace ISLCommon {
struct RuntimeLightDataExt {
    Flags flags; float radius=5, originalRadius=5, fade=2, size=1, cutoffOverride=1;
    struct Diffuse { float red=1,green=2,blue=3; } diffuse;
    static RuntimeLightDataExt* Get(RE::NiLight* light);
};
}
namespace RE {
struct NiPoint3 { float x{},y{},z{}; float GetDistance(const NiPoint3& other) const { return std::hypot(x-other.x,y-other.y,z-other.z); } };
struct NiLight { ISLCommon::RuntimeLightDataExt data; struct World { NiPoint3 translate; } world; struct Form { unsigned formID=1; } form; std::string name; Form* GetUserData() { return &form; } };
struct BSLight { struct Ptr { NiLight* p{}; NiLight* get() const { return p; } } light; bool pointLight=true,shadow=false; float luminance=-77; bool IsShadowLight() const { return shadow; } };
}
ISLCommon::RuntimeLightDataExt* ISLCommon::RuntimeLightDataExt::Get(RE::NiLight* light) { return &light->data; }
struct Editor { bool enabled=false, pending=false, disable=false; unsigned calls{}; bool IsEnabled() const { return enabled; } bool HasDeferredWork() const { return pending; } bool ApplyOverrides(RE::NiLight*,ISLCommon::RuntimeLightDataExt*) const { ++const_cast<Editor*>(this)->calls; return false; } bool ShouldDisableLight(bool) const { return disable; } };
struct EditorWindow { Editor lightEditor; static EditorWindow* GetSingleton() { static EditorWindow window; return &window; } };
namespace logger { template<class... T> void debug(T&&...) {} }
struct InverseSquareLighting {
    bool enabled=true; bool IsEnabled() const { return enabled; }
    static constexpr float DefaultCutoff=.05f, DefaultShadowCasterCutoff=.022f, ScaledUnitsSq=.8f*70*70, FadeZoneBase=4.5f*.8f*70;
    void ProcessLight(LightLimitFix::LightData&,RE::BSLight*,RE::NiLight*) const;
    static float CalculateRadius(float,bool,float,float);
    static float SmoothStep(float,float,float);
    static float GetAttenuation(float,float,float);
    struct BSLight_GetLuminance { static inline float (*func)(RE::BSLight*,RE::NiPoint3*,RE::NiLight*); static float thunk(RE::BSLight*,RE::NiPoint3*,RE::NiLight*); };
};
namespace globals::features { InverseSquareLighting inverseSquareLighting; }
#ifdef DEVBENCH_BRIDGE_ENABLED
namespace CSX::Diagnostics::ISL {
Policy selected; bool suppress=false; unsigned candidates=0;
Scope::Scope(Metric m,bool,uintptr_t) noexcept: policy(selected),metric(m) {} Scope::~Scope() {}
void Scope::Count(Metric,uint64_t) noexcept {} void Scope::Candidate(float) noexcept { ++candidates; }
void Scope::Unique(const void*) noexcept {} void Scope::Finish() noexcept {} void RecordCount(Metric,uint64_t) noexcept {}
bool SuppressRadiusWrites() noexcept { return suppress; }
}
#endif
BODIES
unsigned nativeCalls=0;
RE::BSLight* expectedBS{}; RE::NiPoint3* expectedTarget{}; RE::NiLight* expectedRef{};
float Native(RE::BSLight* bs,RE::NiPoint3* position,RE::NiLight* ref) {
    if(bs!=expectedBS || position!=expectedTarget || ref!=expectedRef) std::abort();
    ++nativeCalls; return 42;
}
void Check(bool value,const char* why) { if(!value) { std::fprintf(stderr,"%s\n",why); std::exit(1); } }
int main() {
    using F=LightLimitFix::LightFlags;
    auto& feature=globals::features::inverseSquareLighting;
    RE::NiLight light; light.data.flags.set(F::Initialised,F::InverseSquare,F::Shadow,F::PortalStrict,F::AffectWater);
    RE::BSLight bs; bs.light.p=&light;
    RE::NiPoint3 target{1,2,3}; expectedBS=&bs; expectedTarget=&target;
    InverseSquareLighting::BSLight_GetLuminance::func=&Native;
    auto query=[&] { return InverseSquareLighting::BSLight_GetLuminance::thunk(expectedBS,expectedTarget,expectedRef); };
    LightLimitFix::LightData output{}; feature.ProcessLight(output,&bs,&light);
    Check(output.radius==light.data.radius && output.radius!=5 && output.lightFlags.any(F::InverseSquare),"normal conversion changed");
    Check(query()!=42 && nativeCalls==0 && bs.luminance!=-77,"normal custom evaluation/native count wrong");
#ifdef DEVBENCH_BRIDGE_ENABLED
    using namespace CSX::Diagnostics::ISL;
    const auto expanded=light.data.radius;
    selected.luminance=Luminance::Native; bs.luminance=-77;
    Check(query()==42 && nativeCalls==1 && bs.luminance==-77 && light.data.radius==expanded,"native gate altered radius/cache or forwarded incorrectly");
    selected.luminance=Luminance::ComputeDiscard;
    Check(query()==42 && nativeCalls==2 && candidates==1 && bs.luminance==-77,"discard candidate leaked result/cache");
    expectedRef=&light;
    Check(query()==42 && nativeCalls==3 && candidates==1,"self-light must forward native once");
    expectedRef=nullptr; light.data.flags.set(F::Disabled);
    Check(query()==42 && nativeCalls==4 && candidates==1,"disabled light must forward native once");
    light.data.flags.reset(F::Disabled); feature.enabled=false;
    Check(query()==42 && nativeCalls==5 && light.data.radius==expanded,"disabled feature diagnostic restored radius");
    feature.enabled=true; expectedBS=nullptr;
    Check(query()==42 && nativeCalls==6,"null source lost native forwarding"); expectedBS=&bs; expectedTarget=nullptr;
    Check(query()==42 && nativeCalls==7,"null target lost native forwarding"); expectedTarget=&target;
    selected={}; selected.renderer=Renderer::VanillaOutput; light.data.radius=5;
    EditorWindow::GetSingleton()->lightEditor.disable=true;
    feature.ProcessLight(output,&bs,&light);
    Check(output.radius==5 && output.invRadius==.2f && output.fade==2 && output.fadeZone==0 && output.sizeBias==0,"ordinary output incoherent");
    Check(light.data.radius==expanded && output.lightFlags.none(F::InverseSquare,F::Linear),"output mode skipped engine side effect");
    Check(output.lightFlags.any(F::Disabled) && output.lightFlags.any(F::Shadow) && output.lightFlags.any(F::PortalStrict) && output.lightFlags.any(F::AffectWater) && output.shadowMaskIndex==123,"output mode lost downstream/disable flags");
    Check(output.color.x==1 && output.color.y==2 && output.color.z==3,"output mode changed colour");
    suppress=true; light.data.radius=5; selected.luminance=Luminance::Native;
    feature.ProcessLight(output,&bs,&light); const auto vanilla=output;
    Check(light.data.radius==5,"startup suppression wrote radius");
    selected.renderer=Renderer::VanillaDirect; feature.ProcessLight(output,&bs,&light);
    Check(output.radius==vanilla.radius && output.invRadius==vanilla.invRadius && output.fade==vanilla.fade && output.lightFlags.value==vanilla.lightFlags.value && light.data.radius==5,"direct differs from output under same suppression");
    light.data.flags.reset(F::Initialised); light.data.radius=7; feature.ProcessLight(output,&bs,&light);
    Check(light.data.flags.any(F::Initialised) && output.radius==7 && light.data.originalRadius==7,"future uninitialized light not initialized before direct output");
    light.data.flags.reset(F::InverseSquare); light.data.flags.set(F::Linear); light.data.radius=9;
    feature.ProcessLight(output,&bs,&light);
    Check(output.radius==9 && output.lightFlags.any(F::Linear),"linear-only light was intercepted");
    light.data.flags.set(F::InverseSquare);
    for(float original : {0.0f,-1.0f,std::numeric_limits<float>::quiet_NaN()}) {
        light.data.originalRadius=original;
        selected.renderer=Renderer::VanillaOutput; feature.ProcessLight(output,&bs,&light); const auto ordinary=output;
        selected.renderer=Renderer::VanillaDirect; feature.ProcessLight(output,&bs,&light);
        Check(output.radius==0 && output.invRadius==0 && output.radius==ordinary.radius && output.fade==ordinary.fade && std::isfinite(output.fadeZone),"invalid/zero captured radius has incoherent direct/output data");
    }
#endif
}
'''.replace("BODIES", bodies)
        self.compile_driver(driver)

    def compile_driver(self, driver, variants=(False, True)):
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "run through the MSVC environment")
        with tempfile.TemporaryDirectory(prefix="csx-isl-gates-") as name:
            temp = Path(name)
            cpp = temp / "gates.cpp"
            cpp.write_text(driver)
            for diagnostic in variants:
                exe = temp / ("on.exe" if diagnostic else "off.exe")
                command = [compiler,"/nologo","/std:c++20","/EHsc","/O2",f"/I{ROOT / 'src'}",str(cpp),f"/Fe:{exe}"]
                if diagnostic:
                    command.append("/DDEVBENCH_BRIDGE_ENABLED")
                result = subprocess.run(command,cwd=temp,capture_output=True,text=True,timeout=90)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                result = subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__ == "__main__":
    unittest.main()
