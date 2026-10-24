"""Compile the actual native persistence implementation against a small frozen journal."""

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--json-include', required=True)
    args = parser.parse_args()
    source = (ROOT / 'src/Diagnostics/ISLCaptureFile.cpp').read_text()
    actual = source[source.index('namespace CSX::Diagnostics::ReflexMeasurement'):source.rindex('#endif')]
    driver = r'''
#define NOMINMAX
#include <Windows.h>
#include <winrt/base.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <mutex>
#include <fstream>
#include "Utils/StringUtils.h"
using json = nlohmann::json;
std::filesystem::path folder;
namespace SKSE::log { auto log_directory() { return std::optional(folder); } }
namespace CSX::Api::ServiceFoundation { std::string NewId() { return "test"; } }
namespace CSX::Diagnostics::ISL {
enum class Metric { EngineUpdate, Count };
constexpr uint64_t UnknownFrame=~uint64_t{};
struct Row {
 Metric metric=Metric::EngineUpdate;
 int64_t enter=1,exit=11;
 uint64_t generation=2,frame=UnknownFrame;
 uint32_t thread=3;
 uintptr_t caller=4;
 uint16_t label=0;
 bool cpuKnown=true,cyclesKnown=false;
 uint64_t cpuStart100ns=100,cpu100ns=0,cycles=0;
};
struct Diagnostic {
 bool frozen=true; uint64_t capture=7; size_t contiguous=0;
 json frozenHeader={{"captureId",7}};
 std::vector<Row> frozenRows{Row{}};
} data;
auto& Data() { return data; }
const char* MetricName(Metric) { return "EngineUpdate"; }
json CallerJson(uintptr_t) { return {{"module","fixture"},{"rva",4}}; }
}
namespace CSX::Diagnostics::ReflexMeasurement {
struct Service {
 bool islPurpose=true,acquiring=false,captureReleased=false;
 struct Journal { bool frozen=true; uint64_t id=7; std::mutex mutex; } journal;
 json savedCapture; size_t contiguousExport=0;
} service;
auto& State() { return service; }
int64_t Clock() { return 123; }
json Export(const json&, bool) { return {{"captureId",7},{"total",1},{"records",json::array()}}; }
json ExportRecords(size_t,size_t) { return json::array({{{"stream","application"},{"ordinal",0},{"data",{{"frame",0},{"completed",false}}}}}); }
}
ACTUAL
void Check(bool value) { if (!value) std::abort(); }
int main(int argc,char** argv) {
 folder=argv[1];
 using namespace CSX::Diagnostics::ReflexMeasurement;
 service.acquiring=true;
 try { SaveCapture(); std::abort(); } catch(const std::logic_error&) {}
 service.acquiring=false;
 const auto result=SaveCapture();
 Check(result["saved"] && service.contiguousExport==1 && CSX::Diagnostics::ISL::data.contiguous==1);
 Check(SaveCapture()==result);
 const auto path=folder/"CSX-Captures"/"ISL-test.csxcap";
 Check(std::filesystem::file_size(path)==result["bytes"].get<uint64_t>());
 service.savedCapture=nullptr;
 service.contiguousExport=CSX::Diagnostics::ISL::data.contiguous=0;
 try { SaveCapture(); std::abort(); } catch(const std::runtime_error&) {}
 Check(service.contiguousExport==0 && CSX::Diagnostics::ISL::data.contiguous==0);
 Check(std::filesystem::file_size(path)==result["bytes"].get<uint64_t>());
}
'''.replace('ACTUAL', actual)
    compiler = os.environ.get('CXX') or shutil.which('cl')
    if not compiler:
        raise RuntimeError('MSVC environment required; run through the repository CMake launcher')
    spec = importlib.util.spec_from_file_location('capture_file', ROOT / 'tools/isl-diagnostics/capture_file.py')
    decoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(decoder)
    with tempfile.TemporaryDirectory(prefix='csx-capture-native-') as name:
        temp = Path(name)
        cpp, exe = temp / 'driver.cpp', temp / 'driver.exe'
        cpp.write_text(driver)
        subprocess.run([compiler, '/nologo', '/std:c++20', '/EHsc', '/O2', f'/I{ROOT / "src"}',
                        *[f'/I{entry}' for entry in args.json_include.split(';') if entry], str(cpp), f'/Fe:{exe}', 'windowsapp.lib'], cwd=temp, check=True, timeout=90)
        subprocess.run([str(exe), str(temp)], check=True, timeout=15)
        with (temp / 'CSX-Captures/ISL-test.csxcap').open('rb') as stream:
            raw, side, quality = decoder.read_capture(stream)
        assert quality['complete'], quality
        assert raw['records'][0]['data'] == {'frame': 0, 'completed': False}
        assert side['records'][0]['metric'] == 'EngineUpdate'
        assert side['records'][0]['cpu100ns'] == 0 and side['records'][0]['cycles'] is None
        assert side['records'][0]['frame'] is None and side['records'][0]['cpuStart100ns'] == 100
    print('Native save, repeated receipt, collision preservation, active-capture rejection and offline decode passed.')


if __name__ == '__main__':
    main()
