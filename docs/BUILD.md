# How to Build Klogg

## Overview

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.
Local builds can be faster because code can be optimized for current CPU instead of generic x86-64. Support for SSE4/AVX code paths
will be enabled if available on build machine.

## Getting the Source

This project is [hosted on GitHub](https://github.com/variar/klogg). You can clone this project directly using this command:

```
git clone https://github.com/variar/klogg
cd klogg
git submodule update --init --recursive
```

The UI2 build uses the `3rdparty/vendor/ZzPureTools` submodule. Initialize all
submodules before configuring a fresh clone, and do not replace repository-
pinned submodule revisions with arbitrary checkouts.

## Dependencies

To build Klogg:

- CMake 3.12 or later for traditional command-line builds
- CMake 3.25 or later when using the provided presets and workflows
- C++ compiler with decent C++17 support (at least gcc 7.5, clang 7, msvc 19.14)
- Qt 6 libraries:
  - QtCore
  - QtGui
  - QtWidgets
  - QtConcurrent
  - QtNetwork
  - QtXml
  - QtTools

The optional `zzlogg_ui2` target has newer requirements: CMake 3.23 or later,
Qt 6.8 or later, a C++20 compiler, and the matching Qt private development
files. Its Qt modules are Core, Gui, Widgets, Svg, Concurrent, and Test.
ZzPureTools requires a macOS deployment target of 13.3 for UI2 builds. The
traditional targets are unaffected by that value when UI2 is disabled.

To build Hyperscan regular expressions backend (default):

- CPU with support for [SSSE3](https://en.wikipedia.org/wiki/SSSE3) instructions (for Hyperscan backend)
- Boost (1.58 or later, header-only part)
- Ragel (6.8 or later; precompiled binary is provided for Windows; has to be installed from package managers on Linux or Homebrew on Mac)

To build installer for Windows:

- nsis to build installer for Windows
- Precompiled OpenSSl library to enable https support on Windows

Building tests:

- QtTest

All other dependencies are provided by [CPM](https://github.com/cpm-cmake/CPM.cmake) during cmake configuration stage (see 3rdparty directory).

CPM will try to find Hyperscan, TBB, uchardet and xxhash installed on build host.
If a library can't be found, the one provided by CPM will be used.

## Building

### Building with CMake Presets

The repository provides cross-platform presets in `CMakePresets.json`. Build
outputs for the traditional presets are kept below `out/build/<preset-name>`.
To avoid Windows path-length failures in generated forwarding headers, UI2
uses the shorter `out/ui2-ninja`, `out/ui2-vs`, and `out/ui2-win` directories.

List all available presets:

```bash
cmake --list-presets=all
```

On Linux and macOS, install Qt and Ninja with the platform package manager (or
set `CMAKE_PREFIX_PATH` to the Qt installation), then use one of the shared
Ninja workflows:

```bash
cmake --workflow --preset ninja-debug
cmake --workflow --preset ninja-relwithdebinfo
cmake --workflow --preset ninja-release
```

Build and test the optional UI2 application with the shared cross-platform
workflow:

```bash
cmake --workflow --preset ninja-ui2-debug
```

Each workflow configures, builds, and verifies the generated application
artifact. The individual stages can also be run separately:

```bash
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

Machine-specific Qt and Visual Studio paths belong in
`CMakeUserPresets.json`, which is intentionally ignored by Git. Copy
`CMakeUserPresets.json.example` to get started. On Windows, the example
provides a Qt 6 preset based on the Visual Studio 2026 generator:

```powershell
cmake --preset windows-qt6
cmake --build --preset windows-qt6-debug
ctest --preset windows-qt6-debug
```

After adding the machine-specific Qt and Visual Studio paths, build and test
UI2 on Windows with:

```powershell
cmake --workflow --preset windows-qt6-ui2-debug
```

The workflows automate configuration, compilation, and focused tests. Windows
interactive DPI, theme, high-contrast, and multi-monitor acceptance, together
with Linux and macOS real-host builds, remain pending release/final-delivery
checks. The release owner approved deferring them for this implementation
phase; they must not be reported as passed until they are performed.

Create a self-contained Windows portable folder:

```powershell
cmake --workflow --preset windows-qt6-portable
```

The generated artifacts are placed in:

```text
out/ui2-vs/portable/RelWithDebInfo/ZzLogg-portable/
```

The folder contains `ZzLogg_portable.exe`, the required Qt plugins and runtime
libraries, the MSVC runtime, TBB libraries, and the project documentation and
license files.

The first UI2 phase does not provide a UI2 portable folder, zip archive, or
installer. The external green output directory is `ZzLogg-portable`; the
internal compatibility target remains `klogg_portable_folder`.

The Visual Studio generator initializes the MSVC build environment itself, so
these presets do not require running `VsDevCmd.bat` first. Qt and Visual Studio
paths in the user preset should use forward slashes, including on Windows. The
example disables Hyperscan so that Qt, MSVC, and CMake are sufficient for a
first build. Install Boost and set `KLOGG_USE_HYPERSCAN` to `true` if the
accelerated regular-expression backend is required.

Qt 6 is required.

### Configuration options

`KLOGG_BUILD_UI2` defaults to `OFF`. With UI2 disabled, the existing Qt 6
targets remain on their C++17 build path. Enabling UI2 adds the C++20
`zzlogg_ui2` target and, when `KLOGG_BUILD_UI2_TESTS` is enabled, its focused
test targets.

By default Klogg is built without support for reporting crash dumps. This can be enabled via cmake option `-DKLOGG_USE_SENTRY=ON`.

Klogg uses Hyperscan regular expressions library which requires CPU with SSSE3 support, ragel and boost headers.
Klogg can be built with only Qt reqular expressions backend by passing `-DKLOGG_USE_HYPERSCAN=OFF` to cmake.

Klogg can use custom memory allocator. By default it uses TBB memory allocator for Windows, mimalloc on Linux and default system allocator on MacOS.
Memory allocator override can be turned off by passing `-DKLOGG_OVERRIDE_MALLOC`. If you want to use TBB allocator on Linux then pass
`-DKLOGG_USE_MIMALLOC=OFF`.

### Building on Linux

Here is how to build klogg on Ubuntu 24.04.

Install dependencies:

```
sudo apt-get install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev libboost-all-dev ragel
```

Configure and build klogg:

```
cd <path_to_klogg_repository_clone>
mkdir build_root
cd build_root
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
cmake --build .
```

**_If CMake reports missing Qt6 LinguistTools, install the Qt 6 tools package:_**

```bash
sudo apt-get install qt6-tools-dev
```

Binaries are placed into `build_root/output`.

See `.github/workflows/ci-build.yml` for more information on build process.

### Building on Windows

Install Microsoft Visual Studio 2017 or 2019 with C++ support.
Community edition can be downloaded from [Microsoft](https://visualstudio.microsoft.com/vs/).

Intall latest Qt version using [online installer](https://www.qt.io/download-qt-installer).
Make sure to select version matching Visual Studio installation. 64-bit libraries are recommended.

Install CMake from [Kitware](https://cmake.org/download/).
Use version 3.14 or later for Visual Studio 2019 support.

Download the Boost source code from http://www.boost.org/users/download/.
Extract to some folder. Directory structure should be something like `C:\Boost\boost_1_63_0`.
Then add `BOOST_ROOT` environment variable pointing to main directory of Boost sources so CMake is able to fine it.

Prepare build environment for CMake. Open command prompt window and depending on version of Visual Studio run either

```
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\Common7\Tools\vsdevcmd" -arch=x64
```

or

```
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\Common7\Tools\vsdevcmd" -arch=x64
```

Next setup Qt paths:

```
<path_to_qt_installation>\bin\qtenv2.bat
```

Then add CMake to PATH:

```
set PATH=<path_to_cmake_bin>:$PATH
```

Configure klogg solution (use CMake generator matching Visual Studio version):

```
cd <path_to_project_root>
md build_root
cd build_root
cmake -G "Visual Studio 16 2019 Win64" -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
```

CMake should generate `klogg.sln` file in `<path_to_project_root>\build_root` directory. Open solution and build it.

Binaries are placed into `build_root/output`.

For https network urls support download precompiled openssl library https://mirror.firedaemon.com/OpenSSL/openssl-1.1.1l-dev.zip.
Put libcrypto-1_1 and libssl-1_1 for desired architecture near klogg binaries.

### Building on Mac OS

Klogg requires macOS High Sierra (10.13) or higher.

Install [Homebrew](https://brew.sh/) using terminal:

```
/usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Homebrew installer should also install xcode command line tools.

Download and install build dependencies:

```
brew install cmake ninja qt boost ragel
```

Usually the Qt installation is discoverable through Homebrew. If it is not,
set `CMAKE_PREFIX_PATH` to the Homebrew Qt 6 prefix.

Configure and build klogg:

```
cd <path_to_klogg_repository_clone>
mkdir build_root
cd build_root
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=<path_to_qt6> ..
cmake --build .
```

Binaries are placed into `build_root/output`.

By default, klogg will rely on cmake to figure out target MacOS version. Usually it uses build host version.
To override default cmake value pass an option `-DKLOGG_OSX_DEPLOYMENT_TARGET=<target>` to cmake during configuration step,
`<target>` is one of `10.14`, `10.15`, `11`, `12`. Klogg's traget must be greater or equal to target used by Qt libraries.

## Running tests

Tests are built by default. To turn them off pass
`-DKLOGG_BUILD_TESTS:BOOL=OFF` to CMake.

The shared presets disable the optional unit/UI test targets because the
`backward-cpp` test dependency is not included in the source snapshot. The
always-available `klogg_smoke` artifact check is still run by the workflow
presets. Set
`KLOGG_BUILD_TESTS` to `ON` in a user preset after providing that dependency
to enable the complete test suite.
Tests use Catch2 (bundled with klogg sources) and require the Qt 6 Test module.
Tests can be run using the CTest tool provided by CMake:

```
cd <path_to_klogg_repository_clone>
cd build_root
ctest --build-config RelWithDebInfo --verbose
```
