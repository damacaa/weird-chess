# Weird Chess ♟️📖

An experimental chess game built on **[weird-engine](https://github.com/damacaa/weird-engine)** (a 2D signed distance field ray-marching engine). 

**Weird Chess** pairs a full chess game with an evolving narrative panel:
- **Left Panel:** The interactive chessboard, smooth SDF piece rendering, animations, legal move indicators, and game control UI.
- **Right Panel:** A real-time story and analysis stream. Every move is evaluated and classified in real time (chess.com-style: *Best*, *Great*, *Excellent*, *Good*, *Inaccuracy*, *Mistake*, *Miss*, *Blunder*, with tactical alerts for *Forks*, *Pins*, *Skewers*, *Checks*, and *Mates*).

```
+-----------------------------------------+-----------------------------------------+
|                                         |  WEIRD CHESS                            |
|                                         |  White (12) - FORK Nf7+                 |
|                                         |  White forks the king and the rook      |
|   8  [r] [.] [b] [q] [k] [b] [.] [r]    |  Eval: +0.46 -> +2.85                   |
|   7  [p] [p] [p] [.] [.] [N] [p] [p]    |                                         |
|   6  [.] [.] [.] [.] [.] [.] [.] [.]    |  "The cavalry struck across the         |
|   5  [.] [.] [.] [p] [.] [.] [.] [.]    |  frontier, cutting the commander's      |
|   4  [.] [.] [.] [P] [.] [.] [.] [.]    |  escape line in two..."                 |
|   3  [.] [.] [N] [.] [.] [.] [.] [.]    |                                         |
|   2  [P] [P] [P] [.] [P] [P] [P] [P]    |                                         |
|   1  [R] [.] [B] [Q] [K] [B] [.] [R]    |                                         |
|       a   b   c   d   e   f   g   h     |                                         |
|                                         |                                         |
|                                         | [NEW GAME] [STRONG AI]                  |
+-----------------------------------------+-----------------------------------------+
```

---

## Table of Contents

- [Features](#features)
- [Architecture & Design](#architecture--design)
- [Libraries & External Dependencies](#libraries--external-dependencies)
- [Threading & Execution Lifecycle](#threading--execution-lifecycle)
- [Project Layout](#project-layout)
- [Building and Running](#building-and-running)
  - [Prerequisites](#prerequisites)
  - [Build Steps](#build-steps)
  - [Stockfish Engine Setup](#stockfish-engine-setup)
- [Controls](#controls)
- [Configuration & Strength Tuning](#configuration--strength-tuning)
- [Automated Tests & Debugging](#automated-tests--debugging)
- [Roadmap (Stage 2: Local LLM)](#roadmap-stage-2-local-llm)
- [Licenses](#licenses)

---

## Features

- **2D SDF Ray-Marched Graphics:** Board squares, pieces, highlights, and buttons are defined as Signed Distance Fields and ray-marched with crisp vector-like edges and procedural materials.
- **Asynchronous Move Classification:** Fast centipawn loss and tactical analysis without stalling the animation or render loop.
- **Tactic Detection Engine:** Real-time bitboard analysis detecting forks, absolute & relative pins, skewers, discovered attacks, checks, and mates.
- **Built-in In-Process Engine (`MinimaxAI`):** High-performance C++20 Alpha-Beta search with PeSTO tapered piece-square evaluation and Quiescence search. Runs 100% in-process out of the box and compiles directly to WebAssembly for web builds (e.g. itch.io).
- **Optional Stockfish UCI Support:** Subprocess adapter for Stockfish with humanized Elo tuning (shallow search depth, dynamic blunders, Elo limits) and tournament master strength toggle.
- **Responsive Layout System:** Dynamic resolution tracking and auto-reframing for arbitrary window sizes.
- **Modular ECS Architecture:** Thin scene dispatcher registering decoupled, single-responsibility systems.

---

## Architecture & Design

Weird Chess is built around clean boundaries and an Entity-Component-System (ECS) pattern. The board state in `chess.hpp` is the single source of truth, while ECS entities act as a visual mirror.

```
                  ┌───────────────────────────────┐
                  │    ChessLibBoard (chess.hpp)  │  <-- Single Source of Truth
                  └──────────────┬────────────────┘
                                 │
           ┌─────────────────────┼─────────────────────┐
           ▼                     ▼                     ▼
┌─────────────────────┐ ┌─────────────────┐ ┌─────────────────────┐
│  MoveSystem::sync   │ │ AsyncAnnotator  │ │ TacticDetector      │
│  (Rebuilds ECS      │ │ (Centipawn eval │ │ (Bitboard scan for  │
│   Piece Entities)   │ │  & grading)     │ │  forks/pins/skewers)│
└─────────────────────┘ └────────┬────────┘ └─────────────────────┘
                                 │
                                 ▼
                     ┌───────────────────────┐
                     │   NarratorThread      │
                     │   (StoryStream)       │
                     └───────────┬───────────┘
                                 │
                                 ▼
                     ┌───────────────────────┐
                     │ NarrativeRenderSystem │
                     │ (UI Panel Output)     │
                     └───────────────────────┘
```

### Core Interfaces & Seams

1. **Board & Rules (`ChessLibBoard`)**  
   Encapsulates move generation, legality validation, FEN parsing, check/mate/draw rules, and move history.
2. **AI & Analysis (`IChessAI`)**  
   Defines the contract for bot moves and position evaluations (`evaluate`, `bestMove`, `setStrength`, `setPosition`).
   - `MinimaxAI`: High-performance in-process Alpha-Beta engine with PeSTO piece-square evaluation and quiescence search (default for web and offline native builds; 100% MIT).
   - `StockfishUCIAI`: Communicates with a local Stockfish binary over standard UCI via non-blocking pipes (`SDL_CreateProcess`).
   - `NullAI`: Minimal fallback for testing without search logic.
3. **Narrator Pipeline (`INarrator` & `NarratorThread`)**  
   Worker thread receiving completed `MoveAnnotation`s and streaming generated story lines into a thread-safe `StoryStream`.

---

## Libraries & External Dependencies

The project is designed to be lightweight, modular, and easy to build:

| Library / Tool | Source | Integration Method | License | Purpose |
|---|---|---|---|---|
| **[weird-engine](https://github.com/damacaa/weird-engine)** | Sibling dir or GitHub | Local `add_subdirectory` or `FetchContent` | Project License | 2D SDF Raymarching engine, ECS, windowing, audio, and UI |
| **[SDL3](https://github.com/libsdl-org/SDL)** | Engine dependency | Built via weird-engine | zlib | Window creation, input handling, non-blocking subprocess I/O |
| **[Dear ImGui](https://github.com/ocornut/imgui)** | Engine dependency | Built via weird-engine | MIT | Debug overlay (FEN, turn counter, engine status, last eval) |
| **[chess-library](https://github.com/Disservin/chess-library)** | `third_party/chess/` | Vendored header (`chess.hpp`) | MIT | High-performance C++20 bitboard chess move generation and rules |
| **`MinimaxAI`** | `include/chess/MinimaxAI.h` | Built-in in-process C++20 engine | MIT | Default out-of-the-box engine, runs natively on WebAssembly / Desktop |
| **[Stockfish](https://stockfishchess.org/)** *(optional)* | External binary | Subprocess execution via UCI pipes | GPLv3 | Optional high-tier engine for deep desktop analysis |

> **Note on Licensing:** `MinimaxAI` is 100% MIT. Stockfish is **not** linked into the WeirdChess binary; it runs as an independent external subprocess via standard UCI pipes, ensuring the WeirdChess codebase remains under the permissive MIT license.

---

## Threading & Execution Lifecycle

To guarantee 60+ FPS smooth rendering and immediate piece animation on player clicks, game execution is split across dedicated worker threads:

```
MAIN THREAD                    ASYNC ANNOTATOR THREAD           NARRATOR WORKER THREAD
-----------                    ----------------------           ----------------------
User selects square
        │
MoveSystem::applyMove()
  ├─ Update board state
  ├─ Start piece animation (0.18s)
  └─ Push job ───────────────▶ StockfishUCIAI::evaluate()
        │                              │
        │                       TacticDetector::analyze()
        │                              │
        │                       MoveClassifier::classify()
        │                              │
AnnotationSystem::update() ◀──── Publish MoveAnnotation
        │                                                     
        ├─ Push to Narrator ─────────────────────────────────▶ INarrator::narrate()
        │                                                                 │
NarrativeRenderSystem::update() ◀─── Drain StoryStream ──────────────────┘
        │
AISystem::update()
  └─ Gated until human animation & annotation finish
```

1. **Player Click:** The move is immediately validated and applied to the board. The visual animation starts on frame 0.
2. **Background Annotation:** `AsyncAnnotator` evaluates position differentials in centipawns and classifies the move without stalling the render thread.
3. **Story Streaming:** Once annotated, the worker thread generates narrative commentary and streams it to the screen-space story panel.
4. **AI Reply:** The AI turn begins once both the player's piece animation and annotation have concluded.

---

## Project Layout

```
weird-chess/
├── AGENTS.md                  # Instructions and conventions for AI assistants
├── CMakeLists.txt             # Project build configuration
├── LICENSE                    # MIT License
├── README.md                  # Project documentation
├── assets/                    # Game assets (scenes, fonts, textures)
├── bin/                       # Local binaries (e.g., bin/stockfish - gitignored)
├── docs/                      # Technical documentation
│   ├── adapters.md            # Guide on writing custom IChessAI engine adapters
│   └── strength.md            # Stockfish Elo and humanized tuning documentation
├── include/
│   ├── chess/                 # Chess rules, evaluation, UCI adapters, tactic detectors
│   │   ├── AnnotationWriter.h # Human-readable text generator
│   │   ├── AsyncAnnotator.h   # Multi-threaded move evaluation worker
│   │   ├── ChessLibBoard.h    # Canonical chess.hpp wrapper
│   │   ├── ChessTypes.h       # Move, Eval, MoveQuality, TacticInfo types
│   │   ├── IChessAI.h         # Abstract AI adapter interface
│   │   ├── MoveClassifier.h   # Centipawn delta classifier
│   │   ├── NullAI.h           # Fallback offline AI
│   │   ├── StockfishUCIAI.h   # Stockfish UCI process controller
│   │   └── TacticDetector.h   # Bitboard tactical detection (forks, pins, etc.)
│   ├── components/            # ECS component definitions (ChessState, Piece, Square)
│   ├── config.h               # Game configuration, board metrics, AI constants
│   ├── globals.h              # Engine global definitions
│   ├── narrator/              # Story narration interfaces & worker thread
│   │   ├── INarrator.h        # Narrator interface
│   │   ├── NarratorThread.h   # Thread-safe worker thread & queue
│   │   ├── PassThroughNarrator.h # Stage 1 verbatim annotation narrator
│   │   ├── StoryStream.h      # Thread-safe string stream buffer
│   │   └── llamacpp-integration.md # Stage 2 local LLM design doc
│   ├── scenes/                # Scene definitions (ChessScene)
│   ├── shapes/                # Piece & Board SDF shape builders, UI Button factory
│   └── systems/               # ECS Systems (input, move, animation, layout, AI, etc.)
├── src/
│   ├── chess/
│   │   └── StockfishUCIAI.cpp # Non-blocking SDL3 process UCI implementation
│   └── main.cpp               # Application entry point
├── tests/
│   └── chess_integration.cpp  # Headless integration test suite
└── third_party/
    └── chess/                 # Vendored header-only Disservin/chess-library
```

---

## Building and Running

### Prerequisites

- **C++20** compatible compiler (GCC 11+, Clang 13+, or MSVC 2022)
- **CMake 3.10+**
- **weird-engine** (expected at `../weird-engine/` by default or automatically fetched via Git)

### Build Steps

```bash
# 1. Configure the build directory
cmake -S . -B build

# 2. Build WeirdChess and the test suite
cmake --build build -j

# 3. Run the game
./build/WeirdChess
```

### Stockfish Engine Setup

While the game includes a built-in `NullAI` fallback, Stockfish provides full-strength evaluation and move classification:

1. Download the latest Stockfish binary for your OS from [official-stockfish/Stockfish Releases](https://github.com/official-stockfish/Stockfish/releases).
2. Place the executable at `bin/stockfish` (or specify an arbitrary path using the environment variable):
   ```bash
   export STOCKFISH_PATH=/usr/bin/stockfish
   ./build/WeirdChess
   ```

---

## Controls

| Action | Control |
|---|---|
| **Select Piece / Move** | Left Mouse Click |
| **Deselect Piece** | `Escape` or Right Mouse Click |
| **Pawn Promotion** | Press `Q` (Queen), `R` (Rook), `B` (Bishop), or `N` (Knight) / Keys `1`-`4` |
| **New Game** | Click the square button on the bottom right panel to reset the board |
| **Manual Opponent Override** | Click the circle toggle on the bottom right panel to take manual control of both sides |

---

## Configuration & Strength Tuning

Game tuning parameters are centralized in [`include/config.h`](file:///home/damaca/projects/weird-chess/include/config.h):

- **Board Dimensions:** `CELL = 15.0f` (120x120 world units).
- **Animation Speed:** `MOVE_ANIM_SECONDS = 0.18f`.
- **Casual Play Tuning:**
  - `DEFAULT_ELO = 1320` (minimum Stockfish Elo limiter rating).
  - `DEFAULT_SKILL = 0` (Stockfish skill level 0-20).
  - `AI_SEARCH_DEPTH = 1` (shallow ply search for quick, human-like play).
  - `AI_BLUNDER_CHANCE = 0.35f` (35% probability of occasional casual blunders).
- **Classification Thresholds:**
  - *Best*: Loss < 0.15 pawns
  - *Good*: Loss < 0.50 pawns
  - *Inaccuracy*: Loss < 1.00 pawns
  - *Mistake*: Loss < 2.50 pawns
  - *Blunder*: Loss >= 2.50 pawns

---

## Automated Tests & Debugging

Weird Chess contains an end-to-end integration test suite that tests rules, castling, en passant, promotion, tactical detectors, the Stockfish UCI handshake, and simulates a full AI-vs-AI game headlessly without requiring a window or GPU context:

```bash
# Run tests with the local Stockfish binary
./build/WeirdChessTests ./bin/stockfish
```

### Debug Options

- **Headless Autoplay:** Set `WEIRDCHESS_AUTOPLAY=1` to have the human side automatically make random legal moves:
  ```bash
  WEIRDCHESS_AUTOPLAY=1 ./build/WeirdChess
  ```
- **ImGui Overlay:** Real-time metrics, FEN output, turn indicators, and raw evaluation scores are displayed directly on the ImGui overlay.

---

## Roadmap (Stage 2: Local LLM)

Stage 1 establishes the annotation pipeline and worker thread boundaries. **Stage 2** replaces the pass-through story renderer with a local LLM via `llama.cpp`:

- A small quantized GGUF model (e.g. Qwen2.5-0.5B, Llama-3.2-1B, Phi-3-mini) generates an ongoing fictional serialized story.
- The story does **not** mention chess pieces directly; instead, tactical events on the board drive dramatic parallels in the narrative (a fork represents betrayal, a sacrifice represents a heroic gamble, a blunder triggers a sudden catastrophe).
- Story progression scales with the phase of the game (short games end abruptly, deep endgames build extensive prose).
- For details, see [`include/narrator/llamacpp-integration.md`](file:///home/damaca/projects/weird-chess/include/narrator/llamacpp-integration.md).

---

## Licenses

- **Weird Chess Source Code:** [MIT License](file:///home/damaca/projects/weird-chess/LICENSE)
- **chess-library (`chess.hpp`):** [MIT License](https://github.com/Disservin/chess-library) (Disservin)
- **weird-engine:** Licensed under its own repository terms.
- **Stockfish:** [GPLv3 License](https://github.com/official-stockfish/Stockfish) (Runs as an independent subprocess; not statically or dynamically linked).
