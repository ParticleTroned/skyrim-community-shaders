# VR reflection setting persistence

Dynamic Cubemaps stores its two editable native VR water settings in
`Data/SKSE/Plugins/CommunityShaders/SkyrimOverwrite.ini`:

```ini
[Water]
bAutoWaterSilhouetteReflections=1
bForceHighDetailReflections=1
```

The file is optional. A missing file or key retains the current value
without warning. At startup, Dynamic Cubemaps first enables its VR
reflection defaults, then applies saved overrides. A saved `0` therefore
survives startup. Subsequent settings reloads apply the overrides again.
SE and AE do not enter this VR persistence path.

Saving creates the directory and file when necessary. The existing
SimpleIni parser preserves unrelated entries and supported comments; the
existing `FileHelpers::WriteTextFileAtomic` writer provides atomic
replacement with its direct-write fallback for virtualized filesystems.
This avoids calling native per-setting persistence methods outside the
engine's collection-handle lifecycle. Collection filenames and handles
are never changed.

An unreadable existing file prevents saving, rather than replacing it
with only the two water settings. Reads must return the complete measured
file size. UTF-8, ANSI and Windows UTF-16LE files are supported; existing
UTF-16LE encoding is preserved using CommonLib's conversion helpers.
Invalid UTF-16 and embedded NULs are rejected to avoid truncated rewrites.
Invalid setting values produce warnings and retain their current
in-memory values. Offset-only settings are not persisted.

Quoted managed values accept both single and double quotes, matching
[Windows profile reads](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprivateprofilestring).
Duplicate managed keys load their first value and save as one updated
entry. Unrelated duplicate entries and quoted values are preserved.

Save failures propagate through `DynamicCubemaps::OnSettingsSaved` into
the existing `State::Save` post-save failure handling. If
`SettingsUser.json` was written but the INI was not, the save result is
incomplete and the menu shows the error without clearing its existing
unsaved state. The log includes the failed INI path and reason.

No manual INI changes are needed for a missing file or missing keys.
Real filesystem failures still require making the effective INI target
writable. All added work runs at initialization or settings load/save;
there is no added frame-loop work.

## Validation

The `GameSettingPersistence` controller test compiles the production
persistence functions, file writer, and Dynamic Cubemaps lifecycle
methods with minimal engine stubs. It exercises real Windows file I/O.

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target game_setting_persistence_test
ctest --test-dir build/ALL -C Release -R '^GameSettingPersistence$' --output-on-failure
```

On 2026-10-01, the regression executable passed missing-file/key,
directory creation, boolean and scalar round-trip, startup/reload order,
unrelated-entry preservation, invalid value, read-only destination,
locked source, missing engine setting/collection, offset exclusion,
non-finite value, unsupported encoding, and non-VR lifecycle checks.
The focused executable also passed with `/O2 /fp:fast /W4 /WX`.

The changed `GameSetting.cpp` and `DynamicCubemaps.cpp` translation units
compiled with the universal SE/AE/VR MSVC configuration and `/WX` before
the compatibility review below.
No linked DLL deployment or in-game SE/AE/VR validation was performed.

Adversarial review found that the first implementation rejected quoted
values and UTF-16 files, and dropped unrelated duplicate keys. A native
Windows INI comparison reproduced the quote/duplicate failure. The
corrected double-quote, duplicate-key and UTF-16 cases passed through
`GameSettingPersistence` before the user prohibited further builds and
tests. Static review then extended quote handling to single quotes while
preserving unrelated quoted values, and added malformed UTF-16 cases.
Those final additions have not been built or run, per that instruction.

Scoped C++/Markdown/whitespace hooks and standalone gersemi checks on
the new test CMake files were run. The single added include in the root
`CMakeLists.txt` was kept unformatted because whole-file gersemi would
rewrite unrelated existing code; its registration was checked by CMake
configuration and the controller-test build. Initial CMake configuration
hit a blocked dependency fetch; configuration succeeded using the
existing dependencies with `FETCHCONTENT_UPDATES_DISCONNECTED=ON`.
