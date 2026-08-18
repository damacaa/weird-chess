#pragma once

#include "weird-engine/ecs/Entity.h"
#include "weird-engine/ecs/Registry.h"
#include "weird-engine/vec.h"

using WeirdEngine::Entity;
using WeirdEngine::INVALID_ENTITY;
using WeirdEngine::Registry;
using WeirdEngine::vec2;

// Scene-wide game state as a single ECS component (one "state" entity,
// created by onCreateSystem). Holds the chess board, the AI, the narrator
// thread and every UI entity handle the systems need. All members are
// copyable so the ECS storage can reallocate freely (hence shared_ptr).

#include "chess/AsyncAnnotator.h"
#include "chess/ChessLibBoard.h"
#include "chess/IChessAI.h"
#include "config.h"
#include "narrator/INarrator.h"
#include "narrator/LlamaNarrator.h"
#include "narrator/NarratorThread.h"
#include "narrator/PassThroughNarrator.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace wchess
{
	struct SquareComp;
	struct PieceComp;

	struct ChessState
	{
		// ---- rules + AI ----
		std::shared_ptr<ChessLibBoard> board;
		std::shared_ptr<IChessAI> ai;
		std::shared_ptr<AsyncAnnotator> annotator; // background move annotation worker
		bool moveAppliedPendingAnnotation = false; // a move was applied, annotation still coming

		// ---- narrator (worker thread) ----
		std::shared_ptr<INarrator> narratorImpl;
		std::shared_ptr<NarratorThread> narrator;

		// ---- board entities ----
		std::vector<Entity> squareEntities;			   // [64]
		std::vector<Entity> highlightEntities;		   // [MAX_TARGET_HIGHLIGHTS = 28] legal targets (cyan)
		Entity selectionHighlight = INVALID_ENTITY;	   // selection (green)
		Entity lastMoveFromHighlight = INVALID_ENTITY; // last move from (yellow)
		Entity lastMoveToHighlight = INVALID_ENTITY;   // last move to (yellow)
		Entity checkHighlight = INVALID_ENTITY;		   // king in check (red)
		std::vector<Entity> allPieceEntities;		   // [32+] pool of piece entities (32 initial, promo on-demand)
		std::vector<Entity> pieceEntities;			   // [64] per square; INVALID_ENTITY if empty
		std::vector<Entity> rankLabels;				   // [8] world-space "1".."8"
		std::vector<Entity> fileLabels;				   // [8] world-space "a".."h"

		// ---- selection / interaction ----
		int selectedSquare = -1;
		std::vector<int> legalTargets; // square indices reachable from selection

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

		// ---- move animations ----
		std::vector<Entity> animatingPieces; // piece entities currently moving
		std::vector<vec2> animFrom;
		std::vector<vec2> animTo;
		std::vector<float> animT;
		std::vector<float> animDuration;
		std::vector<Entity> capturedPiecesPendingRemoval; // captured pieces hidden at end of move animation

		// ---- annotations ----
		MoveAnnotation lastAnnotation;
		bool hasLastAnnotation = false;
		// Last applied move, set synchronously in applyMove (the annotation
		// lags behind on the worker thread; the yellow highlight must not).
		Move lastMove;
		bool hasLastMove = false;
		std::vector<std::string> moveLog; // "12. e4" style lines shown above the board

		// ---- layout ----
		int lastResolutionHash = 0;
		bool layoutDirty = false;

		// ---- UI entity handles ----
		Entity titleText = INVALID_ENTITY;
		Entity statusText = INVALID_ENTITY;					  // "WHITE TO MOVE" / last annotation title
		Entity moveLogText = INVALID_ENTITY;				  // "12. e4  12... e5"
		std::vector<Entity> storyLines;						  // one UITextRenderer per line
		int storyVisibleLines = ChessConfig::STORY_MAX_LINES; // set by layoutSystem
		std::deque<std::string> storyText;					  // visible story lines (front = top)
		std::vector<std::string> rawStoryChunks;			  // full history of raw story paragraphs
		int lastWrapChars = 0;								  // last computed wrap width in characters
		float currentIntensity = 0.0f;						  // smoothly interpolated intensity in [0.0, 1.0]
		float targetIntensity = 0.0f;						  // target intensity driving background shader
		Entity newGameButton = INVALID_ENTITY;
		Entity disableAIToggle = INVALID_ENTITY;
		Entity promoCard = INVALID_ENTITY;
		Entity promoQueenButton = INVALID_ENTITY;
		Entity promoRookButton = INVALID_ENTITY;
		Entity promoBishopButton = INVALID_ENTITY;
		Entity promoKnightButton = INVALID_ENTITY;
		Entity storyTitle = INVALID_ENTITY;
		Entity storyStatus = INVALID_ENTITY;
	};

	// The state entity owns exactly one ChessState component.
	inline ChessState& getState(Registry& registry)
	{
		return registry.getComponentArray<ChessState>()->getDataAtIdx(0);
	}
} // namespace wchess
