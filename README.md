# Switch-Alpaka

Run local LLMs (GGUF models via [llama.cpp](https://github.com/ggml-org/llama.cpp)) natively on a
homebrew-enabled Nintendo Switch — no internet connection required. CPU-only inference, console-based
UI, model picker, and persistent multi-chat history saved to the SD card.

![status](https://img.shields.io/badge/status-alpha-orange)

## Features

- Pick any `.gguf` model dropped into `sdmc:/switch/llm/models/`
- Multiple saved chats per model, stored as plain text on the SD card
- Multi-turn conversation context (not just single-shot Q&A)
- Runs fully offline, CPU-only (Tegra X1, no GPU/CUDA support — see [Background](#background))

## Requirements

- A Switch with Atmosphère (or compatible) CFW and the homebrew launcher
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with `devkitA64`, `switch-dev`, and
  `switch-tools` installed
- A quantized `.gguf` model (see [Getting a model](#getting-a-model))

## Building

### 1. Install devkitPro (if you haven't already)

```bash
wget https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x ./install-devkitpro-pacman
sudo ./install-devkitpro-pacman
sudo dkp-pacman -S devkitA64 switch-dev switch-tools
```

Make sure your environment has:
```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=${DEVKITPRO}/devkitA64
export PATH=${DEVKITPRO}/tools/bin:${DEVKITA64}/bin:${PATH}
```

### 2. Clone and patch llama.cpp

llama.cpp doesn't support Horizon OS out of the box. This repo ships a patch that adds Switch/newlib
compatibility (no `dlfcn.h`/`mmap`, missing `posix_memalign`/`sysconf`, forces the CPU backend, etc).

```bash
git clone https://github.com/ggml-org/llama.cpp
cd llama.cpp
git apply ../patches/llama-switch.patch
```

### 3. Cross-compile llama.cpp for the Switch

```bash
mkdir build-switch && cd build-switch
cmake .. -DCMAKE_TOOLCHAIN_FILE=../switch-toolchain.cmake \
  -DGGML_OPENMP=OFF \
  -DGGML_NATIVE=OFF \
  -DLLAMA_CURL=OFF \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLAMA_BUILD_TOOLS=OFF \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_SERVER=OFF \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_APP=OFF \
  -DGGML_BACKEND_DL=OFF \
  -DGGML_CPU=ON \
  -DLLAMA_BUILD_COMMON=OFF
make -j$(nproc)
cd ../..
```

The toolchain file (`switch-toolchain.cmake`) is included in this repo — copy it into your llama.cpp
checkout before running `cmake`:
```bash
cp switch-toolchain.cmake llama.cpp/
```

### 4. Build Switch-Alpaka

Edit the `LLAMA_DIR` variable at the top of the `Makefile` if your llama.cpp checkout isn't at
`~/llama.cpp`, then:

```bash
export DEVKITPRO=/opt/devkitpro
make -j$(nproc)
```

This produces `switch-llm-console.nro`.

## Installing on your Switch

Copy the following to your SD card:

```
sdmc:/switch/switch-llm-console.nro
sdmc:/switch/llm/models/<your-model>.gguf
```

`sdmc:/switch/llm/chats/` is created automatically on first launch.

## Getting a model

Any GGUF model works in principle, but stick to **small, heavily quantized (Q4) models** — the Switch
has 4 GB of RAM total and homebrew gets a fraction of that. Known-good starting points:

- [Qwen2.5-0.5B-Instruct (Q4_K_M)](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF) — ~380 MB, fastest, least coherent
- [Gemma-2-2B-it (Q4_K_M)](https://huggingface.co/bartowski/gemma-2-2b-it-GGUF) — ~1.6 GB, noticeably better quality, needs the heap-reserve bump already set in the Makefile

Drop the `.gguf` file into `sdmc:/switch/llm/models/`.

## Controls

| Screen | Button | Action |
|---|---|---|
| Model picker | D-Pad / A | Select model |
| Chat picker | D-Pad / A / B | New or existing chat / back |
| Chat | A | Ask a question (opens the system keyboard) |
| Chat | B | Back to chat list (model stays loaded) |
| Any screen | + | Quit |

## Background

This started as an experiment to see whether a Tegra X1 (Switch's SoC, same chip as the Jetson TX1)
could run LLM inference at all under homebrew. Short version: yes, CPU-only, no CUDA (Nintendo never
shipped the proprietary Nvidia CUDA driver stack on Horizon OS, and GPU compute would require a
from-scratch Vulkan/deko3d compute backend for ggml — out of scope for now).

Expect roughly 1–3 tokens/second depending on model size and quantization. This is a fun toy, not a
production inference stack.

## Known limitations

- No GPU acceleration
- Conversation history is re-fed to the model on every turn (no incremental KV-cache reuse across
  turns), so long chats get slower and can eventually exceed the context window
- Console-only UI (an SDL2 graphical UI was attempted but hit unresolved runtime crashes — contributions
  welcome, see [Contributing](#contributing))

## Contributing

PRs welcome, especially around:
- SDL2/graphical UI (see [Known limitations](#known-limitations))
- Chat management (rename/delete)
- Context window handling for long conversations
- Testing on Switch V1 vs Mariko/V2 hardware

## License

MIT for the code in this repo. llama.cpp itself is MIT-licensed; see the patch file for the exact
changes applied for Switch compatibility.
