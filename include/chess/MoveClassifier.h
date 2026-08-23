#pragma once

// Turns engine evaluations + tactic flags into a chess.com-style move grade
// (Brilliant/Best/Great/Excellent/Good/Inaccuracy/Mistake/Blunder/Miss/
// Forced). Pure function; the caller owns the evaluation calls.

#include "chess/ChessTypes.h"
#include "config.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace wchess
{
	struct ClassificationInput
	{
		// Evaluations in centipawns, all from the *mover's* perspective.
		int evalBeforeCp = 0; // the position before the move
		int evalAfterCp = 0;  // the position after the move
		int bestMoveCp = 0;	  // evaluation of the best move in the before position
		int bestReplyCp = 0;  // evaluation after the best reply (used for sacrifices)

		float winChanceBefore = 0.5f;
		float winChanceAfter = 0.5f;
		float winChanceDelta = 0.0f;

		TacticInfo tactics;
		Move move;
		PieceType pieceMoved = PieceType::Pawn;
		int legalMoveCount = 0; // total legal moves in the before position
		int openingPly = 0;		// full-move number, for "book" heuristics
	};

	inline MoveQuality classify(const ClassificationInput& in)
	{
		const TacticInfo& t = in.tactics;

		const bool onlyLegal = in.legalMoveCount <= 1;
		if (onlyLegal)
			return MoveQuality::Forced;

		// How much better the best move was than what was played.
		int lossCp = std::max(0, in.bestMoveCp - in.evalAfterCp);
		// How much the move improved over the starting position.
		int gainCp = in.evalAfterCp - in.evalBeforeCp;

		const float lossPawns = static_cast<float>(lossCp) / 100.0f;
		const float gainPawns = static_cast<float>(gainCp) / 100.0f;
		const float winLoss = -in.winChanceDelta; // positive when win chance dropped

		const bool isBest = lossCp <= static_cast<int>(ChessConfig::BEST_LOSS_PAWNS * 100.0f) ||
							(in.winChanceAfter >= in.winChanceBefore && lossPawns <= 0.25f);

		// 1. Decided game protection:
		// When a player is already completely winning (win chance >= 95% or eval >= +6.00)
		// and plays a move that remains completely winning (win chance >= 90% or eval >= +5.00),
		// it is NOT a blunder, mistake, or miss just because Stockfish prefers a different mate distance.
		bool wasDecisivelyWinning = in.evalBeforeCp >= 600 || in.winChanceBefore >= 0.95f;
		bool remainsDecisivelyWinning = in.evalAfterCp >= 500 || in.winChanceAfter >= 0.90f;

		if (wasDecisivelyWinning && remainsDecisivelyWinning)
		{
			if (isBest || winLoss <= 0.02f || lossPawns <= 0.50f)
				return t.capture ? MoveQuality::Great : MoveQuality::Best;
			if (lossPawns <= 1.50f)
				return MoveQuality::Excellent;
			return MoveQuality::Good;
		}

		// 2. Brilliant move:
		// Must be the best move (or near-best) and involve a genuine material sacrifice
		// (non-pawn piece left hanging/en prise) that yields a winning/superior position.
		if (isBest && in.evalAfterCp >= 150 && gainPawns >= 0.0f)
		{
			bool pieceSacrifice = t.hangsPiece && in.pieceMoved != PieceType::Pawn;
			if (pieceSacrifice)
				return MoveQuality::Brilliant;
		}

		// 3. Miss: A winning advantage / tactic existed (eval was >= +2.00 or win chance >= 70%)
		// and the player failed to find it, significantly throwing away the win (win chance drop >= 20% or loss >= 2.0
		// pawns).
		if (!isBest && in.evalBeforeCp >= 200 && (lossPawns >= ChessConfig::MISS_WIN_PAWNS || winLoss >= 0.20f))
		{
			if (in.evalAfterCp < 200)
				return MoveQuality::Miss;
		}

		// 4. Best / Great Move
		if (isBest)
			return t.capture ? MoveQuality::Great : MoveQuality::Best;

		// 5. Categorize by loss and win probability swing
		if (lossPawns <= ChessConfig::EXCELLENT_LOSS_PAWNS && winLoss <= 0.05f)
			return MoveQuality::Excellent;

		if (lossPawns <= ChessConfig::GOOD_LOSS_PAWNS && winLoss <= 0.10f)
			return MoveQuality::Good;

		if (lossPawns <= ChessConfig::INACCURACY_LOSS_PAWNS && winLoss <= 0.20f)
			return MoveQuality::Inaccuracy;

		if (lossPawns <= ChessConfig::MISTAKE_LOSS_PAWNS && winLoss <= 0.35f)
			return MoveQuality::Mistake;

		return MoveQuality::Blunder;
	}
} // namespace wchess
