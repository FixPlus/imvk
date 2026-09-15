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
generators, set `-DCMAKE_BUILD_TYPE=Debug` while configuring and add build subdirectories like `build/Debug` for different configs; the `--config`
and `-C` arguments are harmless or may be omitted.

## Development notes

- Public headers live in `include/imvk`; implementations live in `lib`.
- Add tests under `tests` and register them with `add_test` in
  `tests/CMakeLists.txt`.
- Follow the repository's LLVM-based `.clang-format` style.
- Do not edit third-party sources under `examples/external` unless explicitly
  updating a submodule.