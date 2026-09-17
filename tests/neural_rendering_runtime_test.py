"""Exercise optional NR staging without substituting the normal DLSS provider."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RuntimeStaging(unittest.TestCase):
    def test_provider_lifecycle_and_failed_transaction(self):
        scratch = ROOT / "build" / "neural-runtime-fixtures"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            fixture = Path(temporary).resolve()
            self.assertTrue(fixture.is_relative_to(scratch.resolve()))
            source, build = fixture / "source", fixture / "output"
            source.mkdir()
            provider = fixture / "provider.dll"
            provider.write_bytes(b"fixture NR provider")
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 4.2)\n"
                "project(NeuralRuntimeFixture LANGUAGES NONE)\n"
                "add_custom_target(NeuralRuntimeFixture)\n"
                'if(NOT DEFINED STREAMLINE_RUNTIME_DIRECTORY)\n'
                'set(STREAMLINE_RUNTIME_DIRECTORY "${CMAKE_BINARY_DIR}/runtime")\n'
                'endif()\n'
                'file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/runtime")\n'
                'foreach(name nvngx_dlss.dll sl.interposer.dll)\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/runtime/${name}" "normal ${name}")\n'
                'list(APPEND STREAMLINE_RUNTIME_FILES "${CMAKE_BINARY_DIR}/runtime/${name}")\n'
                'endforeach()\n'
                f'include("{ROOT.as_posix()}/cmake/NeuralRenderingRuntime.cmake")\n',
                encoding="utf-8",
            )

            def cmake(*arguments):
                forwarded = []
                for argument in map(str, arguments):
                    if argument.startswith("-D"):
                        forwarded.extend(("-D", argument[2:]))
                    else:
                        forwarded.append(argument)
                return subprocess.run(
                    ["pwsh", "-File", str(ROOT / "tools/cmake.ps1"), *forwarded],
                    cwd=ROOT, capture_output=True, text=True, timeout=90,
                )

            configured = cmake("-S", source, "-B", build,
                               f"-DCSX_LOCAL_DLSSNR_RUNTIME_FILE={provider.as_posix()}")
            self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
            verify = build / "neural-runtime/VerifyNeuralRuntime.cmake"
            checked = cmake("-P", verify)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
            staged = build / "runtime"
            self.assertEqual({p.name for p in staged.glob("*.dll")},
                             {"nvngx_dlss.dll", "sl.interposer.dll", "nvngx_dlssnr.dll"})
            self.assertEqual((staged / "nvngx_dlss.dll").read_bytes(), b"normal nvngx_dlss.dll")
            self.assertEqual((staged / "nvngx_dlssnr.dll").read_bytes(), provider.read_bytes())

            provider.write_bytes(b"changed after configuration")
            rejected = cmake("-P", verify)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("Rejected", rejected.stdout + rejected.stderr)
            self.assertEqual((staged / "nvngx_dlssnr.dll").read_bytes(), b"fixture NR provider")
            self.assertEqual((staged / "nvngx_dlss.dll").read_bytes(), b"normal nvngx_dlss.dll")

            cleared = cmake("-S", source, "-B", build, "-DCSX_LOCAL_DLSSNR_RUNTIME_FILE=")
            self.assertEqual(cleared.returncode, 0, cleared.stdout + cleared.stderr)
            self.assertFalse((staged / "nvngx_dlssnr.dll").exists())
            self.assertTrue((staged / "nvngx_dlss.dll").exists())

            missing = cmake("-S", source, "-B", build,
                            f"-DCSX_LOCAL_DLSSNR_RUNTIME_FILE={(fixture / 'missing.dll').as_posix()}")
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("must identify a runtime file", missing.stdout + missing.stderr)

            outside = fixture / "outside"
            outside.mkdir()
            sentinel = outside / "nvngx_dlssnr.dll"
            sentinel.write_bytes(b"must be preserved")
            rejected = cmake("-S", source, "-B", build,
                             "-DCSX_LOCAL_DLSSNR_RUNTIME_FILE=",
                             f"-DSTREAMLINE_RUNTIME_DIRECTORY={outside.as_posix()}")
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("inside the isolated build", rejected.stdout + rejected.stderr)
            self.assertEqual(sentinel.read_bytes(), b"must be preserved")


if __name__ == "__main__":
    unittest.main()
