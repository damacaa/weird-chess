#pragma once

#include "weird-engine/ecs/Entity.h"

using WeirdEngine::Entity;
using WeirdEngine::INVALID_ENTITY;

// Marks a board square entity: its file/rank and its piece entity.

#include <cstdint>

namespace wchess
{
	struct SquareComp
	{
		int file = 0;
		int rank = 0;
		int index = 0;				   // rank*8+file
		Entity piece = INVALID_ENTITY; // piece standing on this square
	};
} // namespace wchess
