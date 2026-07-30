# llama.cpp Switch patch — manual notes

An automated `.patch` file couldn't be generated from this session (the edits were made
interactively on the target machine). Until a proper diff is committed here, apply these
changes manually to a fresh llama.cpp checkout:

1. **`ggml/src/ggml-backend-dl.h`** — guard the `dlfcn.h` include and stub `dl_handle_deleter`
   under `#elif defined(__SWITCH__)` (no dynamic loading on Horizon OS).

2. **`ggml/src/ggml-backend-dl.cpp`** — add a matching `#elif defined(__SWITCH__)` branch with
   stub `dl_load_library` / `dl_get_sym` / `dl_error` implementations (all unreachable, since
   everything is statically linked into one NRO).

3. **`ggml/src/ggml-backend-reg.cpp`** — same `__SWITCH__` guard around the top-of-file
   `dlfcn.h`/`unistd.h` include block; `get_executable_path()` already has a generic
   `#else return {};` fallback that Switch falls into automatically.

4. **`ggml/CMakeLists.txt`** build flags — build with:
   `-DGGML_BACKEND_DL=OFF -DGGML_CPU=ON -DLLAMA_BUILD_COMMON=OFF -DLLAMA_BUILD_APP=OFF`
   and comment out `add_subdirectory(vendor/cpp-httplib)` in the top-level `CMakeLists.txt`
   (httplib needs `ifaddrs.h`, not available under newlib, and isn't needed for inference).

5. **`source/compat.c`** (in this repo, already included) — provides `posix_memalign` (via
   `memalign`) and `sysconf` (hardcoded to 4 cores / 4096-byte pages), both missing from
   devkitA64's newlib.

6. **Toolchain** — build with `-D__SWITCH__ -fPIE` (see `switch-toolchain.cmake` in repo root).
   The `-fPIE` flag is required on *all* object files being linked into the final NRO, including
   llama.cpp/ggml's — mixing PIE and non-PIE objects fails at link time with
   "read-only segment has dynamic relocations".

Contributions turning this into a real `git diff`-based patch file are very welcome.
