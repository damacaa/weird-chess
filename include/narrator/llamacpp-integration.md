# llama.cpp Narrator Integration

This document outlines the architecture and implementation of the local LLM narrator in Weird Chess.

## Goals

- A local LLM (small GGUF model) generates serialized fictional prose that mirrors the *drama* of the chess match: forks become betrayals, blunders become catastrophes, sacrifices become daring gambles, and deep endgames produce escalating narrative tension.
- The story does **not** mention chess pieces directly (no kings, queens, rooks, knights, or pawns). Instead, tactical moves and position shifts translate into character actions and rivalries.
- Generation runs **strictly** on the `NarratorThread` worker thread. The main render and simulation threads are never stalled.
- If no GGUF model is found, the system gracefully falls back to `PassThroughNarrator` (technical move annotations and tactical alerts).

## Architecture

```
main thread                          worker thread (NarratorThread)
-------------                        -------------------------------
annotationSystem  ── push() ──▶  queue ──▶ LlamaNarrator::narrate(ann, out)
                                                           │
narrativeRenderSystem ◀── drain() ── StoryStream (mutex) ◀─┘
```

- `NarratorThread` (`include/narrator/NarratorThread.h`): Owns the background worker thread, task queue, cancellation lifecycle, and thread-safe `StoryStream`.
- `INarrator` (`include/narrator/INarrator.h`): Abstract interface implemented by `LlamaNarrator` and `PassThroughNarrator`.
- `LlamaNarrator` (`include/narrator/LlamaNarrator.h`): Manages the `llama_model`, `llama_context`, sampling chain, turn buffering, prompt construction, and output sanitization.

## Vendoring llama.cpp & Hardware Acceleration

The project integrates `llama.cpp` directly in `CMakeLists.txt` via CMake `FetchContent` (pinned to tag `b4500`):

```cmake
find_package(Vulkan COMPONENTS glslc QUIET)
if(Vulkan_FOUND AND Vulkan_GLSLC_EXECUTABLE)
    set(GGML_VULKAN ON CACHE BOOL "" FORCE)
else()
    set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    llama
    URL https://github.com/ggml-org/llama.cpp/archive/refs/tags/b4500.tar.gz
)
FetchContent_MakeAvailable(llama)
target_link_libraries(${PROJECT_NAME} PRIVATE WeirdEngine llama)
```

- When Vulkan and `glslc` are available on the system, `GGML_VULKAN` is automatically enabled, allowing GPU offloading on compatible hardware (e.g. Steam Deck RDNA 2 GPU).
- Otherwise, it seamlessly falls back to optimized multi-threaded CPU inference.

## Key Features & Design Details

### 1. Model Discovery & GPU / CPU Selection
- Model settings in `assets/config.json` support `"device": "gpu"` or `"device": "cpu"`, `"gpu_layers": 99`, and CPU thread count.
- Model discovery checks the configured name in `assets/config.json`, the `LLAMA_MODEL_PATH` environment variable, or scans `assets/model/`, `assets/models/`, `bin/models/`, and `models/` for any `.gguf` file.

### 2. KV Cache Prefix Reuse (Prefix Caching)
- `LlamaNarrator::generateText` caches the tokenized prompt prefix across turns.
- Consecutive story turns share common system prompt, premise, and past history tokens. `llama_kv_cache_seq_rm()` trims only the divergent tail, evaluating only the newest turn delta (~30-50 tokens) instead of re-evaluating the entire context from scratch.

### 3. Multi-Threaded Batch & Token Decode Scaling
- Prompt ingestion batch processing utilizes all available CPU cores (`n_threads_batch = hardware_concurrency()`) for fast prefill.
- Single-token decoding runs on 4 background threads with Flash Attention (`flash_attn = true`) and without per-token yield overhead.
- Immediate first-token streaming pushes token #1 to `StoryStream` on frame 0 to eliminate typewriter start latency.

### 4. Turn Buffering (Option C Cadence)
- Standard moves from White are buffered and paired with Black's response so that the story narrates cohesive two-player actions and counter-actions per turn.
- Critical events (checks, checkmates, catastrophic blunders, and game ends) bypass buffering and narrate immediately.

### 5. Engine Font Sanitization
- Output text is filtered through `LlamaNarrator::sanitizeForEngine()` to conform strictly to WeirdEngine's 2D SDF font charset (`A-Z`, `a-z`, `0-9`, and `!"&_*()-=+?|.,:;`). Apostrophes and non-supported unicode characters are converted or stripped.
- `trimToCompleteSentence()` ensures that paragraphs always end cleanly on complete sentences without dangling punctuation or fragments.

### 6. Typewriter Pacing
- `NarrativeRenderSystem` reveals story text char-by-char with a minimum speed floor (configurable via `typewriter_speed` in `assets/config.json`) and dynamic acceleration when large queues build up.

### 7. Story Status & Dynamic Climax
- `StoryStatus::Generating`: Active story progression during normal play.
- `StoryStatus::EndedAbruptly`: Triggered immediately upon a critical blunder, surrender, or checkmate.
- `StoryStatus::EndedNaturally`: Triggered upon standard game conclusion / draws.

### 8. Clean Shutdown & Cancellation
- `LlamaNarrator` monitors an `std::atomic<bool> m_cancel` flag during token generation, ensuring instant termination and zero lockups when changing scenes or resetting the match.
