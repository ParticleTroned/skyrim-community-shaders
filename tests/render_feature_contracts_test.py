"""Exercise migrated feature ownership and stable weather registry identifiers.

Production callbacks and the weather ownership lookup are compiled together.
Only the weather registry owner, translations and shader-cache storage are
observable doubles; the underwater API header is included without rewriting.
"""

from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

from extract_adaptive_balance_toggle import function


ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r'''
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <string_view>
struct WeatherManager {
    std::map<std::string, std::set<std::string>> activeWeatherVariables;
    static WeatherManager* GetSingleton() { static WeatherManager instance; return &instance; }
    bool IsVariableOverrideActive(const std::string& featureName, const std::string& variableName) const;
};
namespace globals { WeatherManager* weatherManager = WeatherManager::GetSingleton(); }
struct Feature {
    std::string shortName = "ImageBasedLighting";
    virtual ~Feature() = default;
    virtual std::string GetShortName() { return shortName; }
    virtual void ClearShaderCache() {}
};
const char* T(const char* key, const char*) {
    const std::string_view name(key);
    if (name == "feature.ibl.imagebasedlighting") return "Translated feature name";
    if (name == "feature.ibl.enableibl") return "Translated variable name";
    return "Localized explanation";
}
struct IBL { const char* GetPerformanceToggleBlockReason() const; };
'''

DOF_PRELUDE = r'''
#include "UnderwaterDepthOfField.h"
struct ShaderCache {
    bool shader = true, failed = true;
    unsigned resets = 0;
    void Reset() { shader = false; failed = false; ++resets; }
};
ShaderCache depthOfFieldInputVS, depthOfFieldInputPS;
struct CSUtility { void ClearShaderCache(); };
'''

IBL_CHECKS = r'''
    IBL ibl;
    auto& active = WeatherManager::GetSingleton()->activeWeatherVariables;
    assert(!ibl.GetPerformanceToggleBlockReason());
    active["ImageBasedLighting"].insert("EnableIBL");
    const char* reason = ibl.GetPerformanceToggleBlockReason();
    assert(reason && std::string_view(reason) == "Localized explanation");
    active["ImageBasedLighting"].clear();
    active["ImageBasedLighting"].insert("EnvIBLScale");
    assert(!ibl.GetPerformanceToggleBlockReason());
'''

WEATHER_CHECKS = r'''
    using Util::WeatherUI::IsWeatherControlled;
    Feature feature;
    auto* manager = WeatherManager::GetSingleton();
    auto& active = manager->activeWeatherVariables;
    active.clear();
    assert(!IsWeatherControlled(&feature, "EnableIBL"));
    // During a transition the manager owns both validated source and target
    // variables. The UI must use that union rather than parsing only the target.
    active["ImageBasedLighting"] = {"EnableIBL", "SourceOnly"};
    assert(IsWeatherControlled(&feature, "EnableIBL"));
    assert(IsWeatherControlled(&feature, "SourceOnly"));
    assert(!IsWeatherControlled(&feature, "Translated variable name"));
    assert(!IsWeatherControlled(&feature, "EnvIBLScale"));
    active["ImageBasedLighting"].erase("SourceOnly");
    assert(!IsWeatherControlled(&feature, "SourceOnly"));
    assert(IsWeatherControlled(&feature, "EnableIBL"));
    // Pausing/releasing ownership clears the manager's active variables.
    active.clear();
    assert(!IsWeatherControlled(&feature, "EnableIBL"));
    active["ImageBasedLighting"].insert("EnableIBL");
    feature.shortName = "AnotherFeature";
    assert(!IsWeatherControlled(&feature, "EnableIBL"));
    feature.shortName = "ImageBasedLighting";
    assert(!IsWeatherControlled(nullptr, "EnableIBL"));
    assert(!IsWeatherControlled(&feature, nullptr));
    assert(!IsWeatherControlled(&feature, ""));
    globals::weatherManager = nullptr;
    assert(!IsWeatherControlled(&feature, "EnableIBL"));
    globals::weatherManager = manager;
'''

