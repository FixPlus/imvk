# AGENTS.MD

## Project overview

`imvk` is a modular C++23 Vulkan rendering library built on `vkw`. Its static
library components cover core rendering infrastructure, graphics, copy
operations, and graph-based workflows. The repository also includes a GLFW/
Dear ImGui example application and CTest-based graph pass tests.

## Prerequisites

- CMake 3.25 or newer and a C++23-capable compiler
- CMake packages for `vkw` 0.0.4+, GLFW 3.3+, and GLM
- Initialized Git submodules for Dear ImGui and imgui-node-editor
- Optional: `glslangValidator` from the Vulkan SDK to compile example shaders

If dependencies are installed under a custom prefix, pass it through
`-DCMAKE_PREFIX_PATH=<prefix>` or use the appropriate CMake toolchain file.

## Configure, build, and test

Run commands from the repository root:

```sh
git submodule update --init --recursive
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Use `Release` instead of `Debug` when needed. For single-configuration
generators, set `-DCMAKE_BUILD_TYPE=Debug` while configuring and add build
subdirectories like `build/Debug` for different configs; the `--config` and
`-C` arguments are harmless or may be omitted.

### Windows/MSVC configuration notes

- Do not replace CMake's default `CMAKE_CXX_FLAGS` when adding options. MSVC
  builds require exception handling (`/EHsc`); without it, Boost detects
  `BOOST_NO_EXCEPTIONS`, and code using Boost.StaticString can fail to link with
  an unresolved `boost::throw_exception` symbol. Prefer target-level options,
  for example `target_compile_options(<target> PRIVATE /FS)`. If repairing an
  existing cache that contains only `/FS`, reconfigure it with:

  ```powershell
  cmake -S . -B build `
    -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc /FS"
  ```

  A correctly generated Visual Studio project reports synchronous exception
  handling (`<ExceptionHandling>Sync</ExceptionHandling>`). Rebuild affected
  static libraries after correcting the flag because Boost's behavior is
  selected while compiling them, not while linking the executable.

- Match prebuilt dependencies to the selected configuration and MSVC runtime.
  In particular, do not link a Release-only GLFW package into a Debug build.
  Release GLFW normally requests `MSVCRT`, while Debug targets use `MSVCRTD`;
  mixing them produces linker warning `LNK4098`. Point `glfw3_DIR` at a Debug
  installation for Debug builds, or preferably use a package installation that
  exports both Debug and Release imported configurations:

  ```powershell
  cmake -S . -B build -DBUILD_TESTING=ON `
    -Dglfw3_DIR="<debug-or-multi-config-glfw>/lib/cmake/glfw3"
  ```

  For configuration-specific dependency installations, use separate build
  directories (for example `build/Debug` and `build/Release`) with matching
  `glfw3_DIR` values. `dumpbin /directives <library>.lib` can be used to inspect
  whether a library requests `MSVCRT` or `MSVCRTD`.

## Development notes

- Public headers live in `include/imvk`; implementations live in `lib`.
- Add tests under `tests` and register them with `add_test` in
  `tests/CMakeLists.txt`.
- Follow the repository's LLVM-based `.clang-format` style.
- Do not edit third-party sources under `examples/external` unless explicitly
  updating a submodule.