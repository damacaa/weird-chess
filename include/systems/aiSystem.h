#pragma once

// Enemy AI: when it is the engine's turn, ask the IChessAI for the best move
// and apply it through the normal move pipeline (so enemy moves get the same
// annotation treatment as the player's). The reply is deferred until the
// player's piece animation has finished and the player's annotation has been
// published, so the player's move is always shown immediately.

#include "components/SessionState.h"
#include "globals.h"
#include "shapes/PieceShapes.h"
#include "systems/moveSystem.h"

#include <random>

namespace wchess
{
	namespace AISystem
	{
		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessSessionState& session = getSessionState(registry);
			if (!session.board || !session.ai || session.gameOver || session.awaitingPromotion || session.disableAI)
			{
				session.aiThinking = false;
				session.aiThinkingTimer = 0.0f;
				return;
			}

			if (session.board->sideToMove() == (session.playerIsWhite ? Color::White : Color::Black))
			{
				session.aiThinking = false;
				session.aiThinkingTimer = 0.0f;
				return; // human's turn
			}

			// Wait for the annotation of the last move to be published and
			// for the piece animation to finish before starting to think.
			if (session.moveAppliedPendingAnnotation || PieceShapes::anyPieceAnimating(registry))
				return;

			// Initialize randomized thinking duration (between 1.0s and 3.0s)
			if (!session.aiThinking)
			{
				session.aiThinking = true;
				session.aiThinkingTimer = 0.0f;
				static std::mt19937 rng{std::random_device{}()};
				std::uniform_real_distribution<float> dist(ChessConfig::AI_MIN_THINK_SECONDS,
														   ChessConfig::AI_MAX_THINK_SECONDS);
				session.aiThinkingDuration = dist(rng);
			}

			// Accumulate elapsed frame time
			float dt = services.time().deltaTime();
			session.aiThinkingTimer += dt;

			// Wait until the minimum thinking time has elapsed
			if (session.aiThinkingTimer < session.aiThinkingDuration)
				return;

			session.ai->setPosition(session.board->getFen());
			Move move = session.ai->bestMove(session.board->legalMoves());
			session.aiThinking = false;
			session.aiThinkingTimer = 0.0f;
			MoveSystem::applyMove(registry, services, move, session.playerIsWhite ? Color::Black : Color::White);
		}
	} // namespace AISystem
} // namespace wchess
