#pragma once

// stateInit: creates the ChessState scene state (registered as the first
// start system).

#include "components/ChessState.h"
#include "globals.h"

namespace wchess
{
	inline void stateInitSystem(Registry& registry, ServiceProvider& services)
	{
		ChessState& state = registry.emplaceState<ChessState>();
		state.lastResolutionHash = 0;
		state.layoutDirty = true;
	}
} // namespace wchess
