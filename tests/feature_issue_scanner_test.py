"""Execute the production orphan scanner with real version registrations."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class FeatureIssueScannerTests(unittest.TestCase):
    def test_metadata_unknown_obsolete_and_failed_features(self):
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        source = (ROOT / "src/FeatureIssues.cpp").read_text(encoding="utf-8")
        feature = (ROOT / "src/Feature.cpp").read_text(encoding="utf-8")
        driver = r'''
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>
namespace REL {
struct Version { unsigned major, minor, patch; };
struct Module {
    static inline bool vr = false;
    static bool IsVR() { return vr; }
};
}
#include "FeatureVersions.h"
namespace logger {
template<class... Args> void info(Args&&...) {}
template<class... Args> void warn(Args&&...) {}
}
namespace Util::PathHelpers {
std::filesystem::path featuresPath;
std::filesystem::path GetFeaturesPath() { return featuresPath; }
}
struct CSimpleIniA {
    void SetUnicode() {}
    template<class Path> void LoadFile(Path) {}
    const char* GetValue(const char*, const char*) { return "1-0-0"; }
};
struct Feature {
    std::string name;
    bool loaded = true;
    bool supportsVR = true;
    std::string version = "1-0-0";
    std::string failedLoadedMessage;
    std::string GetShortName() const { return name; }
    bool SupportsVR() const { return supportsVR; }
    static bool IsFeatureKnown(const std::string&, REL::Version* = nullptr);
    static const std::vector<Feature*>& GetFeatureList();
    static Feature* FindRegisteredFeatureByShortName(const std::string&);
};
Feature foliage{ "FoliageLighting", true, true, "1-0-0", {} };
Feature flatOnly{ "LinearLighting", true, false, "1-0-0", {} };
const std::vector<Feature*>& Feature::GetFeatureList() {
    static const std::vector<Feature*> vr{ &foliage };
    static const std::vector<Feature*> flat{ &foliage, &flatOnly };
    return REL::Module::IsVR() ? vr : flat;
}
Feature* Feature::FindRegisteredFeatureByShortName(const std::string& name) {
    for (auto* candidate : { &foliage, &flatOnly })
        if (candidate->name == name)
            return candidate;
    return nullptr;
}
KNOWN_FEATURE
namespace FeatureIssues {
struct FeatureIssueInfo {
    enum class IssueType { UNKNOWN, OBSOLETE, VERSION_MISMATCH };
    std::string shortName;
    IssueType issueType;
};
struct FeatureFileInfo {};
std::vector<FeatureIssueInfo> issues;
std::set<std::string> s_obsoleteFeatureData{ "WaterBlending" };
FeatureFileInfo GetFeatureFileInfo(const std::string&) { return {}; }
void AddFeatureIssue(const std::string& name, const std::string&, const std::string&,
    FeatureIssueInfo::IssueType type, FeatureFileInfo) {
    issues.push_back({ name, type });
}
OBSOLETE_FEATURE
ORPHAN_SCANNER
}
void Check(bool result, const char* message) {
    if (!result) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
void WriteINI(const char* name) {
    std::ofstream stream(Util::PathHelpers::featuresPath / name);
    stream << "[Info]\nVersion = 1-0-0\n";
    Check(stream.good(), "Could not create test INI");
}
int main(int argc, char** argv) {
    using namespace FeatureIssues;
    using Type = FeatureIssueInfo::IssueType;
    Check(argc == 2, "Missing test directory");
    for (const auto* runtime : { "SE", "AE", "VR" }) {
        REL::Module::vr = std::string(runtime) == "VR";
        Util::PathHelpers::featuresPath = std::filesystem::path(argv[1]) / runtime;
        issues.clear();
        ScanForOrphanedFeatureINIs(false);
        Check(issues.empty(), "Absent feature directory must be accepted");
        std::filesystem::create_directory(Util::PathHelpers::featuresPath);
        WriteINI("PerformanceTuning.ini");
        WriteINI("FoliageLighting.ini");
        WriteINI("LinearLighting.ini");
        if (!REL::Module::vr)
            WriteINI("VR.ini");
        std::filesystem::create_directory(Util::PathHelpers::featuresPath / "Ignored.ini");
        std::ofstream(Util::PathHelpers::featuresPath / "Ignored.txt") << "ignored";
        Check(Feature::IsFeatureKnown("PerformanceTuning"), "Missing real version registration");
        Check(FeatureVersions::FEATURE_CORE_NAMES.contains("PerformanceTuning"), "Missing core registration");
        Check(!Feature::FindRegisteredFeatureByShortName("PerformanceTuning"), "Fixture must model a built-in menu");
        ScanForOrphanedFeatureINIs(false);
        Check(issues.empty(), "Known metadata-only INI was incorrectly reported");
        WriteINI("NotAFeature.ini");
        ScanForOrphanedFeatureINIs(false);
        Check(issues.size() == 1 && issues[0].shortName == "NotAFeature" &&
            issues[0].issueType == Type::UNKNOWN, "Unknown INI warning was lost");
        std::filesystem::remove(Util::PathHelpers::featuresPath / "NotAFeature.ini");
        issues.clear();
        WriteINI("WaterBlending.ini");
        ScanForOrphanedFeatureINIs(false);
        Check(issues.size() == 1 && issues[0].shortName == "WaterBlending" &&
            issues[0].issueType == Type::OBSOLETE, "Obsolete INI warning was lost");
        std::filesystem::remove(Util::PathHelpers::featuresPath / "WaterBlending.ini");
        issues.clear();
        s_obsoleteFeatureData.insert("PerformanceTuning");
        ScanForOrphanedFeatureINIs(false);
        Check(issues.size() == 1 && issues[0].issueType == Type::OBSOLETE,
            "Obsolete classification must take precedence over a version entry");
        s_obsoleteFeatureData.erase("PerformanceTuning");
        issues.clear();
        foliage.loaded = false;
        foliage.failedLoadedMessage = "Version mismatch";
        ScanForOrphanedFeatureINIs(false);
        Check(issues.empty(), "Unrequested loaded-feature scan ran");
        ScanForOrphanedFeatureINIs(true);
        Check(issues.size() == 1 && issues[0].shortName == "FoliageLighting" &&
            issues[0].issueType == Type::VERSION_MISMATCH, "Failed loaded-feature issue was lost");
        foliage.loaded = true;
        foliage.failedLoadedMessage.clear();
    }
    std::cout << "SE, AE and VR metadata, unknown, obsolete and failed-feature scans passed\n";
}
'''
        for marker, value in (
            ("KNOWN_FEATURE", block(feature, "bool Feature::IsFeatureKnown(")),
            ("OBSOLETE_FEATURE", block(source, "bool IsObsoleteFeature(")),
            ("ORPHAN_SCANNER", block(source, "void ScanForOrphanedFeatureINIs(")),
        ):
            driver = driver.replace(marker, value)
        with tempfile.TemporaryDirectory(prefix="csx-feature-scanner-") as temporary:
            directory = Path(temporary)
            cpp = directory / "scanner.cpp"
            exe = directory / "scanner.exe"
            cpp.write_text(driver, encoding="utf-8")
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX",
                       f"/I{ROOT / 'include'}", str(cpp), f"/Fe:{exe}"]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe), str(directory)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
