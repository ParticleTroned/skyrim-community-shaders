"""Compile the complete production SSS helper namespace and exercise its contracts.

Keeping the entire namespace catches duplicate helpers and declaration-order
failures that isolated function/UI extraction cannot detect. The real settings
declarations and JSON implementation are used; only NPC and immediate UI owners
are supplied by the fixture.
"""

from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

from extract_adaptive_balance_toggle import between


ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
using uint = unsigned;
struct float3 { float x, y, z; };
struct float4 { float x, y, z, w; };
namespace RE {
struct TESNPC { unsigned sex = 0; unsigned GetSex() const { return sex; } };
}
constexpr int ImGuiSliderFlags_AlwaysClamp = 1;
const char* T(const char*, const char* fallback) { return fallback; }
namespace ImGui {
unsigned idDepth = 0;
void SeparatorText(const char* text) { assert(std::string_view(text) == "Fixture"); }
void PushID(const char*) { ++idDepth; }
void PopID() { assert(idDepth); --idDepth; }
}
namespace Util::Widgets {
std::array<float*, 4> sliderValues{};
unsigned sliderCount = 0;
bool SliderFloat(const char*, float* value, float minimum, float maximum, const char*, int flags) {
    assert(minimum == 0.0f && maximum == 2.0f && flags == ImGuiSliderFlags_AlwaysClamp);
    assert(sliderCount < sliderValues.size());
    sliderValues[sliderCount++] = value;
    return false;
}
}
'''

CHECKS = r'''
int main() {
    static_assert(kHumanSkinControlMin == 0.0f && kHumanSkinControlMax == 2.0f);
    assert(ClampHumanSkinControl(-1.0f) == 0.0f);
    assert(ClampHumanSkinControl(1.25f) == 1.25f);
    assert(ClampHumanSkinControl(3.0f) == 2.0f);
    RE::TESNPC npc;
    assert(!GetNPCIsFemale(nullptr) && !GetNPCIsFemale(&npc));
    npc.sex = 1;
    assert(GetNPCIsFemale(&npc));
    struct NPCWithBothAPIs {
        bool female = false;
        bool IsFemale() const { return female; }
        unsigned GetSex() const { return 1; }
    } both;
    assert(!IsFemaleImpl(&both, 0));
    both.female = true;
    assert(IsFemaleImpl(&both, 0));

    SubsurfaceScattering::Settings settings;
    settings.HumanMaleSSSIntensity = -2.0f;
    settings.HumanMaleSSSSaturation = 5.0f;
    settings.HumanMaleSSSBrightness = 0.5f;
    settings.HumanMaleSSSBaseSaturation = 1.5f;
    settings.HumanFemaleSSSIntensity = 3.0f;
    settings.HumanFemaleSSSSaturation = -1.0f;
    settings.HumanFemaleSSSBrightness = 0.25f;
    settings.HumanFemaleSSSBaseSaturation = 1.75f;
    settings.SSMode = -1;
    settings.ScatterMode = 12;
    settings.BurleySamples = 0;
    SanitizeSettings(settings);
    assert(settings.HumanMaleSSSIntensity == 0.0f && settings.HumanMaleSSSSaturation == 2.0f);
    assert(settings.HumanMaleSSSBrightness == 0.5f && settings.HumanMaleSSSBaseSaturation == 1.5f);
    assert(settings.HumanFemaleSSSIntensity == 2.0f && settings.HumanFemaleSSSSaturation == 0.0f);
    assert(settings.HumanFemaleSSSBrightness == 0.25f && settings.HumanFemaleSSSBaseSaturation == 1.75f);
    assert(settings.SSMode == 0 && settings.ScatterMode == SubsurfaceScattering::kPreAndPostScatter);
    assert(settings.BurleySamples == 1);
    settings.SSMode = 3;
    settings.ScatterMode = -2;
    settings.BurleySamples = 100;
    SanitizeSettings(settings);
    assert(settings.SSMode == 1 && settings.ScatterMode == SubsurfaceScattering::kPreScatter);
    assert(settings.BurleySamples == 64);

    DrawHumanSkinControls("Fixture", settings.HumanMaleSSSIntensity, settings.HumanMaleSSSSaturation,
                          settings.HumanMaleSSSBrightness, settings.HumanMaleSSSBaseSaturation);
    assert(ImGui::idDepth == 0 && Util::Widgets::sliderCount == 4);
    assert(Util::Widgets::sliderValues[0] == &settings.HumanMaleSSSIntensity);
    assert(Util::Widgets::sliderValues[1] == &settings.HumanMaleSSSSaturation);
    assert(Util::Widgets::sliderValues[2] == &settings.HumanMaleSSSBrightness);
    assert(Util::Widgets::sliderValues[3] == &settings.HumanMaleSSSBaseSaturation);

    float male = 0.25f, female = 0.75f;
    ApplyLegacyHumanControl(json{{"legacy", 1.5f}}, "legacy", "male", "female", male, female);
    assert(male == 1.5f && female == 1.5f);
    male = 0.25f; female = 0.75f;
    ApplyLegacyHumanControl(json{{"legacy", 1.5f}, {"male", 0.25f}}, "legacy", "male", "female", male, female);
    assert(male == 0.25f && female == 1.5f);
    ApplyLegacyHumanControl(json{{"legacy", "invalid"}}, "legacy", "male", "female", male, female);
    assert(male == 0.25f && female == 1.5f);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-revision", help="Read the implementation at a Git revision for regression verification")
    parser.add_argument("--json-include", type=Path, default=os.environ.get("CSX_JSON_INCLUDE"))
    args = parser.parse_args()

    def read(relative):
        if args.source_revision:
            return subprocess.check_output(["git", "show", f"{args.source_revision}:{relative}"], cwd=ROOT, text=True)
        return (ROOT / relative).read_text(encoding="utf-8")

    source = read("src/Features/SubsurfaceScattering.cpp")
    header = read("src/Features/SubsurfaceScattering.h")
    production = "struct SubsurfaceScattering {\n"
    production += between(header, "\tstruct DiffusionProfile", "\n\tSettings settings;")
    production += "};\n"
    production += between(source, "namespace\n{", "\nvoid SubsurfaceScattering::DrawSettings()")
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++20 compiler is required for the SSS helper fixture")
    with tempfile.TemporaryDirectory(prefix="se-sss-helpers-") as temporary:
        output = Path(temporary)
        source_path = output / "fixture.cpp"
        source_path.write_text(PRELUDE + production + CHECKS, encoding="utf-8")
        executable = output / ("fixture.exe" if os.name == "nt" else "fixture")
        if Path(compiler).stem.lower() in ("cl", "clang-cl"):
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX"]
            if args.json_include:
                command += [f"/I{args.json_include}"]
            command += [str(source_path), f"/Fe:{executable}"]
        else:
            command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror"]
            if args.json_include:
                command += ["-I", str(args.json_include)]
            command += [str(source_path), "-o", str(executable)]
        subprocess.run(command, cwd=output, check=True)
        subprocess.run([str(executable)], cwd=output, check=True)
    print("Complete production SSS helper namespace compiles; skin controls, sex lookup, sanitization and migration pass")


if __name__ == "__main__":
    main()
