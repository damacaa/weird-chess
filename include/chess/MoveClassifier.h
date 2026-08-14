#pragma once

// Turns engine evaluations + tactic flags into a chess.com-style move grade
// (Brilliant/Best/Great/Excellent/Good/Inaccuracy/Mistake/Blunder/Miss/
// Forced). Pure function; the caller owns the evaluation calls.

#include "chess/ChessTypes.h"
#include "config.h"

#include <vector>

namespace wchess
{
	struct ClassificationInput
	{
		// Evaluations in centipawns, all from the *mover's* perspective.
		int evalBeforeCp = 0;  // the position before the move
		int evalAfterCp = 0;   // the position after the move
		int bestMoveCp = 0;	// evaluation of the best move in the before position
		int bestReplyCp = 0;   // evaluation after the best reply (used for sacrifices)

		TacticInfo tactics;
		Move move;
		int legalMoveCount = 0; // total legal moves in the before position
		int openingPly = 0;		// full-move number, for "book" heuristics
	};

	inline MoveQuality classify(const ClassificationInput& in)
	{
		const TacticInfo& t = in.tactics;

		// Game-ending moves are always noteworthy.
		if (t.checkmate)
			return MoveQuality::Brilliant;

		const bool onlyLegal = in.legalMoveCount <= 1;

		// How much better the best move was than what was played.
		int lossCp = in.bestMoveCp - in.evalAfterCp;
		// How much the move improved over the starting position.
		int gainCp = in.evalAfterCp - in.evalBeforeCp;

		const float lossPawns = static_cast<float>(lossCp) / 100.0f;
		const float gainPawns = static_cast<float>(gainCp) / 100.0f;

		const bool isBest = lossCp <= static_cast<int>(ChessConfig::BEST_LOSS_PAWNS * 100.0f);

		if (onlyLegal)
			return MoveQuality::Forced;

		// A "brilliant" is a best move that sacrifices material for a strong
		// follow-up: the moved piece is en prise (hangs) or the move gives up
		// a higher-value piece, yet the eval still improved or stayed winning.
		if (isBest)
		{
			bool sac = t.hangsPiece && gainPawns >= 0.5f;
			bool materialSac = in.move.isCapture && (t.hangsPiece || gainPawns >= 1.0f);
			if ((sac || materialSac) && gainPawns >= 0.0f && in.evalAfterCp >= 50)
				return MoveQuality::Brilliant;
		}

		// A forced win was available and ignored.
		if (!isBest && in.bestMoveCp - in.evalBeforeCp >= static_cast<int>(ChessConfig::MISS_WIN_PAWNS * 100.0f))
			return MoveQuality::Miss;

		if (isBest)
			return t.capture ? MoveQuality::Great : MoveQuality::Best;

		if (lossPawns < ChessConfig::GOOD_LOSS_PAWNS)
			return MoveQuality::Excellent;

		if (lossPawns < ChessConfig::INACCURACY_LOSS_PAWNS)
			return MoveQuality::Good;

		if (lossPawns < ChessConfig::MISTAKE_LOSS_PAWNS)
			return MoveQuality::Inaccuracy;

		if (lossPawns < 5.0f)
			return MoveQuality::Mistake;

		return MoveQuality::Blunder;
	}
} // namespace wchess
