# Cross-platform build guide

This document describes supported build targets and recommended build commands for
the HubSight Admin SDK for Qt/C++.

## Supported target matrix

| Target        | Architecture          | Native build | Cross-build notes                                                                                       |
| ------------- | --------------------- | -----------: | ------------------------------------------------------------------------------------------------------- |
| Windows 10/11 | amd64 / x86_64        |          Yes | Use the MSVC x64 Qt kit.                                                                                |
| Windows 10/11 | ARM64 / aarch64       |          Yes | Use an ARM64 Qt kit and `-A ARM64`; an x64 host can cross-build with the correct Qt/dependency sysroot. |
| Linux desktop | amd64 / x86_64        |          Yes | Use a Qt kit built for the target distribution/ABI.                                                     |
| Linux desktop | ARM64 / aarch64       |          Yes | Use an ARM64 Qt kit, target compiler, sysroot, and target-architecture dependencies.                    |
| macOS desktop | arm64 / Apple Silicon |          Yes | Use an arm64 Qt kit and `CMAKE_OSX_ARCHITECTURES=arm64`.                                                |

`amd64` means `x86_64`. Windows and macOS commonly call `aarch64`
`ARM64` or `arm64`.

The SDK is desktop-only. Android, iOS, mobile, and tablet targets are not part
of the supported build matrix.

## General requirements

Every target build requires:

- Qt 6.6 or newer with the target architecture's `Core`, `Network`, and
  `WebSockets` components;
- CMake 3.21 or newer;
- a C++20 compiler and linker;
- Ninja or a supported platform generator.

For the native `Visual Studio 18 2026` generator, use CMake 4.2 or newer;
that generator was added after the project's minimum CMake version. Visual
Studio 2026 can also be used with the Ninja generator from a Developer PowerShell
when a standalone CMake 4.2+ installation is not selected.

For a cross-build, **all target-side dependencies must match the target
architecture**. Do not use host Qt libraries, host OpenSSL, or host `libsecret`
when producing an ARM64 binary.

The SDK does not require Qt GUI or QML modules.

## Optional dependencies

### Desktop secure storage

Desktop secure storage is enabled by default when the platform backend is
available:

| Platform | Backend            | Build dependency                                                      |
| -------- | ------------------ | --------------------------------------------------------------------- |
| Windows  | Credential Manager | Windows SDK / `Advapi32`                                              |
| macOS    | Keychain Services  | `Security.framework`, `CoreFoundation.framework`                      |
| Linux    | Secret Service     | `libsecret-1` development files and a runtime Secret Service provider |

If Linux `libsecret-1` is not available, the SDK still builds. The default
application facade fails closed instead of writing refresh tokens to plaintext.

### Native `.hscfg` importer

The Admin `.hscfg` importer requires target-compatible:

- OpenSSL Crypto;
- libargon2;
- libzip;
- libyaml;
- `pkg-config` metadata that CMake can resolve for the target environment.

If these dependencies are unavailable, explicitly disable the importer:

```sh
-DHUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT=OFF
```

The SDK then builds normally, but `.hscfg` import returns
`HscfgImportError::ImporterUnavailable`.

## Common CMake options

A release SDK build normally uses:

```text
-DCMAKE_BUILD_TYPE=Release
-DHUBSIGHT_ADMIN_BUILD_TESTS=OFF
-DHUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT=ON
-DHUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE=ON
```

For a native developer build, enable tests:

```text
-DHUBSIGHT_ADMIN_BUILD_TESTS=ON
```

Multi-config generators such as Visual Studio ignore `CMAKE_BUILD_TYPE`; pass
`--config Release` to the build and test commands instead.

## Windows 10/11 — amd64 with Visual Studio 2026

Open a **Visual Studio 2026 Developer PowerShell** or Developer Command Prompt
with the Windows 10/11 SDK and the **Desktop development with C++** workload
installed. Use a Qt kit built for MSVC x64 and a standalone CMake 4.2+.

The VS2026 generator uses the `v145` toolset by default. It can also be
selected explicitly with `-T v145`.

The Qt path varies by installer/version. The path below is an example and must
point to the actual Qt installation:

