"""Exercise production DOF migration and provider/user compatibility routing."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from extract_adaptive_balance_toggle import function

ROOT = Path(__file__).resolve().parents[1]


class DepthOfFieldMigrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CXX") or next(
            (path for name in ("cl", "c++", "clang++", "g++") if (path := shutil.which(name))), None)
        if compiler is None:
            raise unittest.SkipTest("A C++20 compiler is required")
        include = os.environ.get("CSX_JSON_INCLUDE")
        if not include:
            raise RuntimeError("CSX_JSON_INCLUDE must identify the nlohmann JSON include directory")
        migration = (ROOT / "src/SettingsMigrations.cpp").read_text(encoding="utf-8-sig")
        overrides = (ROOT / "src/SettingsOverrideManager.cpp").read_text(encoding="utf-8-sig")
        overrides = overrides.replace('#include "FeatureIssues.h"', '').replace('#include "Util.h"', '')
        report = function(overrides, "void SettingsOverrideManager::ReportOverrideFailure(")
        overrides = overrides.replace(report, "void SettingsOverrideManager::ReportOverrideFailure(const std::string&, const std::string&, const std::string&) {}\n")
        prelude = (ROOT / "tests/depth_of_field_migration_fixture.h").read_text(encoding="utf-8-sig")
        bloom = (ROOT / "src/Features/Bloom.cpp").read_text(encoding="utf-8-sig")
        prelude += function(bloom, "Bloom::Profile Bloom::GetPresetProfile(")
        filesystem = (ROOT / "src/Utils/FileSystem.cpp").read_text(encoding="utf-8-sig")
        prelude += "namespace Util::FileHelpers {\n" + function(filesystem, "JsonFileReadResult ReadJsonFile(") + "}\n"
        driver = (ROOT / "tests/depth_of_field_migration_test.cpp").read_text(encoding="utf-8-sig")
        cls.temporary = tempfile.TemporaryDirectory(prefix="csx-dof-migration-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        source = cls.directory / "migration.cpp"
        cls.executable = cls.directory / "migration.exe"
        source.write_text(prelude + migration + overrides + driver, encoding="utf-8")
        includes = [str(ROOT / "src"), *include.split(";")]
        if Path(compiler).stem.lower() == "cl":
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX",
                       *[f"/I{path}" for path in includes], str(source), f"/Fe:{cls.executable}"]
        else:
            command = [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                       *[f"-I{path}" for path in includes], str(source), "-o", str(cls.executable)]
        result = subprocess.run(command, cwd=cls.directory, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def check_case(self, name):
        result = subprocess.run([str(self.executable), name, str(self.directory / name)],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_saved_flags_values_and_legacy_renderer_fields(self):
        self.check_case("saved-values")

    def test_source_priority_and_canonical_precedence(self):
        self.check_case("precedence")

    def test_malformed_destination_recovery(self):
        self.check_case("malformed")

    def test_legacy_boot_disable_remains_inactive(self):
        self.check_case("disabled-at-boot")

    def test_malformed_and_unknown_dof_leaves_are_retained(self):
        self.check_case("dof-retained-invalid")

    def test_reset_preserves_unsupported_data_without_replay(self):
        self.check_case("reset-preservation")

    def test_invalid_appearance_numbers_are_retained(self):
        self.check_case("appearance-invalid-numeric")

    def test_upstream_appearance_and_lock_inference(self):
        self.check_case("upstream-appearance")

    def test_global_godray_brightness_scope_values_and_precedence(self):
        self.check_case("godray-migration")

    def test_unrepresentable_godray_numbers_are_retained_and_do_not_block_recovery(self):
        self.check_case("godray-invalid-numeric")

    def test_retained_legacy_godray_sources_migrate_durably(self):
        self.check_case("godray-retained-user")

    def test_bloom_active_global_and_inactive_source_preservation(self):
        self.check_case("bloom-preservation")

    def test_cs_and_os_utility_provider_routing(self):
        self.check_case("providers")

    def test_user_values_are_durable_before_legacy_cleanup(self):
        self.check_case("user-migration")

    def test_failed_write_and_concurrent_source_edit_preserve_data(self):
        self.check_case("user-failure")

    def test_destination_provider_retains_legacy_user_choices(self):
        self.check_case("provider-rename")

    def test_disabled_provider_does_not_apply_legacy_user_choices(self):
        self.check_case("disabled-provider")


if __name__ == "__main__":
    unittest.main()
