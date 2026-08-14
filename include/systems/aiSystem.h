#pragma once

// Enemy AI: when it is the engine's turn, ask the IChessAI for the best move
// and apply it through the normal move pipeline (so enemy moves get the same
// annotation treatment as the player's). The reply is deferred until the
// player's piece animation has finished and the player's annotation has been
// published, so the player's move is always shown immediately.

#include "components/ChessState.h"
#include "globals.h"
#include "systems/moveSystem.h"

#include <random>

namespace wchess
{
	namespace AISystem
	{
		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessState& state = getState(registry);
			if (!state.board || !state.ai || state.gameOver || state.awaitingPromotion || state.disableAI)
			{
				state.aiThinking = false;
				state.aiThinkingTimer = 0.0f;
				return;
			}

			if (state.board->sideToMove() == (state.playerIsWhite ? Color::White : Color::Black))
			{
				state.aiThinking = false;
				state.aiThinkingTimer = 0.0f;
				return; // human's turn
			}

			// Wait for the annotation of the last move to be published and
			// for the piece animation to finish before starting to think.
			if (state.moveAppliedPendingAnnotation || !state.animatingPieces.empty())
				return;

			// Initialize randomized thinking duration (between 1.0s and 3.0s)
			if (!state.aiThinking)
			{
				state.aiThinking = true;
				state.aiThinkingTimer = 0.0f;
				static std::mt19937 rng{std::random_device{}()};
				std::uniform_real_distribution<float> dist(ChessConfig::AI_MIN_THINK_SECONDS,
														   ChessConfig::AI_MAX_THINK_SECONDS);
				state.aiThinkingDuration = dist(rng);
			}

			// Accumulate elapsed frame time
			float dt = services.time().deltaTime();
			state.aiThinkingTimer += dt;

			// Wait until the minimum thinking time has elapsed
			if (state.aiThinkingTimer < state.aiThinkingDuration)
				return;

			state.ai->setPosition(state.board->getFen());
			Move move = state.ai->bestMove(state.board->legalMoves());
			state.aiThinking = false;
			state.aiThinkingTimer = 0.0f;
			MoveSystem::applyMove(state, registry, services, move, state.playerIsWhite ? Color::Black : Color::White);
		}
	} // namespace AISystem
} // namespace wchess
