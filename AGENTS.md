# AGENTS.md

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

Run: `./build/WeirdChess` (windowed 1280x720; the window expects a display).

## Tests

```bash
./build/WeirdChessTests [path-to-stockfish]
```

Covers: board rules, make/undo, checkmate/fork/pin detection, move
classification thresholds, NullAI, the Stockfish UCI adapter (handshake,
evaluate, bestMove, shutdown) and a full AI-vs-AI game through the annotation
pipeline + narrator thread. No window or ECS required.

## Debug helpers

- `WEIRDCHESS_AUTOPLAY=1 ./build/WeirdChess` - human side plays random legal
  moves (exercises the full ECS/render pipeline headlessly).
- `STOCKFISH_PATH=/path/to/stockfish ./build/WeirdChess` - override the
  engine binary location.
- The ImGui overlay shows FEN, turn, AI and last annotation.

## Project conventions

- The scene is a thin dispatcher: `scenes/ChessScene.h` only registers
  systems (`addStartSystem`, `addUpdateSystem`, ...). Game logic lives in one
  header per system under `systems/`. Do not inline game logic in the scene.
- The board (`chess::Board` via `ChessLibBoard`) is the single source of
  truth; ECS piece entities are a visual mirror rebuilt by
  `MoveSystem::syncPieces` after every move.
- Move application is split: `MoveSystem::applyMove` applies the move and
  starts the piece animation IMMEDIATELY, then queues the annotation on the
  `AsyncAnnotator` worker thread (chess/AsyncAnnotator.h). The annotation is
  published by `AnnotationSystem::update` when ready. The AI reply
  (`AISystem::update`) waits for both the annotation and the animation
  (`!moveAppliedPendingAnnotation && animatingPieces.empty()`), so the
  player's piece never waits for the opponent.
- The annotation worker is the only caller of `IChessAI::evaluate` while a
  job is in flight: the main thread must not talk to the engine concurrently
  (aiSystem and the strength toggle are gated on the pending flag).
- All chess access goes through `IChessAI` + `ChessLibBoard`; adding a new
  engine = new `IChessAI` impl (see `docs/adapters.md`). Keep chess.hpp and
  Stockfish types out of systems other than the adapters.
- Text must respect the engine font charset: A-Z, a-z, 0-9 and
  `!"&_*()-=+?|.,:;`. No `#`, no apostrophes, no unicode.
- All screen-space UI must be repositioned by `systems/layoutSystem.h` when
  the window resizes (the engine UI is not responsive).
- Stage 2 (llama.cpp) must only touch the narrator seam
  (`narrator/INarrator.h` + `NarratorThread`); generation must stay on the
  worker thread. Design doc: `include/narrator/llamacpp-integration.md`.
- Style: C++20, Allman braces, tabs, 120-column limit (same as weird-engine).
  Run `clang-format -i <file>` after editing.
- Git commits: DO NOT create git commits automatically unless the user
  explicitly asks you to commit.

## Gotchas

- DO NOT call `services.physics().pause()`: with the simulation paused the 2D
  world shapes stop rendering (verified empirically; the render path depends
  on the simulation stepping). The chess game has no rigid bodies, so just
  leave the physics running.
- Board squares are border-only (`DefaultShapes::BOX_LINE`), never filled
  boxes: a filled square's interior SDF dominates any piece standing on it
  (both live in the same SDF group and the renderer keeps the most-negative
  distance), making pieces invisible.
- Highlight material changes are baked into the generated world shader, so
  batch them: update all 64 highlights, then call
  `services.render().forceShaderRefresh2D()` once (see
  `MoveSystem::refreshHighlights`). A per-highlight refresh recompiles the
  whole 160-shape shader every call.
- `applyMove` clears the in-flight animation list BEFORE `syncPieces` (the
  rebuild destroys every piece entity; a stale animation entry touching a
  dead entity asserts in the engine's Transform array). The animation system
  also drops entries whose entity is gone defensively.
- `SDL_CreateProcess` stdio streams are NON-BLOCKING: reads must poll with
  `SDL_GetIOStatus == SDL_IO_STATUS_NOT_READY` (see `readLine` in
  `src/chess/StockfishUCIAI.cpp`).
- `StockfishUCIAI::shutdown` must write `quit` directly, not through
  `sendLine` (which is gated by the shutdown flag).
- `ChessLibBoard` records its own move history (chess.hpp's `unmakeMove`
  requires the move that was made); keep `makeMove`/`undoMove` paired.
- The `ShapeButton`/`ShapeToggle` systems consume clicks and set
  `Input::flagUIClick()`; always check `services.input().isUIClick()` before
  world picking.
- World text and UI text have separate `SDFRenderSystemContext`s; text size
  is set in `onStartBoardSystem` via `services.render().getContext2D()/
  getContextUI()`.
- The engine's `WEIRD_AUTO_QUIT_SECONDS` hook re-reads the env var every
  frame (sliding window), so it never fires on its own - use `timeout` for
  headless runs instead.
