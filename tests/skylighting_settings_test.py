"""Execute Skylighting's production settings normalizer without engine dependencies."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def block(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    position = opening + 1
    while depth:
        depth += (source[position] == "{") - (source[position] == "}")
        position += 1
    return source[start:position]


class SkylightingSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.compiler = os.environ.get("CXX") or next(
            (path for name in ("cl", "c++", "clang++", "g++") if (path := shutil.which(name))),
            None,
        )
        if cls.compiler is None:
            raise unittest.SkipTest("A C++17 compiler is required; use the MSVC developer environment on Windows")

        header = (ROOT / "src/Features/Skylighting.h").read_text(encoding="utf-8")
        source = (ROOT / "src/Features/Skylighting.cpp").read_text(encoding="utf-8")
        cls.normalizer = block(source, "void NormalizeSettingsForRuntime(")
        cls.prelude = "\n".join(
            [
                "#include <algorithm>\n#include <array>\n#include <cmath>\n#include <iostream>\n#include <limits>",
                "using uint = unsigned int;",
                "struct Skylighting {" + block(header, "struct Settings") + "; };",
                block(source, "struct ProbeGridPreset") + ";",
                block(source, "constexpr std::array<ProbeGridPreset, 3> kProbeGridPresets") + ";",
                "constexpr uint kQualityProbeGrid = static_cast<uint>(kProbeGridPresets.size() - 1);",
                block(source, "uint ClampProbeGridQuality("),
                block(source, "uint ClampUpdateInterval("),
                block(source, "float ClampProbeFieldSize("),
            ]
        )

    def execute(self, normalizer: str) -> subprocess.CompletedProcess[str]:
        driver = r"""
int main()
{
    const float defaultAngle = Skylighting::Settings{}.MaxZenith;
    struct Case { const char* name; float input; float expected; };
    const Case cases[] = {
        { "negative", -1.0f, 0.0f },
        { "large negative", -std::numeric_limits<float>::max(), 0.0f },
        { "above horizon", 2.0f, defaultAngle },
        { "large positive", std::numeric_limits<float>::max(), defaultAngle },
        { "NaN", std::numeric_limits<float>::quiet_NaN(), defaultAngle },
        { "positive infinity", std::numeric_limits<float>::infinity(), defaultAngle },
        { "negative infinity", -std::numeric_limits<float>::infinity(), defaultAngle },
        { "vertical", 0.0f, 0.0f },
        { "small positive", std::numeric_limits<float>::min(), std::numeric_limits<float>::min() },
        { "intermediate", 0.7f, 0.7f },
        { "below horizon", std::nextafter(defaultAngle, 0.0f), std::nextafter(defaultAngle, 0.0f) },
        { "default", defaultAngle, defaultAngle },
    };
    for (const auto& entry : cases) {
        Skylighting::Settings settings;
        settings.MaxZenith = entry.input;
        NormalizeSettingsForRuntime(settings);
        if (!std::isfinite(settings.MaxZenith) || settings.MaxZenith != entry.expected) {
            std::cerr << entry.name << ": got " << settings.MaxZenith << ", expected " << entry.expected << '\n';
            return 1;
        }
        NormalizeSettingsForRuntime(settings);
        if (settings.MaxZenith != entry.expected)
            return 2;
    }
    std::cout << "12 angle cases passed, including repeated normalization\n";
}
"""
        with tempfile.TemporaryDirectory(prefix="csx-skylighting-settings-") as temporary:
            directory = Path(temporary)
            source = directory / "settings.cpp"
            executable = directory / "settings.exe"
            source.write_text(self.prelude + "\n" + normalizer + "\n" + driver, encoding="utf-8")
            if Path(self.compiler).stem.lower() == "cl":
                command = [self.compiler, "/nologo", "/std:c++17", "/EHsc", "/O2", "/fp:fast", "/W4", "/WX", str(source), f"/Fe:{executable}"]
            else:
                command = [self.compiler, "-std=c++17", "-Wall", "-Wextra", str(source), "-o", str(executable)]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            return subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)

    def test_max_zenith_boundaries_and_valid_values(self) -> None:
        result = self.execute(self.normalizer)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_angle_normalization_is_detected(self) -> None:
        normalizer = "\n".join(line for line in self.normalizer.splitlines() if "MaxZenith" not in line)
        result = self.execute(normalizer)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("negative: got -1", result.stderr)


if __name__ == "__main__":
    unittest.main()
