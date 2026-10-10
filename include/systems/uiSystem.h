#pragma once

// Reads the ShapeButton / ShapeToggle components of the right-panel UI strip:
//   - "NEW GAME" button -> reset the game
//   - "STRONG AI" toggle -> switch the engine strength at runtime
// The engine's ButtonSystem already flips ShapeToggle::active and reports
// ShapeButton::state; this system only reacts to those changes.

#include "components/SessionState.h"
#include "components/StoryState.h"
#include "config.h"
#include "globals.h"
#include "systems/moveSystem.h"

namespace wchess
{
	namespace UISystem
	{
		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessSessionState& session = getSessionState(registry);
			ChessStoryState& story = getStoryState(registry);
			if (!session.board)
				return;

			// New game.
			if (story.newGameButton != INVALID_ENTITY &&
				registry.getComponent<ShapeButton>(story.newGameButton).state == ButtonState::Down)
			{
				MoveSystem::resetGame(registry, services);
			}

			// Disable AI toggle: when active, human controls both players' pieces.
			if (story.disableAIToggle != INVALID_ENTITY)
			{
				auto& toggle = registry.getComponent<ShapeToggle>(story.disableAIToggle);
				session.disableAI = toggle.active;
			}
		}
	} // namespace UISystem
} // namespace wchess
