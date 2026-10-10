#pragma once

// Session scene state: the chess rules engine, the AI, the two worker
// threads (annotation + narrator) and the match flow. Runtime only: scene
// states are never serialized.

#include "chess/AsyncAnnotator.h"
#include "chess/ChessLibBoard.h"
#include "chess/ChessTypes.h"
#include "chess/IChessAI.h"
#include "narrator/INarrator.h"
#include "narrator/NarratorThread.h"
#include "weird-engine/Assert.h"
#include "weird-engine/ecs/Registry.h"

#include <memory>
#include <vector>

using WeirdEngine::Registry;

namespace wchess
{
	struct ChessSessionState
	{
		// ---- rules + AI ----
		std::shared_ptr<ChessLibBoard> board;
		std::shared_ptr<IChessAI> ai;
		std::shared_ptr<AsyncAnnotator> annotator; // background move annotation worker
		bool moveAppliedPendingAnnotation = false; // a move was applied, annotation still coming

		// ---- narrator (worker thread) ----
		std::shared_ptr<INarrator> narratorImpl;
		std::shared_ptr<NarratorThread> narrator;

		// ---- game flow ----
		bool playerIsWhite = true;
		bool aiThinking = false;
		float aiThinkingTimer = 0.0f;
		float aiThinkingDuration = 0.0f;
		bool gameOver = false;
		bool disableAI = false; // "DISABLE AI" toggle: take control of both sides
		bool awaitingPromotion = false;
		int promoFrom = -1;
		int promoTo = -1;

		// ---- last move + annotation ----
		MoveAnnotation lastAnnotation;
		bool hasLastAnnotation = false;
		// Last applied move, set synchronously in applyMove (the annotation
		// lags behind on the worker thread; the yellow highlight must not).
		Move lastMove;
		bool hasLastMove = false;
		std::vector<std::string> moveLog; // "12. e4" style lines

		// ---- move-driven sound effects ----
		bool lastMoveWasCapture = false;
		Color lastMoveMover = Color::White;
		bool lastMoveDeliveredCheck = false;
		bool checkmateJingleTriggered = false;
		float checkmateJingleTimer = -1.0f;
		int checkmateJingleStep = 0;

		// ---- background mood (smoothed intensity in [0,1]) ----
		float currentIntensity = 0.0f;
		float targetIntensity = 0.0f;
	};

	inline ChessSessionState& getSessionState(Registry& registry)
	{
		ChessSessionState* state = registry.getState<ChessSessionState>();
		WEIRD_ASSERT(state != nullptr, "ChessSessionState is missing: stateInitSystem must be the first start system");
		return *state;
	}
} // namespace wchess
