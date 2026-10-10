#pragma once

#include "weird-engine/ecs/Entity.h"

using WeirdEngine::Entity;
using WeirdEngine::INVALID_ENTITY;

// Marks a board square entity and carries its file/rank. The piece standing
// on a square is looked up through ChessBoardState::pieceEntities (the board
// is a fixed grid, so indexed arrays stay the single lookup).

#include <cstdint>

namespace wchess
{
	struct SquareComp
	{
		int file = 0;
		int rank = 0;
		int index = 0; // rank*8+file
	};
} // namespace wchess
