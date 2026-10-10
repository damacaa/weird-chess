#pragma once

// stateInit: creates the scene states (session, board, story). Registered as
// the first start system so it runs before every other system.

#include "components/BoardState.h"
#include "components/SessionState.h"
#include "components/StoryState.h"
#include "globals.h"

namespace wchess
{
	inline void stateInitSystem(Registry& registry, ServiceProvider& services)
	{
		registry.emplaceState<ChessSessionState>();
		registry.emplaceState<ChessBoardState>();
		ChessStoryState& story = registry.emplaceState<ChessStoryState>();
		story.lastResolutionHash = 0;
		story.layoutDirty = true;
	}
} // namespace wchess
