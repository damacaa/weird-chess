# Making the opponent human (Stockfish strength tuning)

The user asked for a "very human" opponent rather than an unbeatable engine.
Full-strength Stockfish (rating ~3500+) would crush a casual player, so the
AI is deliberately capped at club level.

## How it works

`StockfishUCIAI` sends two UCI options on startup:

- `UCI_LimitStrength true` - activates the Elo limiter
- `UCI_Elo <elo>` - Stockfish plays at roughly this rating (range 1320-3190)
- `Skill Level <skill>` - 0-20; the internal search-quality knob (ignored
  when `UCI_LimitStrength` is on, but sent anyway for engines that use it)

Default (config.h):

```
DEFAULT_SKILL = 12
DEFAULT_ELO   = 1700   // ~club player
```

## Tuning

| Elo  | Rough description |
|------|-------------------|
| 1320 | Casual / beginner mistakes |
| 1500 | Intermediate club player |
| 1700 | Solid club player (default) |
| 2000 | Strong tournament player |
| 2500 | Master - only blunders rarely |
| 3190 | Nearly full strength |

Change the defaults in `include/config.h`, or at runtime:

- Press the **STRONG AI** toggle in the bottom-right of the panel to jump to
  `setStrength(19, 2500)` and back to the default.

## Effects on annotation quality

The annotation pipeline (blunder/mistake/inaccuracy detection) compares moves
against the engine's eval of the *same position*. When the AI plays at
reduced strength, its chosen moves are sometimes sub-optimal - the
"best move" baseline used for `lossCp` comes from a separate
`evaluate()` call, so move classification stays accurate regardless of the
AI's playing strength.

## Trade-offs

- `UCI_LimitStrength` makes Stockfish occasionally blunder *material*
  (hanging pieces), which is exactly what a human opponent does and what the
  tactic detector is designed to catch - a blunder by the AI produces a
  "BLUNDER" annotation and (stage 2) an abrupt story ending, just like the
  player's blunders do.
- If you prefer the AI to play near-perfectly but still human-ish (e.g. for
  testing the annotation pipeline), raise `DEFAULT_ELO` to 2500+.
