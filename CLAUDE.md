# CLAUDE.md

Guidance for working in this repository. This is the **v2.0** line of lsfg-vk
(fork `vh-x/lsfg-vk`, upstream `PancakeTAS/lsfg-vk`; default branch `develop`).

## What this is

**lsfg-vk** is a Linux **Vulkan implicit layer** that injects interpolated
("generated") frames into any Vulkan application, reimplementing the frame
generation algorithm from the Windows app **Lossless Scaling**. The actual
interpolation shaders are *not shipped*: they are extracted at runtime from the
user's own `Lossless.dll` (the user must own Lossless Scaling on Steam). See
`docs/Journey.md` for the developer's account of how the D3D11 pipeline was
reverse-engineered and ported to Vulkan.

Linux/Vulkan-only. There is no Windows or macOS build. Developed/tested with
clang; the build environment here (macOS) cannot compile or run it — reason
about the code statically rather than trying to build.

## Module layout

The project is one CMake build with five subprojects:

| Module             | Type             | Purpose |
|--------------------|------------------|---------|
| `lsfg-vk-common`   | static lib       | Shared Vulkan RAII wrappers (`vk::` namespace), TOML config (`ls::` namespace), process detection, path/error helpers. Has no knowledge of frame generation. |
| `lsfg-vk-backend`  | static lib       | The frame-generation engine. Extracts shaders from `Lossless.dll`, builds the compute-shader pipeline ("shaderchains"), and runs interpolation on imported images. Public API: `lsfg-vk-backend/lsfgvk.hpp` (`lsfgvk::backend` namespace). |
| `lsfg-vk-layer`    | **shared lib**   | The actual Vulkan layer (`liblsfg-vk-layer.so`). Hooks `vkCreate/Destroy{Instance,Device,SwapchainKHR}` and `vkQueuePresentKHR`. `lsfgvk::layer` namespace. |
| `lsfg-vk-cli`      | executable       | `lsfg-vk-cli` — `validate`, `benchmark`, `debug` subcommands (getopt-based). |
| `lsfg-vk-ui`       | executable (Qt6) | `lsfg-vk-ui` — QML config editor. Off by default. |

Dependency direction: `layer`/`cli`/`ui` → `backend` → `common`. Backend and
common never depend on the layer.

### Where things live
- Layer entry/hooks: `lsfg-vk-layer/src/entrypoint.cpp` (loader negotiation +
  all `myvk*` hook functions), `instance.hpp/.cpp` (`Root` — config + lifecycle),
  `swapchain.hpp/.cpp` (`Swapchain` — per-swapchain present logic).
- Backend orchestration: `lsfg-vk-backend/src/lsfgvk.cpp` (`InstanceImpl`,
  `ContextImpl`, pipeline wiring in `ContextImpl`'s constructor and
  `scheduleFrames`).
- Shader pipeline stages: `lsfg-vk-backend/src/shaderchains/` (alpha0/1, beta0/1,
  gamma0/1, delta0/1, mipmaps, generate). These mirror the original D3D11 passes.
- DLL/shader extraction: `lsfg-vk-backend/src/extraction/` (`dll_reader` =
  PE parsing of `Lossless.dll`, `shader_registry` = SPIR-V/DXBC handling).
- Vulkan wrappers: `lsfg-vk-common/src/vulkan/` (one file per object: buffer,
  image, command_buffer, fence, semaphore, timeline_semaphore, sampler,
  descriptor_pool/set, shader, plus `vulkan.cpp` = instance/device/funcs).
- Config: `lsfg-vk-common/src/configuration/config.cpp` (TOML parse/write,
  env-var overrides) and `detection.cpp` (process → profile matching).

## How it works (mental model)

1. **Layer load.** Implicit layer, enabled by simply being installed; disabled
   with `DISABLE_LSFGVK=1`. On `vkNegotiateLoaderLayerInterfaceVersion` it
   constructs a `Root`, which reads config and detects the running process. If no
   profile matches (`Root::active()` false), the layer disables itself.
2. **Instance/device creation.** Hooks walk the `pNext` layer-link chain, advance
   it for the next layer, then call `Root::modify*CreateInfo` to add required
   extensions (shared-memory/semaphore, PCI bus info, etc.) before forwarding.
3. **Swapchain creation.** `modifySwapchainCreateInfo` forces FIFO present mode
   and adds `TRANSFER_SRC/DST` usage, and bumps image count to leave room for
   inserted frames. A per-swapchain `Swapchain` context is created. Surfaces too
   small to drive the pipeline (`Root::canDriveExtent` — needs ≥64px in each
   flow dimension because the backend builds a 7-level mipmap chain) are tracked
   but passed through untouched.