```powershell
cmake -S . -B build/win-amd64 -G "Visual Studio 18 2026" -A x64 -T v145 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64" -DHUBSIGHT_ADMIN_BUILD_TESTS=ON -DHUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE=ON
cmake --build build/win-amd64 --config Release --parallel
ctest --test-dir build/win-amd64 -C Release --output-on-failure
```

If the optional importer dependencies are not available through the selected
Windows development environment, build the transport/library first with:

```powershell
cmake -S . -B build/win-amd64 -G "Visual Studio 18 2026" -A x64 -T v145 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64" -DHUBSIGHT_ADMIN_BUILD_TESTS=ON -DHUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT=OFF
```

### Windows 10/11 — ARM64 with Visual Studio 2026

Use a Qt kit compiled for Windows ARM64. The kit directory is commonly named
something similar to `msvc2022_arm64`, but the exact name depends on the Qt
installation. Verify that the selected Qt kit is compatible with the MSVC
v145 toolset; rebuild Qt for v145 if the vendor kit does not support it.

```powershell
cmake -S . -B build/win-arm64 -G "Visual Studio 18 2026" -A ARM64 -T v145 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_arm64" -DHUBSIGHT_ADMIN_BUILD_TESTS=OFF -DHUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE=ON
cmake --build build/win-arm64 --config Release --parallel
```

An x64 development machine can cross-build this target. The Qt ARM64 kit and
every optional native dependency must also be ARM64. Do not use the x64 Qt kit
with `-A ARM64`.

Run the test executable on an ARM64 Windows machine, or configure an appropriate
Windows ARM64 test runner. A host x64 CTest process cannot execute an ARM64 test
binary without an emulator or remote runner.

After building an application that links the SDK, deploy the matching Qt
runtime from the same architecture kit:

```powershell
C:/Qt/6.8.0/msvc2022_arm64/bin/windeployqt.exe --release --no-translations path/to/YourApp.exe
```

Use the x64 `windeployqt.exe` for the x64 build and the ARM64 one for the ARM64
build. Sign the application and its binaries according to the deployment
policy before distributing it.

## Linux desktop — amd64

Install a C++20 toolchain, CMake, Ninja, Qt 6 development packages, and
optionally the target development packages for `libsecret`, OpenSSL, libargon2,
libzip, and libyaml.

For a native x86_64 build using a self-contained Qt installation:

```sh
cmake -S . -B build/linux-amd64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64 \
  -DHUBSIGHT_ADMIN_BUILD_TESTS=ON \
  -DHUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE=ON
cmake --build build/linux-amd64 --parallel
ctest --test-dir build/linux-amd64 --output-on-failure
```

For a distro-provided Qt installation, omit `CMAKE_PREFIX_PATH` when Qt is
already discoverable, or set it to the distro's Qt prefix.

A native test run requires a working desktop Secret Service provider if the
secure-storage round-trip test is expected to exercise libsecret. The test
handles an unavailable runtime vault without falling back to plaintext.

## Linux desktop — ARM64

A native ARM64 Linux build uses the same command shape as the amd64 build, but
all tools and libraries must target ARM64:

```sh
cmake -S . -B build/linux-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_arm64 \
  -DHUBSIGHT_ADMIN_BUILD_TESTS=OFF \
  -DHUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE=ON
cmake --build build/linux-arm64 --parallel
```

The Qt directory name is installation-specific. Verify that its `Qt6Config.cmake`
and libraries are ARM64, not x86_64.

### Linux ARM64 cross-toolchain

For an x86_64 host cross-building an ARM64 target, create a toolchain file with
paths appropriate for the target sysroot:

```cmake
# toolchains/linux-aarch64.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_SYSROOT /opt/sysroots/linux-aarch64)
set(CMAKE_FIND_ROOT_PATH
    /opt/sysroots/linux-aarch64
    /opt/Qt/6.8.0/gcc_arm64
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

Build with tests disabled unless a target runner or emulator is configured:

```sh
cmake -S . -B build/linux-arm64-cross -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchains/linux-aarch64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_arm64 \
  -DHUBSIGHT_ADMIN_BUILD_TESTS=OFF \
  -DHUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE=ON
