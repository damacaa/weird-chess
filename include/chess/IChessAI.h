#pragma once

// The AI/evaluation seam. The game only talks to chess through this interface
// (plus the ChessLibBoard class which owns the rules). To swap in a different
// engine - Stockfish, a bundled binary, or your own engine later - write a new
// IChessAI implementation and construct it where the scene builds its state.
// See chess/adapters.md for the how-to.

#include "chess/ChessTypes.h"

#include <string>
#include <vector>

namespace wchess
{
	class IChessAI
	{
	public:
		virtual ~IChessAI() = default;

		// Whether the backing engine is actually usable (e.g. the Stockfish
		// binary was found). The game should fall back to NullAI otherwise.
		virtual bool isAvailable() const = 0;

		virtual std::string name() const = 0;

		// skill: 0-20 (engine-internal knob), elo: 1320-3190 (UCI_LimitStrength).
		// Implementations may ignore either.
		virtual void setStrength(int skill, int elo) = 0;

		// Tell the engine the position to search from. FEN uses standard
		// notation; implementations keep their own copy of the last position.
		virtual void setPosition(const std::string& fen) = 0;

		// The engine's choice of move for the current position. `legalMoves`
		// are the legal moves computed by the board rules layer; the engine
		// result is validated against them and a random legal fallback is
		// returned if it cannot be matched.
		virtual Move bestMove(const std::vector<Move>& legalMoves) = 0;

		// Static evaluation of `fen` (centipawns / mate), thinking up to
		// movetimeMs. Eval is from the side-to-move's perspective.
		virtual Eval evaluate(const std::string& fen, int movetimeMs) = 0;

		// Release the engine (terminate the subprocess, free resources).
		virtual void shutdown() = 0;
	};
} // namespace wchess
