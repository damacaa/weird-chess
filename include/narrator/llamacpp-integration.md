# llama.cpp integration plan (Stage 2 - NOT implemented yet)

This document is the design for the Stage-2 upgrade. Stage 1 ships with
`PassThroughNarrator`; Stage 2 replaces it with `LlamaNarrator` behind the
exact same `INarrator` interface and worker thread. Nothing else in the game
changes.

## Goals

- A local LLM (small GGUF model) writes a story that has **nothing to do with
  chess** but mirrors the *drama* of the match: forks become betrayals, a
  blunder becomes a catastrophe, a long endgame becomes a long story.
- Story length scales with game length: short games end abruptly (blunder /
  checkmate), long endgames produce long stories.
- Generation runs **only** on the `NarratorThread` worker thread. The main
  thread never touches the model.

## Architecture recap (already in place)

```
main thread                          worker thread (NarratorThread)
-------------                        -------------------------------
annotationSystem  ── push() ──▶  queue ──▶ INarrator::narrate(ann, out)
                                                          │
narrativeRenderSystem ◀── drain() ── StoryStream (mutex) ◀─┘
```

- `NarratorThread` (include/narrator/NarratorThread.h): owns the worker, the
  queue and the `StoryStream`. Already generic: it takes an `INarrator`.
- `INarrator::narrate(MoveAnnotation, StoryStream&)` is the whole contract.
- Stage 2 = new class `LlamaNarrator : public INarrator`, constructed where
  `PassThroughNarrator` is constructed today (systems/onStartBoardSystem.h).

## Vendoring llama.cpp

Option A (preferred): git submodule pinned to a release tag.

```bash
git submodule add https://github.com/ggml-org/llama.cpp third_party/llama.cpp
git submodule update --init --recursive
```

Option B: CMake FetchContent.

```cmake
include(FetchContent)
FetchContent_Declare(llama
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp
    GIT_TAG        <pinned-release-tag>)
FetchContent_MakeAvailable(llama)
target_link_libraries(${PROJECT_NAME} PRIVATE llama)
```

Notes:

- License: llama.cpp is MIT, compatible with this project.
- Disable unneeded features to keep the build lean:
  `GGML_CUDA=OFF`, `LLAMA_CURL=OFF`, `LLAMA_BUILD_TESTS=OFF`,
  `LLAMA_BUILD_EXAMPLES=OFF`, `LLAMA_BUILD_SERVER=OFF`.
- Model files (GGUF) live in `bin/models/` (gitignored; users drop their own).
  Suggested small models for weak hardware: Qwen2.5-0.5B-Instruct, Llama-3.2-1B,
  Phi-3-mini, or a TinyLlama GGUF, quantized (Q4_K_M or lower).

## LlamaNarrator design

```cpp
class LlamaNarrator : public INarrator
{
    llama_model*   m_model;
    llama_context* m_ctx;
    std::string    m_systemPrompt;   // built once at startup
    std::string    m_history;        // the story so far (kept across moves)

public:
    bool load(const std::string& ggufPath, const std::string& systemPrompt); // startup
    void narrate(const MoveAnnotation& ann, StoryStream& out) override;       // worker thread
    std::string name() const override { return "llama"; }
};
```

### Prompt construction

- System prompt (fixed): "You are the narrator of a serialized story. You
  receive a summary of dramatic events that happen between characters. Write
  the next paragraph of a story that has nothing to do with chess: no kings,
  no queens, no boards, no pawns. Map the events to human drama (betrayal,
  risk, sacrifice, rivalry, victory, defeat). Keep paragraphs short."
- Per move, the annotation JSON is formatted into the user turn:

```
Move 12. White plays Nf7x - FORK on the king and the rook. Eval 1.2 -> 2.8.
Best line: f7g8 ...
```

  The annotation contains exactly the fields Stage 1 already computes:
  title, san, quality, tactic flags, eval delta, engine line, game state.

### Story-length control

- `n_predict` is scaled with the game phase:
  - opening (plies < 10): short paragraph (~60 tokens)
  - middlegame: normal (~120 tokens)
  - long endgame (plies > 50 or only kings+pawns+1 minor each): longer
    paragraphs (~200 tokens), because the story "goes on for longer".
- Blunder or checkmate: immediately stop generation and write an abrupt
  ending line, then set `StoryStatus::EndedAbruptly` (same as the
  pass-through narrator does today). The game can then fade the panel.

### Token streaming

- Use `llama_token_to_piece` incrementally; accumulate into `StoryStream`
  in chunks of ~1 sentence (or ~20 tokens) so the UI scrolls naturally.
- Respect `StoryStream` threading: only the worker thread calls the model.

### Aborting generation

- Generation must not block scene shutdown: `NarratorThread::stop()` joins
  the worker. `LlamaNarrator::narrate` should check an `std::atomic<bool>
  cancel` flag every few tokens (set from `stop()` via a `NarratorThread`
  hook) and bail out early. Add the flag to `INarrator`/`NarratorThread` in
  Stage 2.

## Switching narrator at runtime

`ChessState::narratorImpl` is currently a `PassThroughNarrator` instance.
Stage 2 replaces its construction in `onStartBoardSystem` with:

```cpp
auto llama = std::make_shared<LlamaNarrator>();
if (llama->load(services.resources().assetPath("models/story.gguf"), kSystemPrompt))
    state.narrator = std::move(llama);
else
    state.narrator = std::make_shared<PassThroughNarrator>(); // graceful fallback
```

## Verification checklist (Stage 2)

1. Build with llama.cpp; binary size and memory are acceptable.
2. `NarratorThread` never stalls the frame loop (measure frame time while
   generating).
3. Blunder in a test position → story ends abruptly within one move.
4. Checkmate → abrupt end.
5. 60-ply game → story keeps growing, paragraphs lengthen.
6. Exit during generation → clean join, no crash (cancel flag).
