"""Exercise the production preset writer with real files and an injected OS move failure."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from extract_adaptive_balance_toggle import function

ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r'''
#include <atomic>
#include <cassert>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
constexpr unsigned MOVEFILE_REPLACE_EXISTING = 1, MOVEFILE_WRITE_THROUGH = 8;
bool failMove = false;
unsigned GetCurrentProcessId() { return 741; }
unsigned GetLastError() { return 5; }
template<class Char> bool MoveFileExW(const Char* from, const Char* to, unsigned flags) {
    if (failMove || (!(flags & MOVEFILE_REPLACE_EXISTING) && std::filesystem::exists(to)))
        return false;
    std::error_code error;
    std::filesystem::rename(from, to, error);
    return !error;
}
namespace logger {
    template<class... A> void warn(const char*, A&&...) {}
    template<class... A> void info(const char*, A&&...) {}
}
namespace Util {
    std::string PathToUtf8(const std::filesystem::path& path) {
        const auto bytes = path.u8string();
        return {bytes.begin(), bytes.end()};
    }
    namespace FileHelpers {
        int directWrites = 0;
        bool WriteTextFileDirect(const std::filesystem::path&, std::string_view, std::string&) {
            ++directWrites;
            throw std::runtime_error("Preset exports must never truncate their destination as fallback");
        }
        bool WriteTextFileAtomic(const std::filesystem::path&, std::string_view, std::string&, bool = true, bool = true);
        ATOMIC_WRITE
    }
}
PRESET_WRITE
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const auto root = std::filesystem::path(argv[1]);
    const auto directory = root / std::filesystem::path(u8"Shared_é_漢");
    const auto file = directory / "Default_AdaptiveBrightness_Full.json";
    std::string error = "old failure";
    assert(WriteJsonFileAtomic(file, {{"revision", 1}}, error) && error.empty());
    assert(json::parse(Bytes(file))["revision"] == 1);
    auto temporary = file;
    temporary += ".tmp";
    auto backup = file;
    backup += ".bak";
    std::ofstream(temporary) << "unrelated temporary data";
    std::ofstream(backup) << "unrelated backup data";
    assert(WriteJsonFileAtomic(file, {{"revision", 2}}, error));
    const auto original = Bytes(file);
    failMove = true;
    assert(!WriteJsonFileAtomic(file, {{"revision", 3}}, error));
    assert(!error.empty() && error.find(Util::PathToUtf8(file)) != std::string::npos);
    assert(Bytes(file) == original && Util::FileHelpers::directWrites == 0);
    failMove = false;
    assert(WriteJsonFileAtomic(file, {{"revision", 4}}, error) && error.empty());
    assert(json::parse(Bytes(file))["revision"] == 4);
    const auto recovered = Bytes(file);
    assert(!Util::FileHelpers::WriteTextFileAtomic(file, "overwrite", error, true, false));
    assert(Bytes(file) == recovered && Util::FileHelpers::directWrites == 0);
    assert(Bytes(temporary) == "unrelated temporary data");
    assert(Bytes(backup) == "unrelated backup data");
    unsigned entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        assert(entry.path() == file || entry.path() == temporary || entry.path() == backup);
        ++entries;
    }
    assert(entries == 3);
    const auto invalid = file / "subdirectory" / "preset.json";
    assert(!WriteJsonFileAtomic(invalid, {{"revision", 5}}, error) && !error.empty());
    assert(Bytes(file) == recovered);
    std::cout << "PASS: atomic Unicode preset exports, failure retention, cleanup and recovery\n";
}
'''


class PresetExportWriteTest(unittest.TestCase):
    def test_export_success_failure_and_recovery(self):
        compiler = os.environ.get("CXX") or shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++23 compiler is required")
        atomic = function((ROOT / "src/Utils/FileSystem.cpp").read_text(), "bool WriteTextFileAtomic(")
        preset = function((ROOT / "src/Features/AdaptiveBrightness.cpp").read_text(), "bool WriteJsonFileAtomic(")
        with tempfile.TemporaryDirectory(prefix="csx-preset-write-") as temporary:
            root = Path(temporary)
            source = root / "fixture.cpp"
            source.write_text(PRELUDE.replace("ATOMIC_WRITE", atomic).replace("PRESET_WRITE", preset), encoding="utf-8")
            executable = root / ("fixture.exe" if os.name == "nt" else "fixture")
            include = os.environ.get("CSX_JSON_INCLUDE")
            self.assertTrue(include, "CSX_JSON_INCLUDE must contain the official nlohmann JSON headers")
            if Path(compiler).name.lower() in {"cl", "cl.exe"}:
                command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX", "/utf-8",
                           f"/I{include}", str(source), f"/Fe:{executable}"]
            else:
                command = [compiler, "-std=c++23", "-Wall", "-Wextra", "-Werror", "-I", include,
                           str(source), "-o", str(executable)]
            result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable), str(root / "data")], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
