"""Generate and execute the production shader-deploy CMake rule in a safe fixture."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ShaderDeployBuildRuleTest(unittest.TestCase):
    def test_generated_command_quotes_paths_and_preserves_runtime_and_unchanged_mtimes(self):
        cmake = os.environ.get("CMAKE") or os.environ.get("CMAKE_COMMAND") or os.environ.get("CSX_CMAKE") or shutil.which("cmake")
        self.assertIsNotNone(cmake, "CMake is required for the production build rule")
        source = (ROOT / "CMakeLists.txt").read_text()
        marker = source.index("# One canonical shader-sync rule")
        start = source.index("add_custom_command(", marker)
        end = source.index("\n            )", start) + len("\n            )")
        production_rule = source[start:end]
        self.assertIn("${DEPLOY_TARGET_HASH}_shaders.stamp", production_rule)
        with tempfile.TemporaryDirectory(prefix="csx-deploy-rule-") as directory:
            root = Path(directory) / "paths with spaces"
            root.mkdir()
            stage = root / "staged/Shaders"
            destination = root / "game/Shaders"
            for path in [stage / "Upscaling/StreamlineDX12", destination / "Upscaling/StreamlineDX12"]:
                path.mkdir(parents=True)
            (stage / "Core.hlsl").write_text("current shader")
            (stage / "Upscaling/StreamlineDX12/nvngx_dlssg.dll").write_text("staged FG")
            (destination / "Upscaling/StreamlineDX12/nvngx_dlssg.dll").write_text("installed FG")
            (destination / "user-owned.hlsl").write_text("another mod")
            (root / "runtime-guard.cmake").write_text("# dependency-only fixture\n")
            (root / "paths.txt").write_text(str(stage))
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 4.2)\nproject(DeployRule NONE)\n'
                f'set(CMAKE_SOURCE_DIR "{ROOT.as_posix()}")\n'
                f'set(AIO_DIR "{(root / "staged").as_posix()}")\n'
                f'set(DEPLOY_TARGET "{(root / "game").as_posix()}")\n'
                'set(DEPLOY_TARGET_HASH fixture)\nset(SKIP_RUNTIME_DOWNLOADS ON)\n'
                f'set(_shader_copy_inputs "{(stage / "Core.hlsl").as_posix()}")\n'
                f'set(_feature_shader_paths_list_file "{(root / "paths.txt").as_posix()}")\n'
                f'set(CSX_RUNTIME_INSTALL_GUARD "{(root / "runtime-guard.cmake").as_posix()}")\n'
                'add_custom_target(prepare_shaders)\n' + production_rule + '\n'
                'add_custom_target(run DEPENDS fixture_shaders.stamp)\n'
            )
            build = root / "build"
            commands = [[cmake, "-S", str(root), "-B", str(build)], [cmake, "--build", str(build), "--target", "run"]]
            for command in commands:
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((destination / "Core.hlsl").read_text(), "current shader")
            self.assertEqual((destination / "Upscaling/StreamlineDX12/nvngx_dlssg.dll").read_text(), "installed FG")
            self.assertEqual((destination / "user-owned.hlsl").read_text(), "another mod")
            expected_time = 1_700_000_000_000_000_000
            os.utime(destination / "Core.hlsl", ns=(expected_time, expected_time))
            before = (destination / "Core.hlsl").stat().st_mtime_ns
            (build / "fixture_shaders.stamp").unlink()
            result = subprocess.run(commands[1], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((destination / "Core.hlsl").stat().st_mtime_ns, before)
            self.assertTrue((build / "fixture_shaders.manifest").is_file())


if __name__ == "__main__":
    unittest.main()
