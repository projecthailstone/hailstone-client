# Continuous integration and local builds

GitHub Actions runs checks only (`.github/workflows/ci.yml`), on every push to `main`, `cloud/**` or `claude/**`
and on pull requests to `main`:

- `core-names`: `tools/check_core_names.py`, the client's sources name no server outside comments (Lua long
  strings count as code). The old names pass only in their own spellings (`nmsc`, `nmsc_`, `nmsc::`, `NMSC_`, the
  SDK's `Nmsc<Word>`, `nms-client`), so `NMSChat` or `kNmsCombatOp` fails; tests/server_tools_test.cpp checks both. Its one
  exception is the updater's table of files old releases shipped under `plugins/nms/` (path, size, SHA-256;
  `retired_release_files()` in `src/app/update_install.cpp`), let through by file and exact line shape.
- `wiki`: `tools/sync_wiki.py --check`, every document of `docs/`, `CONTRIBUTING.md` and `server/README.md` has a
  wiki page and no link points at a missing file (docs/wiki.md).

One workflow builds: `.github/workflows/macos.yml` ("macOS arm64" below), the one platform the developer's Linux
machine cannot build. It runs only when started by hand or on a push to a `macos/**` branch.

`.github/workflows/release.yml` builds and publishes a release: on a pushed tag `v<version>` or by hand, it builds
Windows x64 (MSVC), Linux x64, Linux arm64 and macOS arm64, runs each platform's `hailstone_tests`, makes every
platform's zip and `hailstone-update.txt` with `tools/make_update.py`, signs the manifest with the repository secret
`HAILSTONE_UPDATE_KEY` (the secret key file's 128 hex digits), checks it against `update-key.pub` and uploads it all to
the GitHub release. Without the secret it stops before publishing.

Builds and tests run on the developer's machine, to save Actions minutes. The build legs (linux-gcc and
windows-msvc, and before them Linux Clang, macOS arm64, Linux arm64, a sanitizer leg and clang-tidy) were dropped
on 2026-11-02; they are in the workflow's history. Before a merge to `main`:

| Platform | How | When |
|---|---|---|
| Linux | `cmake --build build && build/hailstone_tests` (AGENTS.md "Build and test"), with `EQ_GAME_DIR` set when the change touches assets | every merge |
| macOS arm64 | the macOS workflow (below): Apple Clang, `hailstone_tests` on GitHub's Apple Silicon runner | a change to `src/platform/`, the build files, or anything Apple Clang may treat differently; and every release |
| Windows | MSVC, CI's old compiler, under Wine: `tools/docker/msvc_build.sh`, then `wine build-msvc/hailstone_tests.exe` ("MSVC under Wine" below) | a change to `src/platform/`, the build files, the Lua bridge (`app/lua_eq*`) or anything a compiler may treat differently; and every release |

Releases are built the same way: Linux in the `hailstone-linux-build` container (Ubuntu 24.04, so the client runs
on 24.04 and newer), Windows with MSVC under Wine (docs/updater.md "Making a release"). Run the sanitizer build
(`-DHAILSTONE_SANITIZE=ON`, docs/hardening.md) and clang-tidy (below) by hand too, and keep the code building with Clang
and Apple Clang: the portability rules in AGENTS.md still hold.

## The tests

- No window, Vulkan device or audio device is needed: Vulkan is reached through volk, and the unit tests only use the headless parts. The window, GPU and audio-device code (`platform/window.cpp`, `render/`, `platform/audio_device.cpp`) is compiled and linked, but not run by the tests.
- `tests/mock_*_test.cpp` run `LoginClient`, `GameConnection` and `Session` against an in-process mock EQEmu on loopback (`tests/mock_server/README.md`): about 4 s, no game files, no network. They use real time on `127.0.0.1` only, on ephemeral ports. A test that asserts a timeout or a hold runs its sessions on `tests/manual_clock.h` (`SessionOptions::clock`) and advances the time itself, so a stalled test thread cannot fire or skip a timer.
- Several `hailstone_tests` run at once on a shared machine (several checkouts, or parallel ctest). A test that writes files does it in `tests/scratch_dir.h`'s `ScratchDir` (or `unique_temp_path`), never a fixed name under the temp folder: with fixed names, eight parallel runs failed 36 of 48 times (one run's `remove_all` emptying another's window-places, Lua and texture folders mid-test).
- With `EQ_GAME_DIR` unset or empty, the tests that need an installed RoF2 game (`tests/game_files.h`, `hailstone::test::game_dir()` returns null) return early, so they pass without checking anything. Run them with it set.
- `HAILSTONE_TEST_TRACE` set makes each test case's name go to stderr as it starts (`tests/test_main.cpp`). A crash that ends the process at once loses doctest's buffered report on stdout, but the last `[test]` line names the case it happened in.
- The browser test against the real CEF helper runs when the build has it (`HAILSTONE_BROWSER`); on Linux without a display it uses Chromium's headless platform.
- The server plugins are kept in repositories of their own (projecthailstone/hailstone-nms and the private projecthailstone/hailstone-projecttriune) and reach players as add-ons (docs/addons.md), released by each repository's `tools/release.sh`. Cloned into `plugins/nms` or `plugins/projecttriune`, each is built with the client and its tests join `hailstone_tests`. The Actions secrets `NMS_DEPLOY_KEY` and `TRIUNE_DEPLOY_KEY`, which let the build legs check them out, are no longer used.

