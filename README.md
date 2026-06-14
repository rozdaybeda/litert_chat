# litert_chat

C++ chat client for **Gemma 3**, **Gemma 4**, and **Qwen3** using Google's [LiteRT-LM](https://developers.google.com/edge/litert-lm/overview) runtime.

## Requirements

- Windows 10/11 x64
- [CMake](https://cmake.org/) 3.21+
- [Ninja](https://ninja-build.org/) — included with Visual Studio 2022 (`VC/Tools/Llvm/...`)
- [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` env var set
- **Compiler** (either):
  - Visual Studio 2022 (MSVC) — recommended, no extra setup
  - LLVM/Clang — standalone install or VS 2022 bundled Clang
- Python 3.9+ with `pip`

## Setup

### 1. Install the LiteRT-LM runtime

```
pip install litert-lm-api
```

### 2. Download a model

```
python scripts/setup.py                        # Gemma 4 E2B (default, 2.6 GB)
python scripts/setup.py --model gemma3-1b      # Gemma 3 1B INT4 (529 MB) *
python scripts/setup.py --model qwen3-0.6b     # Qwen3 0.6B INT8 (586 MB)
python scripts/setup.py --model qwen3-4b       # Qwen3 4B INT4 (2.5 GB)
```

\* Gemma 3 requires a HuggingFace account and accepting the model license.
Log in first with `huggingface-cli login`.

Optional flags:

```
python scripts/setup.py --model-dir D:\ai\models   # custom output directory
```

> **Note:** The Gemma 4 E2B `web` variant (`gemma-4-E2B-it-web.litertlm`) is compiled
> for browser-hosted WebGPU runtimes (e.g. MediaPipe in Chrome) and does not work
> with the native `litert-lm.dll` desktop runtime used here. Use the default `generic`
> variant for CPU/GPU desktop use.

### 3. Build

Open an **x64 Native Tools Command Prompt for VS 2022** (sets up compiler + Ninja in PATH), then:

```
cmake --preset x64-release
cmake --build --preset x64-release
```

To build with Clang instead of MSVC, set `CC` and `CXX` before configuring:

```
set CC=clang
set CXX=clang++
cmake --preset x64-release
cmake --build --preset x64-release
```

The build automatically copies `litert-lm.dll`, `dxcompiler.dll`, and `dxil.dll`
from the installed Python package next to the executable.

Available presets: `x64-release`, `x64-debug`.

## Run

```
build\x64-release\litert_chat.exe models\gemma-4-E2B-it.litertlm --gpu
build\x64-release\litert_chat.exe models\gemma3-1b-it-int4.litertlm --gpu
```

### Options

```
litert_chat.exe <model.litertlm> [options]

Options:
  --gpu          Use GPU backend via WebGPU/Direct3D 12 (default)
  --cpu          Use CPU backend
  --no-think     Disable thinking mode (Qwen3 models)
  --dll <path>   Path to litert-lm.dll (auto-detected if omitted)
  -h, --help     Show help
```

The DLL is also auto-discovered from the `LITERT_LM_DLL` environment variable
or the directory containing the executable.

## Chat commands

| Command | Description |
|---|---|
| `/translate <lang> <text>` | Fast translation — output only, no explanations |
| `/cpu` | Switch to CPU backend (reloads model) |
| `/gpu` | Switch to GPU backend (reloads model) |
| `/reset` | Clear conversation history |
| `/quit` | Exit |

### Examples

```
[gpu] You: What is the capital of France?
Gemma: The capital of France is Paris.
  [198 ms]

[gpu] You: /translate Ukrainian The weather is beautiful today, let's go for a walk.
  Ukrainian: Погода сьогодні чудова, підемо прогулятися.
  [192 ms]

[gpu] You: /translate French Good morning, how are you?
  French: Bonjour, comment allez-vous ?
  [185 ms]
```

## Benchmarks (RTX 5060 Ti)

Translation benchmark — 6 European languages, averaged across French / German / Spanish / Italian / Ukrainian / Polish.

| Model | GPU Load | GPU Avg | GPU tok/s | CPU Load | CPU Avg | CPU tok/s |
|---|---|---|---|---|---|---|
| Gemma 4 E2B (2.6 GB) | ~2.1 s | ~378 ms | **60.6** | ~0.2 s | ~876 ms | 15.1 |
| Gemma 3 1B (0.9 GB) | ~2.6 s | ~1114 ms | 12.1 | ~0.3 s | ~557 ms | **23.7** |
| Qwen3 0.6B | ~1.4 s | ~4430 ms | 7.6 | ~0.2 s | ~2609 ms | 7.7 |
| Qwen3 4B \* | — | — | — | ~2.2 s | ~4399 ms | 4.8 |

\* Qwen3 4B mixed\_int4 does not support GPU — CPU only.

First GPU inference is slower (~1–2 s extra) due to shader compilation; subsequent calls use cached shaders.

## Project structure

```
litert_chat/
├── include/
│   ├── litert_lm.h     # LiteRT-LM C API header
│   └── litert.h        # C++ Engine / Conversation wrapper
├── src/
│   ├── main.cpp        # CLI entry point
│   └── litert.cpp      # Engine and Conversation implementation
├── scripts/
│   └── setup.py        # Install deps + download model
├── models/             # Downloaded model files (gitignored)
├── CMakeLists.txt
├── CMakePresets.json
└── vcpkg.json
```

## License

The source code in this repository is released under the [MIT License](LICENSE).

**Dependencies:**
- [LiteRT-LM](https://github.com/google-ai-edge/LiteRT-LM) runtime — Apache 2.0
- Gemma 4 model weights — Apache 2.0 (downloaded separately, not included)
- Gemma 3 model weights — [Gemma Terms of Use](https://ai.google.dev/gemma/terms) (downloaded separately, not included)
- Qwen3 model weights — Apache 2.0 (downloaded separately, not included)

## How it works

`litert-lm.dll` is loaded at runtime via `LoadLibrary` — no import library
needed. All C API functions are resolved with `GetProcAddress` and stored as
function pointers in `LiteRtLmLib`. The `litert::Engine` and
`litert::Conversation` classes wrap the C API into RAII-safe C++ objects.

The `/translate` command creates a one-shot conversation with a strict system
message (`"Respond with ONLY the translated text"`) separate from the main chat
history, which keeps translations fast (~190 ms GPU) regardless of conversation
length.
