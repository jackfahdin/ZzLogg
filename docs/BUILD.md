# How to Build ZzLogg

## Getting the source

The current repository is
[gitcode.com/JackfahdinQt/ZzLogg](https://gitcode.com/JackfahdinQt/ZzLogg).
Clone it with its pinned submodules:

```bash
git clone --recursive https://gitcode.com/JackfahdinQt/ZzLogg
cd ZzLogg
```

If the repository was cloned without `--recursive`, initialize the submodules
before configuration:

```bash
git submodule update --init --recursive
```

ZzPureTools is a pinned, required build dependency and the repository's only
remaining Git submodule. backward-cpp v1.6 is tracked directly under
`3rdparty/vendor/backward-cpp`, so that dependency is available offline after
the ZzLogg source tree itself has been cloned or archived. This does not make
Qt, Boost, OpenSSL, or every CPM/CI dependency offline.

## Requirements

ZzLogg requires:

- CMake 3.23 or later for direct command-line builds; the checked-in presets
  and workflows require CMake 3.25 or later;
- a C++20 compiler: GCC 13.1 or newer, Clang 17 or newer, Apple Clang 15 or newer, or MSVC 19.38 or newer (Visual Studio 2022 17.8+); Apple builds
  require a macOS deployment target 13.3 or newer;
- Qt 6.8 or later, including Core, Core5Compat, Gui, Widgets, Svg, Concurrent,
  Network, Xml, LinguistTools, and the matching private development files;
- the pinned ZzPureTools submodule and the other vendored dependencies in this
  repository.

Focused UI tests additionally require Qt Test. macOS builds currently set a
13.3 deployment target.

Hyperscan search additionally requires SSSE3, Boost headers, and Ragel. Pass
`-DKLOGG_USE_HYPERSCAN=OFF` when those dependencies are unavailable; ZzLogg
then uses the Qt regular-expression backend. Other third-party dependencies
are provided by the repository or resolved during CMake configuration.

## Preset builds

List the available configure, build, test, and workflow presets:

```bash
cmake --list-presets=all
```

The shared Ninja workflows configure, build, and test in one command:

```bash
cmake --workflow --preset ninja-debug
cmake --workflow --preset ninja-relwithdebinfo
cmake --workflow --preset ninja-release
```

The same stages can be run separately:

```bash
cmake --preset ninja-release -DKLOGG_USE_HYPERSCAN=OFF
cmake --build --preset ninja-release
ctest --preset ninja-release
```

The focused UI test workflow uses the same ZzLogg GUI target:

```bash
cmake --workflow --preset ninja-ui-debug
```

On Windows, copy `CMakeUserPresets.json.example` to `CMakeUserPresets.json`
and set the local Qt and Visual Studio paths. The repository includes Visual
Studio 2026 presets; for example:

```powershell
cmake --preset windows-vs2026-ui -DKLOGG_USE_HYPERSCAN=OFF
cmake --build --preset windows-vs2026-ui-relwithdebinfo
ctest --preset windows-vs2026-ui-relwithdebinfo
```

`CMakeUserPresets.json` is intentionally ignored so machine-specific paths do
not enter version control.

## Direct command-line build

When presets are not suitable, configure a conventional build directory:

```bash
cmake -S . -B out/build/ZzLogg -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DKLOGG_USE_HYPERSCAN=OFF
cmake --build out/build/ZzLogg
ctest --test-dir out/build/ZzLogg --output-on-failure
```

The only GUI executable is `ZzLogg` (`ZzLogg.exe` on Windows). The experimental
`klogg_grep` target is excluded from the default build; build it explicitly
only when working on that command-line frontend:

```bash
cmake --build out/build/ZzLogg --target klogg_grep
```

Default builds do not create alternate GUI or self-contained executable
targets.

## Storage location and migration

On first launch, ZzLogg asks where its persistent configuration, saved session,
logs, and crash data should live. Canceling the chooser exits without creating
configuration. The choices are:

- **User data directory**: the platform's per-user application data location;
- **Program directory**: an adjacent `data/` directory, providing green use
  with application and data kept together;
- **Custom directory**: an absolute directory selected by the user.

The selected root contains `config/ZzLogg.ini`,
`session/ZzLogg_session.ini`, `logs/`, `crashes/`, and
`storage-manifest.ini`. `--data-dir <absolute-path>` is a process-only override
and does not replace the saved storage locator.

The **Storage** page in Preferences can migrate an existing root to a new,
empty location. ZzLogg saves current settings, performs the migration
transactionally, and asks whether to restart immediately or later. The new
location becomes active after restart; the saved session and recent files are
preserved. If the selected root later becomes unavailable, startup reports the
storage error instead of silently creating a new default profile.

## Install and CPack

Install from a configured Ninja build into a staging directory:

```bash
cmake --install out/build/ninja-release --prefix staging/ZzLogg
```

On Linux, the install rules place `ZzLogg.desktop`, PNG icons at 16, 32, 48,
64, 128, 256, and 512 pixels, and the scalable `ZzLogg.svg` icon in their
standard locations. CPack configuration is generated by supported Unix
configurations and uses the same central product metadata.

The Windows NSIS input is `packaging/windows/ZzLogg.nsi` and packages Qt 6.
Building an installer requires NSIS and a prepared release directory containing
the application and runtime files referenced by the script. The macOS bundle,
distribution metadata, and DMG layout are maintained in the source tree, but
must be built and checked on a macOS host.

## Windows runtime folder

After configuring a Windows build with `windeployqt` available, create the
self-contained runtime directory with:

```powershell
cmake --build --preset windows-vs2026-ui-relwithdebinfo --target zzlogg_runtime_folder
```

The generated directory is
`<build-directory>/runtime/RelWithDebInfo/ZzLogg-runtime/` and contains the
single `ZzLogg.exe` GUI plus its Qt, ZzPureTools, MSVC runtime, and TBB
dependencies. Windows packaging copies this same tree into installer and
self-contained archive staging. Both forms run the same `ZzLogg.exe`.

## Build options and targets

- `KLOGG_BUILD_UI_TESTS=ON` enables the focused UI tests;
- `KLOGG_USE_HYPERSCAN=OFF` selects the Qt regular-expression backend;
- `KLOGG_USE_SENTRY=ON` enables crash-reporting support;
- `zzlogg_runtime_folder` creates the Windows self-contained runtime tree.

These options do not create a second GUI. The public executable, package, and
desktop entry remain ZzLogg.

## Verification boundaries

### Windows Release UI acceptance

From a configured developer environment (or with Qt supplied explicitly):

```powershell
cmake --preset windows-vs2026-ui -DCMAKE_PREFIX_PATH=D:/SoftWare/Qt/6.11.0/msvc2022_64
cmake --build --preset windows-vs2026-ui-release --parallel 8
ctest --preset windows-vs2026-ui-release
cmake --build --preset windows-vs2026-ui-release --target zzlogg_runtime_folder
```

The corresponding `windows-vs2026-ui-release` workflow runs configure, build,
and test. Supply your Qt path through the environment or a local user preset
when using workflows. The Release test preset runs all registered tests,
including `klogg_smoke`. Its runtime folder is
`out/ui-vs/runtime/Release/ZzLogg-runtime/`; run `ZzLogg.exe` from that folder,
not the build output directory without dependencies. No ZIP is required.

### Compatibility names

Application UI implementation is in `src/ui`, not `src/ui2`. The `tests/ui2`
directory, `zzlogg_ui2.*` CTest names, old `*-ui2-*` preset aliases and smoke
protocol names remain compatibility interfaces for existing scripts and CI.
They do not enable a second GUI or a second UI framework. Use the `*-ui-*`
presets for new commands; do not rename the legacy interfaces in isolation.

### Platform limits

The CTest presets exercise tests available on the current host. Windows
interactive DPI, theme, high-contrast, and multi-monitor checks, together with
Linux and macOS real-host packaging checks, must be run on their corresponding
hosts before making release claims.
