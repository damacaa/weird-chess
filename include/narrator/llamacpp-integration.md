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

## Vendoring llama.cpp

The project integrates `llama.cpp` directly in `CMakeLists.txt` via CMake `FetchContent` (pinned to tag `b4500`):

```cmake
FetchContent_Declare(
    llama
    URL https://github.com/ggml-org/llama.cpp/archive/refs/tags/b4500.tar.gz
)
FetchContent_MakeAvailable(llama)
target_link_libraries(${PROJECT_NAME} PRIVATE WeirdEngine llama)
```

Unnecessary components (CUDA, Vulkan, server, tests, examples) are disabled to ensure lightweight CPU compilation and minimal build overhead.

## Key Features & Design Details

### 1. Model Discovery & Bundled Model
- Ships with `assets/model/tinyllama-15M-stories-Q2_K.gguf` for out-of-the-box CPU inference.
- Model discovery checks the configured name in `assets/config.json`, the `LLAMA_MODEL_PATH` environment variable, or scans `assets/model/`, `assets/models/`, `bin/models/`, and `models/` for any `.gguf` file.

### 2. Turn Buffering (Option C Cadence)
- Standard moves from White are buffered and paired with Black's response so that the story narrates cohesive two-player actions and counter-actions per turn.
- Critical events (checks, checkmates, catastrophic blunders, and game ends) bypass buffering and narrate immediately.

### 3. Engine Font Sanitization
- Output text is filtered through `LlamaNarrator::sanitizeForEngine()` to conform strictly to WeirdEngine's 2D SDF font charset (`A-Z`, `a-z`, `0-9`, and `!"&_*()-=+?|.,:;`). Apostrophes and non-supported unicode characters are converted or stripped.
- `trimToCompleteSentence()` ensures that paragraphs always end cleanly on complete sentences without dangling punctuation or fragments.

### 4. Typewriter Pacing
- `NarrativeRenderSystem` reveals story text char-by-char with a minimum speed floor (configurable via `typewriter_speed` in `assets/config.json`) and dynamic acceleration when large queues build up.

### 5. Story Status & Dynamic Climax
- `StoryStatus::Generating`: Active story progression during normal play.
- `StoryStatus::EndedAbruptly`: Triggered immediately upon a critical blunder, surrender, or checkmate.
- `StoryStatus::EndedNaturally`: Triggered upon standard game conclusion / draws.

### 6. Clean Shutdown & Cancellation
- `LlamaNarrator` monitors an `std::atomic<bool> m_cancel` flag during token generation, ensuring instant termination and zero lockups when changing scenes or resetting the match.
