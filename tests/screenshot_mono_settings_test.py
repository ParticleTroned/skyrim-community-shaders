"""Compile and exercise the production mono capture normalizer/settings methods."""
from pathlib import Path
import os
import subprocess
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]
api = (ROOT / 'src/Features/ScreenshotApi.cpp').read_text(encoding='utf-8')
feature = (ROOT / 'src/Features/ScreenshotFeature.cpp').read_text(encoding='utf-8')
methods = api[api.index('ScreenshotApi::json ScreenshotApi::NormalizeCaptureDescriptor('):api.index('ScreenshotApi::json ScreenshotApi::BuildCapabilities(')]
build = feature[feature.index('json ScreenshotFeature::BuildCaptureDescriptor('):feature.index('ScreenshotFeature::CaptureStartResult ScreenshotFeature::TryStartApiCapture(')]
preamble = r'''
#include "Features/ScreenshotApiPolicy.h"
#include "Features/ScreenshotCapturePolicy.h"
#include "Utils/StringUtils.h"
#include <nlohmann/json.hpp>
#include <array>
#include <cassert>
#include <unordered_set>
using json = nlohmann::json;
static bool hdr = false;
bool IsFlatHdrScreenshotCapture() { return hdr; }
struct Crop { float x=0,y=0,w=1,h=1; };
struct Subrect { Crop GetUV() const { return {}; } };
struct ScreenshotFeature {
 bool enabled=true, sdrUsePng=true, frameCaptureUsePng=false, copyToClipboard=false, applyCropToScreenshot=true;
 uint32_t hdrPngBitDepth=11;
 std::string screenshotPath="Screenshots", frameCapturePath="Frames";
 struct { uint32_t frameCount=30,intervalFrames=6,previewFramesPerSecond=15; bool writePreviewVideo=false; } sequenceDefaults;
 Subrect subrect;
 bool IsRuntimeEnabled() const { return enabled; }
 void SetEnabled(bool value) { enabled=value; }
 json BuildCaptureDescriptor(bool,bool) const;
};
struct CaptureDescriptorError : std::runtime_error {
 CaptureDescriptorError(std::string,std::string,std::string text): std::runtime_error(text) {}
};
std::filesystem::path ResolveConfiguredCaptureDirectory(const std::filesystem::path& path, bool) {
 if (path.empty() || path.string().find("..")!=std::string::npos) throw std::runtime_error("unsafe path");
 return std::filesystem::path("/game")/path;
}
struct ScreenshotApi {
 using json=nlohmann::json;
 static constexpr uint32_t kMaximumSequenceFrames=10000;
 json NormalizeCaptureDescriptor(const ScreenshotFeature&,const json&,bool=false) const;
 json BuildSettings(const ScreenshotFeature&) const;
 json ValidateSettingsPatch(const json&) const;
 void ApplySettingsPatch(ScreenshotFeature&,const json&) const;
 static std::filesystem::path ResolveDestinationDirectory(const ScreenshotFeature& f,const json&,bool seq) {
  return ResolveConfiguredCaptureDirectory(seq?f.frameCapturePath:f.screenshotPath,seq);
 }
};
'''
tests = r'''
int main() {
 ScreenshotApi api; ScreenshotFeature feature;
 auto normal=api.NormalizeCaptureDescriptor(feature,json::object());
 assert(normal["source"]["kind"]=="desktop_mirror");
 assert(normal["outputs"][0]["view"]=="source_native");
 assert(normal["outputs"][0]["encoding"]["format"]=="png");
 assert(normal["outputs"][0]["encoding"]["bitDepth"]==8);
 auto sequence=api.NormalizeCaptureDescriptor(feature,json::object(),true);
 assert(sequence["outputs"][0]["encoding"]["format"]=="bmp");
 assert(std::filesystem::path(sequence["destination"]["resolvedDirectory"].get<std::string>()).lexically_normal()==std::filesystem::path("/game/Frames"));
 hdr=true;
 normal=api.NormalizeCaptureDescriptor(feature,json::object());
 assert(normal["outputs"][0]["encoding"]["colourContract"]=="hdr10_pq");
 assert(normal["outputs"][0]["encoding"]["bitDepth"]==11);
 auto descriptor=json::parse(R"({"useSettings":false,"capture":{"source":{"kind":"desktop_mirror"},"outputs":[{"encoding":{"format":"png","colourContract":"hdr10_pq","bitDepth":14},"crop":{"x":0.25,"y":0.25,"width":0.5,"height":0.5}}]}})");
 normal=api.NormalizeCaptureDescriptor(feature,descriptor);
 assert(normal["outputs"][0]["encoding"]["bitDepth"]==14);
 for (auto invalid : {json(-1),json(17),json("11"),json(nullptr)}) {
  descriptor["capture"]["outputs"][0]["encoding"]["bitDepth"]=invalid;
  bool rejected=false; try { api.NormalizeCaptureDescriptor(feature,descriptor); } catch (...) { rejected=true; } assert(rejected);
 }
 descriptor["capture"]["outputs"][0]["encoding"]["bitDepth"]=11;
 descriptor["capture"]["outputs"][0]["crop"]["width"]=2;
 bool rejected=false; try { api.NormalizeCaptureDescriptor(feature,descriptor); } catch (...) { rejected=true; } assert(rejected);
 auto patch=json{{"Sequence",{{"FrameCount",120},{"Schedule",{{"IntervalFrames",1}}}}},{"HdrPngBitDepth",16},{"ApplyCrop",false}};
 assert(api.ValidateSettingsPatch(patch).at("valid").get<bool>());
 api.ApplySettingsPatch(feature,patch);
 assert(feature.sequenceDefaults.frameCount==120 && feature.sequenceDefaults.intervalFrames==1 && feature.hdrPngBitDepth==16 && !feature.applyCropToScreenshot);
 for (const auto& bad : {json{{"HdrPngBitDepth",6}},json{{"HdrPngBitDepth",1ull<<63}},json{{"Unknown",true}},json{{"Sequence",{{"Unknown",true}}}}})
  assert(!api.ValidateSettingsPatch(bad).at("valid").get<bool>());
 assert(feature.hdrPngBitDepth==16);
}
'''
compiler = os.environ.get('CXX') or shutil.which('cl') or shutil.which('g++') or shutil.which('clang++')
if not compiler:
    raise RuntimeError('A C++20 compiler is required')
json_include = os.environ.get('CSX_JSON_INCLUDE')
if not json_include:
    raise RuntimeError('CSX_JSON_INCLUDE must name the directory containing nlohmann/json.hpp')
json_include = str(Path(json_include).resolve())
with tempfile.TemporaryDirectory(prefix='csx-screenshot-settings-') as temporary:
    directory = Path(temporary)
    source = directory / 'test.cpp'
    binary = directory / 'test.exe'
    source.write_text(preamble + build + methods + tests, encoding='utf-8')
    if Path(compiler).name.lower() in ('cl', 'cl.exe'):
        command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/O2', '/W4', '/WX',
                   '/I' + str(ROOT / 'src'), '/I' + json_include, str(source), '/Fe:' + str(binary)]
    else:
        command = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-Wno-deprecated-declarations', '-I' + str(ROOT / 'src'),
                   '-I' + json_include, str(source), '-o', str(binary)]
    subprocess.run(command, cwd=directory, check=True, timeout=60)
    subprocess.run([str(binary)], cwd=directory, check=True, timeout=10)
print('Screenshot mono normalization/settings tests passed')