## Problems the first runs showed

| Leg | Failure | Fix |
|---|---|---|
| Apple Clang (Xcode 15; GCC and recent Clang accept it) | `game_connection_market.cpp`: a lambda captured a structured binding (`serial`), which C++20 does not allow | capture a named reference |
| Apple Clang | `std::from_chars` for `double` is missing from Xcode 15's libc++ (`game/spell_record.cpp`, `assets/eqg.cpp`) | `core/float_chars.h`: `from_chars` where `__cpp_lib_to_chars` says it exists, else `strtod` with the same accepted syntax |
| MSVC | `__builtin_memset` and `__builtin_bit_cast` in `tests/cast_spell_vectors_test.cpp` | `std::memset`, `std::bit_cast` |
| MSVC | `hailstone_tests.exe` exits with 0xc0000135: `SDL3.dll` is not next to it (Windows has no rpath) | post-build copy of `$<TARGET_RUNTIME_DLLS:...>` for `hailstone_tests` and `hailstone` |
| MSVC under Wine (19.44, /O2) | Recorded vectors Social_Stop and GetFocusCastingTimeModifier: a 32-bit product that wraps, divided by a constant (`static_cast<std::int32_t>(a * b) / k`), folded as if it had not wrapped | `core/wrap_math.h` `wrapped_div` at every such division (game mechanics, option commands, the reliable channel), and `wrapped_mul_div` where a helper (`mul`, `wrap_mul`) made the product: once inlined, `mul(a, b) / k` is the same pattern |
| MSVC under Wine | Recorded vectors CSpellBookWnd::GetSpellMemTicksLeft: `calc_dex() + calc_int()` called INT first | the calls as separate statements, in the original's order; the other query calls an operator or argument list ordered too |
| MSVC under Wine | "http_get: cancelled ends a transfer stalled waiting for the answer" takes about 21 s: Wine's WinHTTP does not end a synchronous call when another thread closes the request handle, as Windows does | not a client fault: under Wine the test's time is a doctest WARN, not a failure; the cancel still reports "cancelled". Only a run on Windows checks that the close ends the call at once |
| Linux, Windows | CI set-up only (SDL3 dev packages; the glslang asset name and executable name) | see the workflow |

Behaviour is unchanged by all of these.

## Not covered

- Nothing runs on GitHub's machines but the checks: a change merged without the builds above is not built anywhere.
- Linux arm64 and Windows arm64 are not built. macOS arm64 is built only when its workflow is started.
- No macOS build has been run on a Mac with a display: the runner has no usable GPU, so the window, MoltenVK and
  the rendering are compiled and linked only.
- Nothing runs against a real game install or a server unless run by hand; Vulkan and audio device code is only compiled.

## Windows builds from Linux

