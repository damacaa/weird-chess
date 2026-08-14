# Swapping the chess engine / AI (documented extension point)

All chess access goes through two seams. Swapping in a different engine (or
your own) means implementing one of these interfaces - nothing else changes.

## 1. Rules & board: `ChessLibBoard` (include/chess/ChessLibBoard.h)

The canonical game state. Built on the vendored `chess.hpp` (Disservin's
chess-library, MIT, `third_party/chess/chess.hpp`). It owns:

- FEN load / export
- legal move generation and validation
- move application + undo (with history)
- check / game-over detection
- piece lookup, king square

The whole game treats this as the single source of truth. The ECS entities
are only a *visual mirror* (`MoveSystem::syncPieces` rebuilds them from the
board after every move).

**Swapping the rules engine** (e.g. to handle variants or a home-grown board):
rewrite the internals of `ChessLibBoard` behind the same public API. The
tactic detector also reads the raw `chess::Board` (`ChessLibBoard::raw()`) -
that part is the one coupling to chess.hpp; it is isolated in
`TacticDetector.h` and can be rewritten independently.

## 2. AI & evaluation: `IChessAI` (include/chess/IChessAI.h)

```
isAvailable()  name()  setStrength(skill, elo)
setPosition(fen)  bestMove(legalMoves)  evaluate(fen, movetimeMs)  shutdown()
```

Implemented by:

| Class | File | Notes |
|---|---|---|
| `StockfishUCIAI` | include/chess/StockfishUCIAI.h + src/chess/StockfishUCIAI.cpp | UCI subprocess via SDL3 `SDL_CreateProcess`. Stockfish is NOT linked (GPLv3); it runs as an external process so this project's source stays MIT. |
| `NullAI` | include/chess/NullAI.h | Built-in fallback: random legal moves + material-count eval. Used when no Stockfish binary is found so the game always runs. |

### Adding your own engine

1. Create `include/chess/MyEngineAI.h` implementing `wchess::IChessAI`.
2. Construct it in `systems/onStartBoardSystem.h` where `state.ai` is set
   (currently: try Stockfish, fall back to NullAI).
3. The annotation pipeline (`systems/annotationSystem.h`) and the AI system
   (`systems/aiSystem.h`) need no changes - they only see `IChessAI`.

### Stockfish placement

The binary is looked up at startup, in order:

1. `$STOCKFISH_PATH` environment variable (full path to the binary)
2. `bin/stockfish` (relative to the working directory)
3. `../bin/stockfish` (when running from `build/`)

Download a release from https://github.com/official-stockfish/Stockfish/releases
and put the executable at `bin/stockfish`. The binary is gitignored.

### Threading notes

- `bestMove`/`evaluate` are blocking calls on the main thread (about
  200-600 ms each; annotation evals are 2x 300 ms per move). Fine for stage 1.
- If this becomes a problem: run the evals on a worker thread and gate the
  input system on a "annotation pending" flag. The narrator thread already
  demonstrates the pattern to copy.
