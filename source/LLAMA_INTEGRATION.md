# Wiring llama.cpp into the GUI build

The project's `Makefile` (one level up from `source/`) already points at your
existing llama.cpp checkout and build:

```
LLAMA_DIR   := $(HOME)/llama.cpp
LLAMA_BUILD := $(LLAMA_DIR)/build-switch
```

taken from your working `switch-llm-console` Makefile, not guessed. `ARCH` and
`CXXFLAGS` (`-march=armv8-a+simd ...`, `-std=gnu++20 -fno-rtti -fno-exceptions`)
are copied from that same file too, so the GUI's object files agree with
`libllama.a` on ABI-relevant flags.

Correction from the previous version of this note: I'd claimed
`llama_backend.cpp` needs `-fexceptions`. Your console Makefile proves that
wrong - it links against `libllama.a` fine with `-fno-exceptions` globally, so
the GUI Makefile does the same, no per-file override needed.

## Before your first build, please confirm these two paths exist

I could only verify `libllama.a` directly:

```
find ~/llama.cpp/build-switch -name "libllama.a"     # confirmed: build-switch/src/libllama.a
```

`ggml`, `ggml-cpu`, `ggml-base` I've assumed sit next to it under
`build-switch/ggml/src/`, matching your console Makefile's `EXTRA_LIBPATHS`,
but I have not seen this directory's contents myself. Please run:

```
find ~/llama.cpp/build-switch -name "libggml*.a"
```

If any of the three are missing or sit in a different subfolder, tell me the
actual paths (or the full `find ~/llama.cpp/build-switch -name "*.a"` output)
and I'll adjust `LIBS`/`EXTRA_LIBPATHS` in the Makefile accordingly.

## Mock vs. real backend

`main.cpp` calls `loadLlamaBackend()` (real inference) when a model is picked.
`mock_backend.cpp` is still in `source/` and still builds - it's just unused
by `main.cpp` right now. To test the GUI without waiting on a multi-GB model
load, swap the `loader.start(...)` call in `main.cpp` for
`chat.reset(new ChatScreen(createMockBackend(stripExtension(m.name), 6)));`
and skip the loading-screen branch.

## What to test first

1. Does the build link cleanly now, or does the linker complain about a
   missing `libggml*.a` (see above)?
2. Does model loading crash silently, or fail with the "Load failed: ..."
   toast on the picker screen? If it dies silently, the loader thread's stack
   size (`0x40000` = 256 KiB in `main.cpp`'s `Loader::start()`) is the first
   thing I'd increase.
3. Time-to-first-token after the spinner.
4. Whether the lighter sampler (penalties + temp 0.7 instead of pure greedy)
   changes Gemma-2's gibberish behaviour - worth noting either way, since it's
   an unplanned variable next to the chat-template fix.