DOF_CHECKS = r'''
    CSUtility legacy;
    legacy.ClearShaderCache();
    assert(depthOfFieldInputVS.resets == 1 && depthOfFieldInputPS.resets == 1);
    assert(!depthOfFieldInputVS.shader && !depthOfFieldInputVS.failed);
    assert(!depthOfFieldInputPS.shader && !depthOfFieldInputPS.failed);
    depthOfFieldInputVS.shader = depthOfFieldInputPS.shader = true;
    depthOfFieldInputVS.failed = depthOfFieldInputPS.failed = true;
    AdaptiveBrightness active;
    Feature& owner = active;
    owner.ClearShaderCache();
    assert(depthOfFieldInputVS.resets == 2 && depthOfFieldInputPS.resets == 2);
    assert(!depthOfFieldInputVS.shader && !depthOfFieldInputVS.failed);
    assert(!depthOfFieldInputPS.shader && !depthOfFieldInputPS.failed);
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-revision", help="Read the implementation at a Git revision for regression verification")
    parser.add_argument("--case", choices=("all", "ibl", "weather", "dof"), default="all")
    args = parser.parse_args()

    def read(relative):
        if args.source_revision:
            return subprocess.check_output(["git", "show", f"{args.source_revision}:{relative}"], cwd=ROOT, text=True)
        return (ROOT / relative).read_text(encoding="utf-8")

    production = PRELUDE
    checks = "int main() {\n"
    if args.case in ("all", "ibl", "weather"):
        production += function(read("src/WeatherManager.cpp"), "bool WeatherManager::IsVariableOverrideActive(")
    if args.case in ("all", "ibl"):
        production += function(read("src/Features/IBL.cpp"), "const char* IBL::GetPerformanceToggleBlockReason()")
        checks += "{\n" + IBL_CHECKS + "}\n"
    if args.case in ("all", "weather"):
        production += "namespace Util::WeatherUI {\n"
        production += function(read("src/Utils/UI.cpp"), "\t\tbool IsWeatherControlled(")
        production += "}\n"
        checks += "{\n" + WEATHER_CHECKS + "}\n"
    if args.case in ("all", "dof"):
        production += DOF_PRELUDE
        header = read("src/Features/AdaptiveBrightness.h")
        declaration = "\tvirtual void ClearShaderCache() override;"
        production += "struct AdaptiveBrightness : Feature {\n"
        if declaration in header:
            production += declaration + "\n"
        production += "};\n"
        underwater = read("src/Features/UnderwaterDepthOfField.cpp")
        signature = "\tvoid ClearShaderCache()"
        if signature in underwater:
            production += "namespace UnderwaterDepthOfField {\n" + function(underwater, signature) + "}\n"
        adaptive = read("src/Features/AdaptiveBrightness.cpp")
        if "void AdaptiveBrightness::ClearShaderCache()" in adaptive:
            production += function(adaptive, "void AdaptiveBrightness::ClearShaderCache()")
        production += function(read("src/Features/CSUtility.cpp"), "void CSUtility::ClearShaderCache()")
        checks += "{\n" + DOF_CHECKS + "}\n"
    checks += "}\n"

    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++20 compiler is required for the feature owner fixture")
    with tempfile.TemporaryDirectory(prefix="se-feature-contracts-") as temporary:
        output = Path(temporary)
        source_path = output / "fixture.cpp"
        source_path.write_text(production + checks, encoding="utf-8")
        (output / "UnderwaterDepthOfField.h").write_text(read("src/Features/UnderwaterDepthOfField.h"), encoding="utf-8")
        executable = output / ("fixture.exe" if os.name == "nt" else "fixture")
        if Path(compiler).stem.lower() in ("cl", "clang-cl"):
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", str(source_path), f"/Fe:{executable}"]
        else:
            command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", str(source_path), "-o", str(executable)]
        subprocess.run(command, cwd=output, check=True)
        subprocess.run([str(executable)], cwd=output, check=True)
    print("Production IBL and weather UI share stable-ID ownership, transition union and null guards; both DOF owners reset shader caches")


if __name__ == "__main__":
    main()
