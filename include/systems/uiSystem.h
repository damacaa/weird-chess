#pragma once

// Reads the ShapeButton / ShapeToggle components of the right-panel UI strip:
//   - "NEW GAME" button -> reset the game
//   - "STRONG AI" toggle -> switch the engine strength at runtime
// The engine's ButtonSystem already flips ShapeToggle::active and reports
// ShapeButton::state; this system only reacts to those changes.

#include "components/ChessState.h"
#include "config.h"
#include "globals.h"
#include "systems/moveSystem.h"

namespace wchess {
namespace UISystem {
inline void update(Registry &registry, ServiceProvider &services) {
  ChessState &state = getState(registry);
  if (!state.board)
    return;

  // New game.
  if (state.newGameButton != INVALID_ENTITY &&
      registry.getComponent<ShapeButton>(state.newGameButton).state ==
          ButtonState::Down) {
    MoveSystem::resetGame(state, registry, services);
  }

  // Disable AI toggle: when active, human controls both players' pieces.
  if (state.disableAIToggle != INVALID_ENTITY) {
    auto &toggle = registry.getComponent<ShapeToggle>(state.disableAIToggle);
    state.disableAI = toggle.active;
  }
}
} // namespace UISystem
} // namespace wchess