cmake --build build/linux-arm64-cross --parallel
```

The target sysroot must contain ARM64 versions of optional dependencies if
`.hscfg` import or libsecret support is enabled. In particular, configure
`pkg-config` for the target sysroot instead of allowing it to return host
packages.

If the test executable is built for a target runner, set
`CMAKE_CROSSCOMPILING_EMULATOR` or use a remote CTest configuration. Otherwise,
run CTest on the ARM64 device/VM after installing the target library and Qt
runtime.

## Building Linux targets on macOS

Yes. The recommended approach is to build inside a Linux container or VM rather
than trying to use macOS Clang and macOS Qt libraries directly. Docker Desktop,
OrbStack, Colima, or a Linux VM can be used.

On an Apple Silicon Mac:

- use `--platform linux/arm64` for a Linux ARM64 build;
- use `--platform linux/amd64` for a Linux amd64 build; Docker Desktop runs this
  through emulation and it will be slower;
- use a separate build directory for each target architecture;
- install and use the Linux target's Qt and native dependencies inside the
  container/VM.

The following example uses Ubuntu 24.04 containers. It builds the SDK and runs
CTest inside the target Linux environment:

### Linux ARM64 from macOS arm64

```sh
docker run --rm --platform linux/arm64 \
  -v "$PWD":/src -w /src ubuntu:24.04 \
  bash -lc 'export DEBIAN_FRONTEND=noninteractive && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
      ca-certificates cmake g++ ninja-build pkg-config \
      qt6-base-dev qt6-websockets-dev qt6-base-dev-tools \
      libssl-dev libargon2-dev libzip-dev libyaml-dev libsecret-1-dev && \
    cmake -S . -B build/linux-arm64-macos-host -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DHUBSIGHT_ADMIN_BUILD_TESTS=ON \
      -DHUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT=ON \
      -DHUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE=ON && \
    cmake --build build/linux-arm64-macos-host --parallel && \
    ctest --test-dir build/linux-arm64-macos-host --output-on-failure'
```

### Linux amd64 from macOS arm64

```sh
docker run --rm --platform linux/amd64 \
  -v "$PWD":/src -w /src ubuntu:24.04 \
  bash -lc 'export DEBIAN_FRONTEND=noninteractive && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
      ca-certificates cmake g++ ninja-build pkg-config \
      qt6-base-dev qt6-websockets-dev qt6-base-dev-tools \
      libssl-dev libargon2-dev libzip-dev libyaml-dev libsecret-1-dev && \
    cmake -S . -B build/linux-amd64-macos-host -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DHUBSIGHT_ADMIN_BUILD_TESTS=ON \
      -DHUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT=ON \
      -DHUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE=ON && \
    cmake --build build/linux-amd64-macos-host --parallel && \
    ctest --test-dir build/linux-amd64-macos-host --output-on-failure'
```

The container's architecture determines the produced binary. Verify it after
copying/installing artifacts:

```sh
docker run --rm --platform linux/arm64 \
  -v "$PWD":/src -w /src ubuntu:24.04 \
  file build/linux-arm64-macos-host/libhubsight-admin-sdk.so

# Replace linux/arm64 and the build directory with linux/amd64 when needed.
```

A container usually has no desktop Secret Service daemon. The SDK still builds
with libsecret support, but the secure-storage round-trip test may skip because
no runtime vault provider is available. Run the final secure-storage validation
on a real Linux desktop/VM with GNOME Keyring, KWallet, or another Secret Service
provider.

For faster ARM64 iteration on Apple Silicon, prefer the `linux/arm64` container
or a native ARM64 Linux runner. Use `linux/amd64` only when an x86_64 Linux
artifact is required.

## macOS — Apple Silicon arm64

Install Xcode Command Line Tools and an arm64 Qt 6 kit. Build using Apple Clang
and select the arm64 architecture explicitly:

```sh
cmake -S . -B build/macos-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH=/Users/Shared/Qt/6.8.0/macos \
  -DHUBSIGHT_ADMIN_BUILD_TESTS=ON \
  -DHUBSIGHT_ADMIN_ENABLE_DESKTOP_SECURE_STORAGE=ON
