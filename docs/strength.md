# Making the opponent human (Strength tuning)

The AI is tuned to provide a human-like, approachable opponent rather than an unbeatable engine that crushes casual players instantly.

## How it works

- **In-process MinimaxAI (Default):** Uses Alpha-Beta search with depth-limiting and probabilistic blunder generation (`blunder_chance`) to simulate human oversights at lower difficulties.
- **StockfishUCIAI (Optional):** Sends UCI options to constrain engine rating and search depth:
  - `UCI_LimitStrength true` - activates the Elo limiter
  - `UCI_Elo <elo>` - limits Stockfish rating (range 1320-3190)
  - `Skill Level <skill>` - Stockfish search-quality parameter (0-20)

## Difficulty Presets (`assets/config.json`)

Tuning is configured via `assets/config.json` through the `enemy` object:

```json
"enemy": {
  "difficulty": "easy",
  "skill_level": 0,
  "elo": 1320,
  "search_depth": 1,
  "blunder_chance": 0.35
}
```

| Difficulty Preset | Skill Level | Elo | Search Depth | Blunder Chance | Description |
|---|---|---|---|---|---|
| `easy` (default) | 0 | 1320 | 1 ply | 0.35 (35%) | Casual / beginner play with frequent mistakes |
| `medium` / `normal` | 5 | 1600 | 3 ply | 0.15 (15%) | Intermediate club player |
| `hard` | 12 | 2000 | 5 ply | 0.05 (5%) | Solid competitive club player |
| `master` | 20 | 2500 | 8 ply | 0.00 (0%) | Master strength without unforced blunders |

You can specify a preset name (e.g. `"difficulty": "medium"`) or set custom numeric values for `skill_level`, `elo`, `search_depth`, and `blunder_chance`.

## Effects on annotation quality

The annotation pipeline (blunder/mistake/inaccuracy detection) compares moves against the engine's evaluation of the *same position*. When the AI plays at reduced strength, its chosen moves are sometimes sub-optimal - the "best move" baseline used for centipawn loss comes from a separate evaluation call, so move classification stays accurate regardless of the AI's playing strength.

## Trade-offs

- Casual tuning makes the AI occasionally blunder material (hanging pieces), which is realistic for human opponents and provides material for the tactic detector and narrative story generator to highlight.
- A critical blunder by either player triggers dramatic narrative tension and can cause the story to conclude abruptly (`StoryStatus::EndedAbruptly`).
- If you prefer the AI to play near-perfectly (e.g. for testing the annotation pipeline or high-tier play), set difficulty to `master` or raise `elo` to 2500+.

