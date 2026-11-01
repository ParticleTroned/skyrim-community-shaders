"""Execute the production SE feature admission and orphan scanning policies."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from extract_adaptive_balance_toggle import function


ROOT = Path(__file__).resolve().parents[1]


class FeatureIssueScannerTest(unittest.TestCase):
    def test_metadata_aliases_unknown_obsolete_and_hidden_features(self):
        compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("cl")
        self.assertIsNotNone(compiler, "A C++23 compiler is required")
        source = (ROOT / "src/FeatureIssues.cpp").read_text(encoding="utf-8")
        feature = (ROOT / "src/Feature.cpp").read_text(encoding="utf-8")
        legacy = (ROOT / "src/LegacyUtilityCompatibility.cpp").read_text(encoding="utf-8")
        driver = r'''
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>
namespace REL { struct Version { unsigned major, minor, patch; }; }
#include "FeatureVersions.h"
#include "LegacyUtilityCompatibility.h"
LEGACY_ALIAS
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
    bool hidden = false;
    std::string version = "1-0-0";
    std::string failedLoadedMessage;
    std::string GetShortName() const { return name; }
    bool IsHiddenFromUserView() const { return hidden; }
    static bool IsFeatureKnown(const std::string&, REL::Version* = nullptr);
    static const std::vector<Feature*>& GetFeatureList();
};
Feature foliage{ "FoliageLighting", true, false, "1-0-0", {} };
Feature hiddenFeature{ "Skin", false, true, "1-0-0", "Internal feature" };
const std::vector<Feature*>& Feature::GetFeatureList() {
    static const std::vector<Feature*> list{ &foliage, &hiddenFeature };
    return list;
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
    if (!result) { std::cerr << message << '\n'; std::exit(1); }
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
    Util::PathHelpers::featuresPath = std::filesystem::path(argv[1]) / "features";
    ScanForOrphanedFeatureINIs(false);
    Check(issues.empty(), "Missing feature directory must be accepted");
    std::filesystem::create_directory(Util::PathHelpers::featuresPath);
    for (const char* alias : { "CSUtility", "OSUtility", "CS Utility", "OS Utility" }) {
        REL::Version version{};
        Check(Feature::IsFeatureKnown(alias, &version), "Legacy alias admission failed");
        Check(version.major == 1 && version.minor == 6 && version.patch == 0, "Legacy alias version differs");
        WriteINI((std::string(alias) + ".ini").c_str());
    }
    for (const char* name : { "Wetterness", "GrassOptimizations", "NeuralRendering", "WeatherPicker", "TextureStreaming" })
        Check(Feature::IsFeatureKnown(name), "New feature is absent from metadata");
    Check(!Feature::IsFeatureKnown("CSUtilityExtra"), "Aliases must match exactly");
    Check(!Feature::IsFeatureKnown(""), "Empty names must be rejected");
    WriteINI("FoliageLighting.ini");
    WriteINI("WeatherPicker.ini");
    WriteINI("Skin.ini");
    std::filesystem::create_directory(Util::PathHelpers::featuresPath / "Ignored.ini");
    std::ofstream(Util::PathHelpers::featuresPath / "Ignored.txt") << "ignored";
    ScanForOrphanedFeatureINIs(true);
    Check(issues.empty(), "Known metadata-only aliases or hidden features were reported");
    WriteINI("NotAFeature.ini");
    ScanForOrphanedFeatureINIs(false);
    Check(issues.size() == 1 && issues[0].shortName == "NotAFeature" && issues[0].issueType == Type::UNKNOWN,
        "Unknown INI warning was lost");
    std::filesystem::remove(Util::PathHelpers::featuresPath / "NotAFeature.ini");
    issues.clear();
    WriteINI("WaterBlending.ini");
    ScanForOrphanedFeatureINIs(false);
    Check(issues.size() == 1 && issues[0].issueType == Type::OBSOLETE, "Obsolete INI warning was lost");
    std::filesystem::remove(Util::PathHelpers::featuresPath / "WaterBlending.ini");
    issues.clear();
    s_obsoleteFeatureData.insert("WeatherPicker");
    ScanForOrphanedFeatureINIs(false);
    Check(issues.size() == 1 && issues[0].issueType == Type::OBSOLETE, "Obsolete classification must precede metadata");
    s_obsoleteFeatureData.erase("WeatherPicker");
    issues.clear();
    foliage.loaded = false;
    foliage.failedLoadedMessage = "Version mismatch";
    ScanForOrphanedFeatureINIs(false);
    Check(issues.empty(), "Unrequested loaded-feature scan ran");
    ScanForOrphanedFeatureINIs(true);
    Check(issues.size() == 1 && issues[0].shortName == "FoliageLighting" && issues[0].issueType == Type::VERSION_MISMATCH,
        "Failed loaded-feature issue was lost or hidden issue leaked");
    std::cout << "SE metadata, aliases, unknown, obsolete and hidden-feature scans passed\n";
}
'''
        for marker, body in [
            ("LEGACY_ALIAS", function(legacy, "bool LegacyUtilityCompatibility::IsAlias(")),
            ("KNOWN_FEATURE", function(feature, "bool Feature::IsFeatureKnown(")),
            ("OBSOLETE_FEATURE", function(source, "bool IsObsoleteFeature(")),
            ("ORPHAN_SCANNER", function(source, "void ScanForOrphanedFeatureINIs(")),
        ]:
            driver = driver.replace(marker, body)
        with tempfile.TemporaryDirectory(prefix="csx-feature-scanner-") as temporary:
            directory = Path(temporary)
            cpp = directory / "scanner.cpp"
            exe = directory / "scanner.exe"
            cpp.write_text(driver, encoding="utf-8")
            if Path(compiler).name.lower() in ("cl", "cl.exe"):
                command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/O2", "/W4", "/WX",
                           f"/I{ROOT / 'include'}", f"/I{ROOT / 'src'}", str(cpp), f"/Fe:{exe}"]
            else:
                command = [compiler, "-std=c++23", "-O1", f"-I{ROOT / 'include'}", f"-I{ROOT / 'src'}", str(cpp), "-o", str(exe)]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(exe), str(directory)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