cmake --build build/macos-arm64 --parallel
ctest --test-dir build/macos-arm64 --output-on-failure
```

The macOS secure-storage backend links `Security.framework` and
`CoreFoundation.framework` automatically when they are available. No separate
Keychain library is required.

For a deployment build, use an arm64 application bundle and deploy Qt from the
same arm64 installation:

```sh
cmake --install build/macos-arm64 --prefix dist/macos-arm64
macdeployqt path/to/YourApp.app -always-overwrite
```

Sign and notarize the final application bundle according to the macOS
distribution policy. Do not mix x86_64 Qt frameworks into an arm64 bundle.

## Install and verify the SDK

A release build can be installed to a target-specific prefix:

```sh
cmake --install build/<target> --prefix dist/<target>
```

The exported target is `HubSight::AdminSdk`. Consumers should use the same Qt
major/minor ABI family and a compatible C++ runtime as the SDK build.

Verify the produced architecture before packaging:

```sh
# Linux
file dist/linux-amd64/lib/libhubsight-admin-sdk.so
readelf -h dist/linux-amd64/lib/libhubsight-admin-sdk.so | grep Machine

# macOS
file dist/macos-arm64/lib/libhubsight-admin-sdk.dylib
```

On Windows, use Visual Studio's `dumpbin /headers` or another PE inspection tool
to verify `x64` versus `ARM64`.

## CI matrix

The repository includes [`.github/workflows/build.yml`](../.github/workflows/build.yml)
with one build job per target ABI rather than reusing a host Qt install:

```text
windows-2022-x64       -> MSVC, Qt Windows x64, CTest
windows-2022-arm64     -> MSVC ARM64 cross-build, Qt Windows ARM64
ubuntu-24.04-x64       -> Linux x86_64 Qt/toolchain, CTest
ubuntu-24.04-arm64     -> native Linux aarch64 runner, CTest
macos-14-arm64         -> Apple Clang, arm64 Qt, CTest
```

The Windows ARM64 job is a cross-build because GitHub-hosted Windows ARM64
availability is not assumed. The Linux ARM64 job uses a native ARM64 runner;
if that runner is unavailable for an organization, replace it with a matching
self-hosted runner or disable only that job rather than executing its binary on
x86_64.

For native jobs:

1. configure with tests enabled;
2. build Debug or Release;
3. run CTest;
4. install/package the target artifacts.

For cross jobs without a target runner:

1. configure with tests disabled;
2. build and inspect the target architecture;
3. run the same test binary later on a native target runner;
4. package only target-architecture Qt and native dependencies.

## Troubleshooting

### CMake finds the wrong Qt architecture

Set `CMAKE_PREFIX_PATH` or `Qt6_DIR` explicitly to the target Qt kit. Remove the
build directory after changing architecture or Qt kit:

```sh
rm -rf build/<target>
```

On Windows, use a new build directory when switching between `-A x64` and
`-A ARM64`.

If CMake reports that `Visual Studio 18 2026` is an unknown generator, install
or select standalone CMake 4.2+ and ensure it is first on `PATH`. As a fallback,
open the VS2026 Developer PowerShell and use Ninja:

```powershell
cmake -S . -B build/win-amd64-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64" -DHUBSIGHT_ADMIN_BUILD_TESTS=ON
cmake --build build/win-amd64-ninja --parallel
ctest --test-dir build/win-amd64-ninja --output-on-failure
```

### `.hscfg` importer is unavailable

This means one or more target dependencies or their `pkg-config` metadata were
not found. Either install target-compatible OpenSSL Crypto, libargon2, libzip,
and libyaml, or configure with:

```sh
-DHUBSIGHT_ADMIN_ENABLE_HSCFG_IMPORT=OFF
```

Do not copy host `.so`, `.dylib`, or `.dll` files into a target build to silence
CMake detection.

### Linux secure storage is unavailable at runtime

Compile-time libsecret support requires `libsecret-1`; runtime operation also
requires a Secret Service provider such as GNOME Keyring or KWallet. The SDK
reports a sanitized diagnostic and does not write refresh tokens to plaintext
when the provider is missing or locked.

### Cross-compiled tests do not start

This is expected when the host cannot execute the target binary. Run CTest on a
native target runner or configure an emulator through
`CMAKE_CROSSCOMPILING_EMULATOR`.
