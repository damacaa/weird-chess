#pragma once

// Enemy AI: when it is the engine's turn, ask the IChessAI for the best move
// and apply it through the normal move pipeline (so enemy moves get the same
// annotation treatment as the player's). The reply is deferred until the
// player's piece animation has finished and the player's annotation has been
// published, so the player's move is always shown immediately.

#include "components/ChessState.h"
#include "globals.h"
#include "systems/moveSystem.h"

namespace wchess {
namespace AISystem {
inline void update(Registry &registry, ServiceProvider &services) {
  ChessState &state = getState(registry);
  if (!state.board || !state.ai || state.gameOver || state.awaitingPromotion ||
      state.disableAI)
    return;

  if (state.board->sideToMove() ==
      (state.playerIsWhite ? Color::White : Color::Black))
    return; // human's turn

  if (state.aiThinking)
    return;

  // Wait for the annotation of the last move to be published and
  // for the piece animation to finish before replying.
  if (state.moveAppliedPendingAnnotation || !state.animatingPieces.empty())
    return;

  state.aiThinking = true;
  state.ai->setPosition(state.board->getFen());
  Move move = state.ai->bestMove(state.board->legalMoves());
  state.aiThinking = false;
  MoveSystem::applyMove(state, registry, services, move,
                        state.playerIsWhite ? Color::Black : Color::White);
}
} // namespace AISystem
} // namespace wchess
