// Advances piece move animations (linear travel with ease-in ease-out,
// plus special parabolic hop for knights) each frame.

#include "components/ChessState.h"
#include "components/PieceComp.h"
#include "config.h"
#include "globals.h"
#include "shapes/PieceShapes.h"

#include <cmath>

namespace wchess
{
	namespace AnimationSystem
	{
		// Smooth cubic ease-in ease-out easing curve
		inline float easeInOutCubic(float t)
		{
			return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
		}

		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessState& state = getState(registry);
			if (state.animatingPieces.empty())
				return;

			float dt = services.time().deltaTime();

			for (size_t i = 0; i < state.animatingPieces.size(); ++i)
			{
				Entity piece = state.animatingPieces[i];
				if (piece == INVALID_ENTITY || !registry.hasComponent<Transform>(piece))
				{
					state.animT[i] = 1.0f; // mark finished so the compaction drops it
					continue;
				}

				float duration = state.animDuration[i];
				if (duration <= 0.0f)
					duration = ChessConfig::MOVE_ANIM_MIN_SECONDS;

				float t = std::min(1.0f, state.animT[i] + dt / duration);
				state.animT[i] = t;

				// Ease-in ease-out linear interpolation along travel path
				float e = easeInOutCubic(t);
				vec2 pos = glm::mix(state.animFrom[i], state.animTo[i], e);
				float scale = ChessConfig::PIECE_SCALE;

				bool isKnight = false;
				if (registry.hasComponent<PieceComp>(piece))
				{
					auto& pc = registry.getComponent<PieceComp>(piece);
					isKnight = (pc.type == PieceType::Knight);
					pc.animating = t < 1.0f;
					pc.animT = t;
					registry.setComponentDirty(pc);
				}

				// Knight special parabolic hop (up and down)
				if (isKnight)
				{
					float hopProgress = 4.0f * t * (1.0f - t); // peaks at 1.0 at t = 0.5
					float hopHeight = ChessConfig::CELL * 0.75f;
					pos.y += hopHeight * hopProgress;
					scale *= (1.0f + 0.25f * hopProgress);
				}

				PieceShapes::setPiecePositionAndScale(registry, piece, pos, scale);
			}

			// Drop finished animations and restore final resting scale/position.
			size_t alive = 0;
			for (size_t i = 0; i < state.animatingPieces.size(); ++i)
			{
				if (state.animT[i] >= 1.0f)
				{
					Entity piece = state.animatingPieces[i];
					if (piece != INVALID_ENTITY && registry.hasComponent<Transform>(piece))
						PieceShapes::setPiecePositionAndScale(registry, piece, state.animTo[i],
															  ChessConfig::PIECE_SCALE);
					continue;
				}
				state.animatingPieces[alive] = state.animatingPieces[i];
				state.animFrom[alive] = state.animFrom[i];
				state.animTo[alive] = state.animTo[i];
				state.animT[alive] = state.animT[i];
				state.animDuration[alive] = state.animDuration[i];
				++alive;
			}
			state.animatingPieces.resize(alive);
			state.animFrom.resize(alive);
			state.animTo.resize(alive);
			state.animT.resize(alive);
			state.animDuration.resize(alive);

			// When all piece animations finish, hide any captured pieces that were waiting to be removed
			if (state.animatingPieces.empty() && !state.capturedPiecesPendingRemoval.empty())
			{
				for (Entity captured : state.capturedPiecesPendingRemoval)
				{
					if (captured != INVALID_ENTITY)
						PieceShapes::destroyPiece(registry, captured);
				}
				state.capturedPiecesPendingRemoval.clear();
			}
		}
	} // namespace AnimationSystem
} // namespace wchess
