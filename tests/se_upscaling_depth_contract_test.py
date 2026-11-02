"""Exercise production Streamline depth/cache boundaries across SE menu/world activation.

The SDK is a recorder and reprojection is a parameter recorder. Actual convention
selection, matrix standardization, Streamline constants/cache and pre-evaluate
resource invalidation, GPU-idle proof and SDK teardown are extracted unchanged
from production; boundary fakes inject failure and incomplete-completion results.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from extract_adaptive_balance_toggle import function


ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r'''
#include "Utils/DepthConvention.h"
#include <array>
#include <bit>
#include <cassert>
#include <cfloat>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
struct Matrix {
    float m[4][4]{};
    Matrix Transpose() const {
        Matrix out; for(unsigned row=0;row<4;++row) for(unsigned col=0;col<4;++col) out.m[row][col]=m[col][row]; return out;
    }
};
#define _11 m[0][0]
#define _12 m[0][1]
#define _13 m[0][2]
#define _21 m[1][0]
#define _22 m[1][1]
#define _23 m[1][2]
#define _31 m[2][0]
#define _32 m[2][1]
#define _33 m[2][2]
struct float3 { float x{},y{},z{}; };
struct float2 { float x{},y{}; };
namespace sl {
    using ViewportHandle=uint32_t;
    using float4x4=Matrix;
    enum class Boolean { eFalse,eTrue };
    enum class Result { eOk,eFailed,eWarnOutOfVRAM };
    struct FrameToken {};
    enum class DLSSMode { eOff };
    struct DLSSOptions { DLSSMode mode{}; };
    constexpr uint32_t kFeatureDLSS=1;
    struct Constants {
        float cameraAspectRatio{}, cameraFOV{}, cameraNear{}, cameraFar{}, motionVectorsInvalidValue{};
        float3 cameraRight{},cameraUp{},cameraFwd{},cameraPos{};
        float2 cameraPinholeOffset{},jitterOffset{},mvecScale{};
        Matrix cameraViewToClip{},clipToCameraView{},clipToPrevClip{},prevClipToClip{};
        Boolean cameraMotionIncluded{},depthInverted{},reset{},motionVectors3D{},orthographicProjection{},motionVectorsDilated{},motionVectorsJittered{};
    };
}
namespace DLSSResultPolicy {
    constexpr bool IsEvaluationSuccessful(sl::Result result) noexcept;
}
#define SL_FAILED(result, call) auto result=(call); result != sl::Result::eOk
namespace logger { template<class... A> void error(A&&...) {} template<class... A> void debug(A&&...) {} }
namespace magic_enum { template<class T> int enum_name(T) { return 0; } }
using HRESULT=int32_t;
using BOOL=int;
using ULONGLONG=uint64_t;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=-1;
constexpr BOOL FALSE=0;
constexpr int D3D11_QUERY_EVENT=1;
#define FAILED(value) ((value)<0)
struct D3D11_QUERY_DESC { int Query{}; };
struct ID3D11Query { int references=0; void Release() { assert(references>0); --references; } } idleQuery;
struct ID3D11Device {
    int references=0;
    HRESULT createResult=S_OK;
    bool nullQuery=false;
    HRESULT CreateQuery(const D3D11_QUERY_DESC* desc,ID3D11Query** query) {
        assert(desc->Query==D3D11_QUERY_EVENT); *query=nullptr;
        if(createResult==S_OK && !nullQuery) { ++idleQuery.references; *query=&idleQuery; }
        return createResult;
    }
    void Release() { assert(references>0); --references; }
} idleDevice;
struct ID3D11DeviceContext {
    bool deviceAvailable=true,completed=true;
    HRESULT dataResult=S_OK;
    unsigned flushes=0,ends=0,polls=0;
    void GetDevice(ID3D11Device** device) {
        *device=deviceAvailable?&idleDevice:nullptr;
        if(*device) ++idleDevice.references;
    }
    void Flush() { ++flushes; }
    void End(ID3D11Query* query) { assert(query==&idleQuery); ++ends; }
    HRESULT GetData(ID3D11Query* query,BOOL* value,size_t,unsigned) {
        assert(query==&idleQuery); ++polls; *value=completed; return dataResult;
    }
};
ULONGLONG fakeMilliseconds=0;
ULONGLONG GetTickCount64() { return fakeMilliseconds; }
void Sleep(unsigned duration) { fakeMilliseconds+=duration; }
namespace Util { float GetVerticalFOVRad() { return 1.2f; } }
namespace UpscalingCamera {
    struct Reprojection { Matrix cameraViewToClip,clipToCameraView,clipToPrevClip,prevClipToClip; };
    Matrix recordedCurrent,recordedPrevious;
    Reprojection BuildReprojection(const Matrix&,const Matrix& current,const Matrix& previous,const float3&) {
        recordedCurrent=current; recordedPrevious=previous; return {current,current,previous,previous};
    }
}
struct ReverseZ {
    bool active=false;
    uint64_t generation=0;
    bool IsActive() const { return active; }
    uint64_t GetGeneration() const { return generation; }
    Depth::Convention GetConvention() const { return active?Depth::Convention::Reversed:Depth::Convention::Standard; }
    Depth::Convention GetNativeDepthConvention() const;
};
namespace globals {
    struct State { uint32_t frameCount=10; } stateStorage;
    State* state=&stateStorage;
    namespace game {
        float nearValue=1,farValue=2;
        float* cameraNear=&nearValue;
        float* cameraFar=&farValue;
        struct GraphicsState { uint32_t screenWidth=1920,screenHeight=1080; } viewport;
        GraphicsState* graphicsState=&viewport;
        struct FrameBuffer {
            Matrix projection,current,previous,inverseView;
            float3 position{},previousPosition{};
            const Matrix& GetCameraProj() const { return projection; }
            const Matrix& GetCameraViewInverse() const { return inverseView; }
            const Matrix& GetCameraViewProjUnjittered() const { return current; }
            const Matrix& GetCameraPreviousViewProjUnjittered() const { return previous; }
            const float3& GetCameraPosAdjust() const { return position; }
            const float3& GetCameraPreviousPosAdjust() const { return previousPosition; }
        } frameBufferCached;
    }
    namespace features {
        ReverseZ reverseZ;
        struct Upscaling {
            float2 jitter{.25f,-.5f};
            bool reset=false;
            void PrepareHistoryResetForCurrentFrame() {}
            bool ShouldResetHistoryThisFrame() const { return reset; }
        } upscaling;
    }
    namespace d3d { ID3D11DeviceContext contextStorage; ID3D11DeviceContext* context=&contextStorage; }
}
struct ID3D11Resource {};
sl::Constants recorded;
unsigned submissions=0;
sl::Result Record(const sl::Constants& constants,sl::FrameToken&,sl::ViewportHandle) {
    recorded=constants; ++submissions; return sl::Result::eOk;
}
struct Streamline {
    bool initialized=true, featureDLSS=true, slEvaluateFeature=true, slSetTag=true;
    bool generationInstance=false, tokenAvailable=true;
    unsigned tokenRequests=0, destroyCalls=0;
    sl::FrameToken token;
    sl::Result (*slSetConstants)(const sl::Constants&,sl::FrameToken&,sl::ViewportHandle)=Record;
    uint32_t constantsFrame=UINT32_MAX,constantsViewport=UINT32_MAX;
    uint64_t constantsDepthGeneration=UINT64_MAX;
    bool constantsNativeDepthInverted=false,constantsResult=false;
    std::optional<bool> dlssNativeDepthInverted;
    bool dlssEvaluationSucceeded=false,dlssResourceReleasePending=false;
    std::mutex frameTokenMutex;
    std::array<int,3> frameTokens{};
    unsigned nextFrameTokenSlot=0;
    uint32_t latestFrameTokenFrame=UINT32_MAX,lastReflexSleepFrame=UINT32_MAX,dlssgSimulationStartFrame=UINT32_MAX;
    bool IsDLSSGInstance() const { return generationInstance; }
    bool EnsureFrameToken() { ++tokenRequests; return tokenAvailable; }
    sl::FrameToken* GetFrameTokenForFrame(uint32_t) { return tokenAvailable?&token:nullptr; }
    sl::ViewportHandle viewport=0;
    sl::Result freeResourcesResult=sl::Result::eOk,offResult=sl::Result::eOk;
    unsigned offCalls=0;
    bool optionsValid=true;
    std::function<sl::Result(sl::ViewportHandle,const sl::DLSSOptions&)> slDLSSSetOptions=
        [this](auto,const auto& options) { assert(options.mode==sl::DLSSMode::eOff); ++offCalls; return offResult; };
    std::function<sl::Result(uint32_t,sl::ViewportHandle)> slFreeResources=
        [this](auto feature,auto) { assert(feature==sl::kFeatureDLSS); ++destroyCalls; return freeResourcesResult; };
    void InvalidateDLSSOptionsCache() { optionsValid=false; }
    bool DestroyDLSSResources();
    void ResetFrameTracking();
    void ResetDLSSResourceTracking();
    bool CheckFrameConstants(sl::ViewportHandle);
    bool FinishEvaluate(sl::Result evalResult,bool nativeDepthInverted);
    bool BeforeEvaluate(ID3D11Resource* colorIn,ID3D11Resource* colorOut,ID3D11Resource* depth,
        ID3D11Resource* mvec,ID3D11Resource* reactiveMask,ID3D11Resource* transparencyMask);
};
'''

CHECKS = r'''
Matrix StandardProjection() {
    Matrix value; value.m[0][0]=1; value.m[1][1]=1; value.m[2][2]=2; value.m[2][3]=1; value.m[3][2]=-2; return value;
}
void Publish(bool reversed) {
    auto projection=StandardProjection();
    if(reversed) Depth::SetReverseProjection(projection,1,2);
    auto& cache=globals::game::frameBufferCached;
    cache.projection=cache.current=cache.previous=projection.Transpose();
}
Matrix StandardHistory() {
    Matrix value;
    value.m[0][2]=1.25f; value.m[0][3]=1;
    value.m[1][1]=1; value.m[2][0]=-1;
    value.m[3][0]=5; value.m[3][1]=-3; value.m[3][2]=7.5f; value.m[3][3]=8;
    return value; // Rotated, translated previous camera with its own near/far range.
}
void CheckHistory(const Matrix& expected) {
    for(unsigned row=0;row<4;++row) for(unsigned col=0;col<4;++col)
        assert(UpscalingCamera::recordedPrevious.m[row][col]==expected.m[row][col]);
}
int main() {
    using namespace globals;
    Streamline dlss,fg;
    fg.generationInstance=true;
    ID3D11Resource resource;
    const auto evaluate=[&] { return dlss.BeforeEvaluate(&resource,&resource,&resource,&resource,&resource,&resource); };
    Publish(false);
    assert(dlss.CheckFrameConstants(0) && recorded.depthInverted==sl::Boolean::eFalse && recorded.reset==sl::Boolean::eTrue);
    assert(recorded.cameraNear==1 && recorded.cameraFar==2 && recorded.jitterOffset.x==-.25f && recorded.jitterOffset.y==.5f);
    assert(dlss.FinishEvaluate(sl::Result::eOk,false));
    ++state->frameCount;
    assert(dlss.CheckFrameConstants(0) && recorded.reset==sl::Boolean::eFalse);
    const auto stable=submissions;
    assert(dlss.CheckFrameConstants(0) && submissions==stable);

    features::reverseZ.active=true; ++features::reverseZ.generation;
    assert(features::reverseZ.GetConvention()==Depth::Convention::Reversed);
    assert(features::reverseZ.GetNativeDepthConvention()==Depth::Convention::Standard);
    assert(evaluate() && dlss.destroyCalls==0);
    assert(dlss.CheckFrameConstants(0) && recorded.depthInverted==sl::Boolean::eFalse);
    assert(UpscalingCamera::recordedCurrent.m[2][2]==2);
    assert(fg.CheckFrameConstants(0) && recorded.depthInverted==sl::Boolean::eFalse);
    assert(UpscalingCamera::recordedCurrent.m[2][2]==2); // Allocation must not invert menu matrices.

    Publish(true); // First world camera changes after the early resource check in the same frame.
    const auto standardHistory=StandardHistory();
    game::frameBufferCached.previous=standardHistory.Transpose();
    assert(features::reverseZ.GetNativeDepthConvention()==Depth::Convention::Reversed);
    assert(evaluate() && dlss.destroyCalls==1 && !dlss.dlssNativeDepthInverted);
    assert(dlss.CheckFrameConstants(0) && recorded.depthInverted==sl::Boolean::eTrue && recorded.reset==sl::Boolean::eTrue);
    assert(UpscalingCamera::recordedCurrent.m[2][2]==-1);
    assert(dlss.FinishEvaluate(sl::Result::eWarnOutOfVRAM,true)); // Budget warnings still publish valid history.
    const auto steady=submissions,steadyFences=d3d::contextStorage.ends;
    assert(evaluate() && dlss.destroyCalls==1 && d3d::contextStorage.ends==steadyFences);
    assert(dlss.CheckFrameConstants(0) && submissions==steady);
    assert(fg.CheckFrameConstants(0) && recorded.depthInverted==sl::Boolean::eFalse);
    assert(UpscalingCamera::recordedCurrent.m[2][2]==2 && UpscalingCamera::recordedCurrent.m[3][2]==-2);
    CheckHistory(standardHistory); // Current reversal must not reverse an independent standard history.
    assert(fg.destroyCalls==0);

    Publish(false); // A standard camera must retain its convention even after world activation.
    auto reversedHistory=standardHistory;
    reversedHistory.m[0][2]=-.25f; reversedHistory.m[3][2]=.5f;
    game::frameBufferCached.previous=reversedHistory.Transpose();
    assert(evaluate() && dlss.destroyCalls==2);
    assert(dlss.CheckFrameConstants(0) && recorded.depthInverted==sl::Boolean::eFalse && recorded.reset==sl::Boolean::eTrue);
    assert(fg.CheckFrameConstants(0) && recorded.depthInverted==sl::Boolean::eFalse);
    assert(UpscalingCamera::recordedCurrent.m[2][2]==2);
    CheckHistory(standardHistory); // Previous reversed history still needs standardization.

    assert(!dlss.FinishEvaluate(sl::Result::eFailed,false));
    ++state->frameCount;
    assert(dlss.CheckFrameConstants(0) && recorded.reset==sl::Boolean::eTrue);
    const auto failedContextDestroys=dlss.destroyCalls;
    Publish(true);
    assert(evaluate() && dlss.destroyCalls==failedContextDestroys+1);
    assert(!dlss.dlssNativeDepthInverted && !dlss.dlssEvaluationSucceeded);
    assert(dlss.CheckFrameConstants(0) && recorded.reset==sl::Boolean::eTrue && recorded.depthInverted==sl::Boolean::eTrue);

    // A failed native free must not erase the old convention or permit reuse,
    // even if a later camera switches back to that old convention.
    assert(dlss.FinishEvaluate(sl::Result::eOk,true));
    Publish(false);
    dlss.freeResourcesResult=sl::Result::eFailed;
    const auto freeFailures=dlss.destroyCalls;
    assert(!evaluate() && dlss.destroyCalls==freeFailures+1);
    assert(dlss.dlssResourceReleasePending && dlss.dlssNativeDepthInverted==true && !dlss.optionsValid);
    dlss.ResetFrameTracking();
    assert(dlss.dlssResourceReleasePending && dlss.dlssNativeDepthInverted==true);
    Publish(true);
    assert(!evaluate() && dlss.destroyCalls==freeFailures+2 && dlss.dlssNativeDepthInverted==true);
    dlss.featureDLSS=false; // A feature recheck is not evidence of context retirement.
    dlss.ResetFrameTracking();
    assert(!dlss.DestroyDLSSResources() && dlss.dlssResourceReleasePending && dlss.dlssNativeDepthInverted==true);
    dlss.freeResourcesResult=sl::Result::eOk;
    assert(dlss.DestroyDLSSResources() && !dlss.dlssResourceReleasePending && !dlss.dlssNativeDepthInverted);
    dlss.featureDLSS=true;
    assert(evaluate() && !dlss.dlssResourceReleasePending && !dlss.dlssNativeDepthInverted);

    // Native completion failures cannot be treated as proof that old resources
    // are safe to free. All query/device references must be balanced on failure.
    assert(dlss.FinishEvaluate(sl::Result::eOk,true));
    Publish(false);
    const auto releases=dlss.destroyCalls,offCalls=dlss.offCalls;
    const auto retain=[&] {
        assert(!evaluate() && dlss.destroyCalls==releases && dlss.offCalls==offCalls);
        assert(dlss.dlssResourceReleasePending && dlss.dlssNativeDepthInverted==true);
        assert(idleQuery.references==0 && idleDevice.references==0);
    };
    d3d::contextStorage.deviceAvailable=false; retain();
    d3d::contextStorage.deviceAvailable=true;
    idleDevice.createResult=E_FAIL; retain();
    idleDevice.createResult=S_OK; idleDevice.nullQuery=true; retain();
    idleDevice.nullQuery=false;
    d3d::contextStorage.dataResult=E_FAIL; retain();
    d3d::contextStorage.dataResult=S_FALSE; retain();
    d3d::contextStorage.dataResult=S_OK; d3d::contextStorage.completed=false; retain();
    d3d::contextStorage.completed=true;
    const auto freeFunction=dlss.slFreeResources;
    dlss.slFreeResources=nullptr; retain(); dlss.slFreeResources=freeFunction;
    // Off may fail independently: a successful native free still proves retirement.
    dlss.offResult=sl::Result::eFailed;
    assert(evaluate() && dlss.destroyCalls==releases+1 && !dlss.dlssResourceReleasePending);
    assert(!dlss.dlssNativeDepthInverted && idleQuery.references==0 && idleDevice.references==0);
    assert(dlss.CheckFrameConstants(0) && recorded.reset==sl::Boolean::eTrue);
    dlss.offResult=sl::Result::eOk;
    assert(!FlushAndWaitForD3D11Idle(nullptr,"test"));
    dlss.featureDLSS=false;
    const auto emptyFences=d3d::contextStorage.ends,emptyFrees=dlss.destroyCalls;
    assert(dlss.DestroyDLSSResources() && d3d::contextStorage.ends==emptyFences && dlss.destroyCalls==emptyFrees);
    dlss.featureDLSS=true;

    dlss.tokenAvailable=false;
    dlss.ResetFrameTracking();
    const auto requests=dlss.tokenRequests;
    assert(!dlss.CheckFrameConstants(0) && !dlss.CheckFrameConstants(0) && dlss.tokenRequests==requests+1);
    ++features::reverseZ.generation;
    assert(!dlss.CheckFrameConstants(0) && dlss.tokenRequests==requests+2);
    const auto destroys=dlss.destroyCalls;
    assert(!dlss.BeforeEvaluate(nullptr,&resource,&resource,&resource,&resource,&resource) && dlss.destroyCalls==destroys);
}
'''


def main() -> None:
    source = (ROOT / "src/Features/Upscaling/Streamline.cpp").read_text(encoding="utf-8")
    reverse = (ROOT / "src/Features/ReverseZ.cpp").read_text(encoding="utf-8")
    production = function(reverse, "Depth::Convention ReverseZ::GetNativeDepthConvention() const")
    production += function(source, "bool Streamline::CheckFrameConstants(")
    production += function(source, "void Streamline::ResetFrameTracking()")
    production += function(source, "void Streamline::ResetDLSSResourceTracking()")
    production += function(source, "bool FlushAndWaitForD3D11Idle(")
    production += function(source, "bool Streamline::DestroyDLSSResources()")
    result_policy = (ROOT / "src/Features/Upscaling/DLSSResultPolicy.h").read_text(encoding="utf-8")
    production += "namespace DLSSResultPolicy {\n" + function(result_policy, "[[nodiscard]] constexpr bool IsEvaluationSuccessful(") + "\n}\n"
    evaluation = function(source, "bool Streamline::EvaluateDLSS(")
    prefix = evaluation[evaluation.index("{") + 1:evaluation.index("\tsl::Resource colorInRes")]
    production += "bool Streamline::BeforeEvaluate(ID3D11Resource* colorIn,ID3D11Resource* colorOut,ID3D11Resource* depth,ID3D11Resource* mvec,ID3D11Resource* reactiveMask,ID3D11Resource* transparencyMask) {\n" + prefix + "\nreturn true;\n}\n"
    production += "bool Streamline::FinishEvaluate(sl::Result evalResult,bool nativeDepthInverted) {\n" + evaluation[evaluation.index("\tconst bool successful ="):]
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++20 compiler is required")
    with tempfile.TemporaryDirectory(prefix="se-upscaling-depth-contract-") as directory:
        root = Path(directory)
        unit = root / "depth.cpp"
        unit.write_text(PRELUDE + production + CHECKS, encoding="utf-8")
        executable = root / ("depth.exe" if os.name == "nt" else "depth")
        if Path(compiler).stem.lower() in ("cl", "clang-cl"):
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", f"/I{ROOT / 'src'}", str(unit), f"/Fe:{executable}"]
        else:
            command = [compiler, "-std=c++20", "-O0", "-g", "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'src'}", str(unit), "-o", str(executable)]
        subprocess.run(command, check=True, cwd=root)
        subprocess.run([str(executable)], check=True, cwd=root)
    print("Production SE menu/world depth convention, DLSS recreation/reset, FG matrices, generation/cache and failed-retirement regression passed")


if __name__ == "__main__":
    main()