A Windows x64 build can be made without Windows: `cmake/toolchains/clang-cl-xwin.cmake` compiles with LLVM's
clang-cl and links with lld-link against the MSVC C runtime and the Windows SDK, which
[xwin](https://github.com/Jake-Shadle/xwin) downloads from Microsoft. Running xwin means accepting Microsoft's
license for those files.

```sh
xwin --accept-license --arch x86_64 splat --output ~/.xwin      # once, about 1 GB; HAILSTONE_XWIN_DIR names another folder
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang-cl-xwin.cmake
cmake --build build-windows
cmake --install build-windows --prefix dist/hailstone            # the client's folder, as a release has it
```

It needs clang-cl, lld-link, llvm-lib, llvm-rc and llvm-mt (an LLVM install has them) and the build machine's
glslang for the shaders. RelWithDebInfo writes `hailstone.pdb`: kept beside `hailstone.exe`, it lets a crash report
name the function. The tests run under Wine (`wine build-windows/hailstone_tests.exe`, with a `WINEPREFIX` of its own).

The compiler is `clang-cl-xwin.sh`, which puts the target and the SDK's headers on every compile, since CEF's CMake
files (the browser helper) set `CMAKE_CXX_FLAGS` themselves. It also turns off three warnings that CEF's `/WX`
would make errors and that MSVC does not give (`/MP` unused, missing field initializers, an undefined variable
template); `xwin-include/` forwards a header CEF names in another case than the SDK. This is not MSVC: the
release and the Windows check use MSVC under Wine (below), and clang-cl warns about some code MSVC accepts.

### MSVC under Wine

CI's own compiler (MSVC 19.44 from Visual Studio 2022, as on the `windows-2022` runner) also runs on Linux, under
Wine, with [msvc-wine](https://github.com/mstorsjo/msvc-wine). A crash or a warning that only MSVC's build shows can
then be found without spending CI minutes: the Windows leg's `0xc0000409` in "eq API: bad arguments are Lua errors"
was found this way (`/EHs`, CMakeLists.txt). The download means accepting Microsoft's license. Everything runs in
containers, so msvc-wine's scripts never run on the host:

```sh
docker build -t hailstone-linux-build:24.04 -f tools/docker/linux.Dockerfile tools/docker   # also the Linux release image
docker build -t hailstone-msvc-build:24.04 -f tools/docker/msvc.Dockerfile tools/docker
git clone https://github.com/mstorsjo/msvc-wine ~/opt/msvc-wine-src
mkdir -p ~/opt/msvc ~/opt/msvc-home
# once, about 3.3 GB: MSVC 2022 and the Windows SDK, then msvc-wine's wrappers (bin/x64/cl ...)
docker run --rm -v ~/opt:/opt/host hailstone-linux-build:24.04 bash -c "apt-get update -qq && apt-get install -y -qq msitools \
    && cd /opt/host/msvc-wine-src && python3 vsdownload.py --major 17 --accept-license --architecture x64 \
       --with-asan no --with-atl no --dest /opt/host/msvc && chown -R $(id -u):$(id -g) /opt/host/msvc"
docker run --rm --user $(id -u):$(id -g) -e HOME=$HOME/opt/msvc-home -v $HOME/opt:$HOME/opt hailstone-msvc-build:24.04 \
    $HOME/opt/msvc-wine-src/install.sh $HOME/opt/msvc
# a build: Release, in build-msvc/ (the browser helper too: HAILSTONE_BROWSER is on by default)
docker run --rm --cpus 20 --user $(id -u):$(id -g) -e HOME=$HOME/opt/msvc-home -e MSVC_DIR=$HOME/opt/msvc \
    -v $HOME/opt:$HOME/opt -v "$PWD:$PWD" -w "$PWD" hailstone-msvc-build:24.04 tools/docker/msvc_build.sh . build-msvc
WINEDEBUG=-all EQ_GAME_DIR= HAILSTONE_TEST_TRACE=1 wine build-msvc/hailstone_tests.exe
```

Wine refuses a home folder it does not own, hence `HOME` under `~/opt`. The folders the build reads (a worktree's
dependency sources through `FETCHCONTENT_SOURCE_DIR_*`, a symlinked plugin clone) must be mounted at the same path.
Under Wine a fast fail shows as doctest's "test case CRASHED: SIGSEGV", with the case named. This is the build the
Windows release is made from (docs/updater.md "Making a release").

## clang-tidy

Not in CI at present (see above). By hand: configure the tree with Clang (no build needed) and run `run-clang-tidy -p build -quiet -header-filter='.*/src/.*' '.*/src/.*\.cpp$'` with the root `.clang-tidy`; the CI job that did this is in the workflow's history.

## macOS arm64

`.github/workflows/macos.yml` builds on GitHub's `macos-15` runner (Apple Silicon, Apple Clang): a Release build,
`hailstone_tests` (without game files or vectors), `cmake --install`, and an ad hoc signature on every Mach-O file of the
install (Apple Silicon runs no unsigned code, and the install's rpath edits can leave the linker's signature stale).
It fails when the binary needs a library outside the system's and its own folder. The run's artifact,
`hailstone-macos-arm64`, is the installed folder as a tar, which keeps the executable bits an artifact's zip drops.

Start it from the Actions tab or with `gh workflow run macos.yml -f ref=<commit or tag>`; a push to a branch named
`macos/<topic>` starts it too. Then `gh run download <run> -n hailstone-macos-arm64` and `tar xf` it. The repository
is public, so its minutes are free.

The build sets `CMAKE_OSX_DEPLOYMENT_TARGET` to 13.3 (the first macOS whose libc++ formats floating-point numbers),
fetches MoltenVK's prebuilt release (cmake/Dependencies.cmake) and leaves the in-game browser out (CEF needs app
bundles there). There is no Developer ID signature or notarization, by choice: players clear the download's
quarantine (README.md "Download").

