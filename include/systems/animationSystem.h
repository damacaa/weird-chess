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

		// Computes the dramatic intensity / closeness to game end in [0.0, 1.0]
		inline float computeGameIntensity(const ChessState& state)
		{
			if (!state.board)
				return 0.0f;

			if (state.gameOver || state.board->isGameOver())
			{
				if (state.board->gameState() == GameState::Checkmate)
					return 1.0f;
				return 0.15f; // Draw/stalemate settles down
			}

			// 1. Position decisiveness / evaluation advantage (decisive positions drive high tension)
			float evalIntensity = 0.0f;
			if (state.hasLastAnnotation)
			{
				if (state.lastAnnotation.tactics.checkmate)
				{
					evalIntensity = 1.0f;
				}
				else
				{
					float cpScore = static_cast<float>(std::abs(state.lastAnnotation.evalAfterCp));
					float cpNorm = std::clamp(cpScore / 700.0f, 0.0f, 1.0f);
					// 99% win chance -> winDev = 0.98 -> evalIntensity reaches 0.98
					float winDev = std::clamp(std::abs(state.lastAnnotation.winChanceAfter - 0.5f) * 2.0f, 0.0f, 1.0f);
					evalIntensity = std::max(cpNorm, winDev);
				}
			}

			// 2. Endgame progression (moves + material traded)
			int fullMoves = state.board->fullMoveNumber();
			float moveProgress = std::clamp(static_cast<float>(fullMoves - 1) / 35.0f, 0.0f, 1.0f);

			int pieceCount = 0;
			for (int r = 0; r < 8; ++r)
			{
				for (int f = 0; f < 8; ++f)
				{
					if (state.board->pieceAt(Square{f, r}).has_value())
						++pieceCount;
				}
			}
			float materialProgress = std::clamp((32.0f - static_cast<float>(pieceCount)) / 26.0f, 0.0f, 1.0f);
			float gameProgress = 0.5f * (moveProgress + materialProgress);

			// 3. Immediate pressure: King in check (+0.25 bonus)
			float checkBonus = state.board->inCheck() ? 0.25f : 0.0f;

			// Decisive positions (e.g. 99% win chance, mate in 1) or checks directly drive high intensity
			float target = std::max(evalIntensity * 0.90f, gameProgress * 0.65f) + checkBonus;
			return std::clamp(target, 0.0f, 1.0f);
		}

		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessState& state = getState(registry);
			float dt = services.time().deltaTime();

			// ---- 1. Match background intensity smoothing ----
			state.targetIntensity = computeGameIntensity(state);
			// Smooth, responsive transition without sudden phase jerks
			float lerpRate = 1.6f;
			state.currentIntensity =
				glm::mix(state.currentIntensity, state.targetIntensity, std::clamp(dt * lerpRate, 0.0f, 1.0f));
			services.render().getBackground().intensity = state.currentIntensity;

			// ---- 2. Checkmate Jingle Sequencer (Chill & Spaced with Initial Pause) ----
			if (state.checkmateJingleTimer >= 0.0f)
			{
				state.checkmateJingleTimer += dt;
				// Initial pause of 0.40s before first note
				if (state.checkmateJingleStep == 0 && state.checkmateJingleTimer >= 0.40f)
				{
					services.audio().playSound(
						WeirdRenderer::SimpleAudioRequest{0.55f, 261.63f, false, vec3(0.0f), 3}); // Warm C4
					state.checkmateJingleStep = 1;
				}
				if (state.checkmateJingleStep == 1 && state.checkmateJingleTimer >= 0.85f)
				{
					services.audio().playSound(
						WeirdRenderer::SimpleAudioRequest{0.50f, 392.00f, false, vec3(0.0f), 3}); // Gentle G4
					state.checkmateJingleStep = 2;
				}
				if (state.checkmateJingleStep == 2 && state.checkmateJingleTimer >= 1.30f)
				{
					services.audio().playSound(
						WeirdRenderer::SimpleAudioRequest{0.45f, 659.25f, false, vec3(0.0f), 4}); // Soft E5
					state.checkmateJingleStep = 3;
				}
				if (state.checkmateJingleStep == 3 && state.checkmateJingleTimer >= 1.75f)
				{
					services.audio().playSound(
						WeirdRenderer::SimpleAudioRequest{0.40f, 523.25f, false, vec3(0.0f), 5}); // Peaceful C5
					state.checkmateJingleStep = 4;
				}
				if (state.checkmateJingleStep == 4 && state.checkmateJingleTimer >= 2.60f)
				{
					state.checkmateJingleTimer = -1.0f; // Jingle complete
				}
			}

			// ---- 3. Piece move animations & friction sound ----
			if (state.animatingPieces.empty())
			{
				services.audio().setFrictionSound(0.0f);
				return;
			}

			float totalFriction = 0.0f;
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

				// Compute friction based on movement velocity derivative
				float speedFactor =
					(t < 0.5f) ? (4.0f * t * t * 3.0f) : (3.0f * (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f));
				speedFactor = std::clamp(speedFactor / 3.0f, 0.0f, 1.0f);
				float pieceFriction = speedFactor * 0.9f;

				if (isKnight)
				{
					// Knight friction is moderate at beginning (take-off) and end (landing), silent in mid-air
					if (t <= 0.20f)
					{
						float takeOffProgress = (0.20f - t) / 0.20f;
						pieceFriction = speedFactor * (1.2f + 0.4f * takeOffProgress);
					}
					else if (t >= 0.80f)
					{
						float landingProgress = (t - 0.80f) / 0.20f;
						pieceFriction = speedFactor * (1.2f + 0.5f * landingProgress);
					}
					else
					{
						pieceFriction = 0.0f; // Mid-air hop has zero board contact
					}
				}
				totalFriction += pieceFriction;
			}

			float frictionLevel = std::clamp(totalFriction * 0.75f, 0.0f, 1.0f);
			services.audio().setFrictionSound(frictionLevel);

			// Check if all piece animations are finishing this frame
			bool willBeEmpty = true;
			for (size_t i = 0; i < state.animatingPieces.size(); ++i)
			{
				if (state.animT[i] < 1.0f)
				{
					willBeEmpty = false;
					break;
				}
			}

			if (willBeEmpty)
			{
				services.audio().setFrictionSound(0.0f);

				if (state.lastMoveWasCapture)
				{
					if (state.lastMoveMover == Color::Black)
					{
						// Deeper, heavier capture sound for Black
						services.audio().playSound(
							WeirdRenderer::SimpleAudioRequest{1.00f, 110.0f, false, vec3(0.0f), 4});
					}
					else
					{
						// Punchy piece capture sound for White
						services.audio().playSound(
							WeirdRenderer::SimpleAudioRequest{0.95f, 196.0f, false, vec3(0.0f), 3});
					}
					state.lastMoveWasCapture = false;
					state.lastMoveDeliveredCheck = false;
				}
				else if (state.lastMoveDeliveredCheck)
				{
					// Tension alert tone for Check (D5)
					services.audio().playSound(
						WeirdRenderer::SimpleAudioRequest{0.85f, 587.33f, false, vec3(0.0f), 2});
					state.lastMoveDeliveredCheck = false;
				}
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
