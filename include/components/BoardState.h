#pragma once

// Board scene state: the visual mirror of the chess board. Holds the entity
// handle pools (squares, pieces, highlight overlays), the current selection
// and the off-screen friction audio rig. Per-piece animation lives on
// PieceComp; this state only owns the entity handles.

#include "weird-engine/Assert.h"
#include "weird-engine/ecs/Entity.h"
#include "weird-engine/ecs/Registry.h"

#include <vector>

using WeirdEngine::Entity;
using WeirdEngine::INVALID_ENTITY;
using WeirdEngine::Registry;

namespace wchess
{
	struct ChessBoardState
	{
		// ---- board entities ----
		std::vector<Entity> squareEntities;			// [64]
		std::vector<Entity> highlightEntities;		// [MAX_TARGET_HIGHLIGHTS] legal targets (cyan)
		Entity selectionHighlight = INVALID_ENTITY; // selection (green)
		Entity lastMoveFromHighlight = INVALID_ENTITY;
		Entity lastMoveToHighlight = INVALID_ENTITY; // last move (yellow)
		Entity checkHighlight = INVALID_ENTITY;		 // king in check (red)
		std::vector<Entity> allPieceEntities;		 // [32+] pool of piece entities (32 initial, promo on-demand)
		std::vector<Entity> pieceEntities;			 // [64] per square; INVALID_ENTITY if empty

		// ---- selection / interaction ----
		int selectedSquare = -1;
		std::vector<int> legalTargets; // square indices reachable from selection

		// ---- captures waiting for the move animation to land ----
		std::vector<Entity> capturedPiecesPendingRemoval;

		// ---- off-screen friction audio proxy (see systems/frictionRig.h) ----
		Entity frictionBall = INVALID_ENTITY;
		Entity frictionRail = INVALID_ENTITY;
	};

	inline ChessBoardState& getBoardState(Registry& registry)
	{
		ChessBoardState* state = registry.getState<ChessBoardState>();
		WEIRD_ASSERT(state != nullptr, "ChessBoardState is missing: stateInitSystem must be the first start system");
		return *state;
	}
} // namespace wchess