4. **Present.** `myvkQueuePresentKHR` routes each swapchain through its
   `Swapchain::present`, which copies the app's frame out, asks the backend to
   generate intermediate frames, and presents real + generated frames in order.
   FIFO/V-Sync is what paces insertion (no separate timing thread — `VkQueue`
   isn't thread-safe). Config changes are hot-reloaded here (`Root::update`).
5. **Frame generation (backend).** Runs on a **separate Vulkan device/instance**
   (needs Vulkan 1.3 for the DXVK-derived shaders). Images and a timeline
   semaphore are shared between the app device and the framegen device via
   exported FDs. `ContextImpl` imports those FDs, builds the whole shaderchain
   graph once, then `scheduleFrames` submits a pre-pass + one main pass per
   generated frame, synchronized with the imported timeline semaphore. See the
   contract documented on `Instance::openContext` in `lsfgvk.hpp`.

## Configuration

- File: `~/.config/lsfg-vk/conf.toml` (auto-created on first run). `[global]` +
  repeated `[[profile]]` sections. Full reference: `docs/Configuration.md`.
- Profiles match a process via `active_in` (linux binary name, windows .exe
  name, process name, or path suffix).
- Hot-reloadable: multiplier, flow_scale, performance_mode. Others need a
  swapchain recreation or app restart.
- Env vars: `DISABLE_LSFGVK`, `LSFGVK_CONFIG`, `LSFGVK_PROFILE`, and (with
  `LSFGVK_ENV=1`) `LSFGVK_DLL_PATH`, `LSFGVK_NO_FP16`, `LSFGVK_MULTIPLIER`,
  `LSFGVK_FLOW_SCALE`, `LSFGVK_PERFORMANCE_MODE`, `LSFGVK_PACING`, `LSFGVK_GPU`.

## Building

Linux only, clang recommended. Standard invocation (matches CI):
```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DLSFGVK_BUILD_UI=ON
cmake --build build
```
CMake options (top of root `CMakeLists.txt`): `LSFGVK_BUILD_VK_LAYER` (ON),
`LSFGVK_BUILD_CLI` (ON), `LSFGVK_BUILD_UI` (OFF), `LSFGVK_INSTALL_DEVELOP`,
`LSFGVK_INSTALL_XDG_FILES`, `LSFGVK_LAYER_LIBRARY_PATH` (set when installing to a
non-system prefix, e.g. `../../../lib/liblsfg-vk-layer.so`),
`LSFGVK_TESTING_RENDERDOC`. Full guide: `docs/Building-From-Source.md`.

There is **no test suite**. Verification is the CLI (`lsfg-vk-cli validate`,
`benchmark`, `debug <folder>`) and running real Vulkan apps. CI
(`.github/workflows/ci.yml`) only builds + packages on push to `develop`/`release`.

## Conventions (match the existing code)

- **C++20**, clang with `-Weverything` in Debug (see root `CMakeLists.txt` for the
  curated `-Wno-*` exclusions). `clang-tidy` runs in Debug builds; per-module
  `.clang-tidy` files enable nearly all check groups. Keep `// NOLINT` comments
  with their existing justifications.
- **Namespaces:** `vk::` (common Vulkan wrappers), `ls::` (common config/helpers),
  `lsfgvk::backend`, `lsfgvk::layer`, `lsfgvk::cli`.
- Every source file starts with `/* SPDX-License-Identifier: GPL-3.0-or-later */`.
  The project is GPL-3.0-or-later.
- **RAII everywhere.** Vulkan objects are wrapped one-class-per-file in `vk::`.
  Custom smart helpers in `lsfg-vk-common/.../helpers/pointers.hpp`: `ls::R<T>`
  (= `reference_wrapper`), `ls::lazy<T>` (deferred-init optional with checked
  access), `ls::owned_ptr<T>` (move-only owning pointer with optional deleter).
  Prefer these over raw `new`/`delete`.
- **Error handling:** throw `ls::error` / `ls::vulkan_error` (common) or
  `lsfgvk::backend::error` (backend); the layer hooks catch at the Vulkan
  boundary, log `lsfg-vk: ...` to `std::cerr`, and translate to `VkResult`.
  Don't let exceptions escape a Vulkan callback.
- Doc comments use `///` with `@param`/`@return`/`@throws`. Headers carry the
  contract; keep them as the source of truth (e.g. the frame-sync protocol on
  `Instance::openContext`).
- The Vulkan loader can't cleanly destroy instances/devices, so
  `backend::makeLeaking()` intentionally leaks them — this is deliberate, not a
  bug.

## Gotchas

- Globals `layer_info` / `instance_info` in `entrypoint.cpp` are raw-pointer
  singletons managed by the loader lifecycle; multiple `VkInstance`s share one
  `InstanceInfo` (the layer supports multiple instances — see commit history).
- Always `DeviceWaitIdle` before tearing down per-swapchain resources
  (semaphores/command buffers/fences) — present, recreate, and destroy paths all
  do this; preserve it when editing swapchain lifecycle.
- The shaderchain graph in `ContextImpl`'s constructor hard-codes the 7 mipmap
  levels and the alpha/beta/gamma/delta wiring to mirror the original Lossless
  Scaling pipeline. Changes here must match the algorithm, not just compile.
- `dist/` holds packaging (Flatpak manifests, local layer json); `docs/` is
  user-facing. `.serena/` and `build*/` are tooling/output — ignore.
