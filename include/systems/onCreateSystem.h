#pragma once

// onCreate: creates the single "state" entity that owns the ChessState
// component, pauses physics (chess needs none) and stashes the scene start
// time.

#include "components/ChessState.h"
#include "globals.h"

namespace wchess
{
	inline void onCreateSystem(Registry& registry, ServiceProvider& services)
	{
		Entity stateEntity = registry.createEntity();
		registry.addComponent<ChessState>(stateEntity);
		services.tags().tag(stateEntity, "chess_state");
		services.serialization().blacklistEntity(stateEntity);


		ChessState& state = getState(registry);
		state.lastResolutionHash = 0;
		state.layoutDirty = true;
	}
} // namespace wchess
