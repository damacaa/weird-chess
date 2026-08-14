#pragma once

// Advances piece move animations (simple eased lerp) each frame.

#include "components/ChessState.h"
#include "components/PieceComp.h"
#include "config.h"
#include "globals.h"
#include "shapes/PieceShapes.h"

namespace wchess
{
	namespace AnimationSystem
	{
		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessState& state = getState(registry);
			if (state.animatingPieces.empty())
				return;

			float dt = services.time().deltaTime();
			const float duration = ChessConfig::MOVE_ANIM_SECONDS;

			for (size_t i = 0; i < state.animatingPieces.size(); ++i)
			{
				Entity piece = state.animatingPieces[i];
				// The piece may have been destroyed by a newer move's
				// syncPieces (defensive: applyMove clears the list, but a
				// stale entry must never touch a dead entity).
				if (piece == INVALID_ENTITY || !registry.hasComponent<Transform>(piece))
				{
					state.animT[i] = 1.0f; // mark finished so the compaction drops it
					continue;
				}

				float t = std::min(1.0f, state.animT[i] + dt / duration);
				state.animT[i] = t;

				// ease-out cubic
				float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
				vec2 pos = glm::mix(state.animFrom[i], state.animTo[i], e);
				PieceShapes::setPiecePosition(registry, piece, pos);

				if (registry.hasComponent<PieceComp>(piece))
				{
					auto& pc = registry.getComponent<PieceComp>(piece);
					pc.animating = t < 1.0f;
					pc.animT = t;
					registry.setComponentDirty(pc);
				}
			}

			// Drop finished animations.
			size_t alive = 0;
			for (size_t i = 0; i < state.animatingPieces.size(); ++i)
			{
				if (state.animT[i] >= 1.0f)
					continue;
				state.animatingPieces[alive] = state.animatingPieces[i];
				state.animFrom[alive] = state.animFrom[i];
				state.animTo[alive] = state.animTo[i];
				state.animT[alive] = state.animT[i];
				++alive;
			}
			state.animatingPieces.resize(alive);
			state.animFrom.resize(alive);
			state.animTo.resize(alive);
			state.animT.resize(alive);
		}
	} // namespace AnimationSystem
} // namespace wchess
