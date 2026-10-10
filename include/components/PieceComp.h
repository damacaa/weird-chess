#pragma once

#include "weird-engine/ecs/Entity.h"
#include "weird-engine/vec.h"

using WeirdEngine::Entity;
using WeirdEngine::INVALID_ENTITY;
using WeirdEngine::vec2;

// Marks a piece entity: its color/type and its live move animation. The board
// (ChessLibBoard) is the source of truth; this mirrors it for rendering and
// animation. AnimationSystem drives `animating` pieces and clears the flag
// when the lerp lands, so no per-frame bookkeeping lives in the scene state.

#include "chess/ChessTypes.h"

namespace wchess
{
	struct PieceComp
	{
		Color color = Color::White;
		PieceType type = PieceType::Pawn;

		// ---- move animation (owned by AnimationSystem) ----
		bool animating = false;
		vec2 fromPos{0.0f, 0.0f};
		vec2 toPos{0.0f, 0.0f};
		float animT = 0.0f;
		float animDuration = 0.0f;
	};
} // namespace wchess
