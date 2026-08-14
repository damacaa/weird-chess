#pragma once

#include "weird-engine/ecs/Entity.h"

using WeirdEngine::Entity;
using WeirdEngine::INVALID_ENTITY;

// Marks a piece entity: its color/type and where it currently stands. The
// board (ChessLibBoard) is the source of truth; this mirrors it for
// rendering and animation.

#include "chess/ChessTypes.h"

namespace wchess
{
	struct PieceComp
	{
		Color color = Color::White;
		PieceType type = PieceType::Pawn;
		int squareIndex = 0; // 0-63 where the piece currently is

		bool animating = false;
		vec2 fromPos{0.0f, 0.0f};
		vec2 toPos{0.0f, 0.0f};
		float animT = 0.0f;
	};
} // namespace wchess
