// Advances piece move animations (linear travel with ease-in ease-out,
// plus special parabolic hop for knights) each frame. Animation state lives
// on PieceComp, so this system just walks the piece components.

#include "components/BoardState.h"
#include "components/PieceComp.h"
#include "components/SessionState.h"
#include "config.h"
#include "globals.h"
#include "shapes/PieceShapes.h"
#include "systems/frictionRig.h"

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
		inline float computeGameIntensity(const ChessSessionState& session)
		{
			if (!session.board)
				return 0.0f;

			if (session.gameOver || session.board->isGameOver())
			{
				if (session.board->gameState() == GameState::Checkmate)
					return 1.0f;
				return 0.15f; // Draw/stalemate settles down
			}

			// 1. Position decisiveness / evaluation advantage (decisive positions drive high tension)
			float evalIntensity = 0.0f;
			if (session.hasLastAnnotation)
			{
				if (session.lastAnnotation.tactics.checkmate)
				{
					evalIntensity = 1.0f;
				}
				else
				{
					float cpScore = static_cast<float>(std::abs(session.lastAnnotation.evalAfterCp));
					float cpNorm = std::clamp(cpScore / 700.0f, 0.0f, 1.0f);
					// 99% win chance -> winDev = 0.98 -> evalIntensity reaches 0.98
					float winDev =
						std::clamp(std::abs(session.lastAnnotation.winChanceAfter - 0.5f) * 2.0f, 0.0f, 1.0f);
					evalIntensity = std::max(cpNorm, winDev);
				}
			}

			// 2. Endgame progression (moves + material traded)
			int fullMoves = session.board->fullMoveNumber();
			float moveProgress = std::clamp(static_cast<float>(fullMoves - 1) / 35.0f, 0.0f, 1.0f);

			int pieceCount = 0;
			for (int r = 0; r < 8; ++r)
			{
				for (int f = 0; f < 8; ++f)
				{
					if (session.board->pieceAt(Square{f, r}).has_value())
						++pieceCount;
				}
			}
			float materialProgress = std::clamp((32.0f - static_cast<float>(pieceCount)) / 26.0f, 0.0f, 1.0f);
			float gameProgress = 0.5f * (moveProgress + materialProgress);

			// 3. Immediate pressure: King in check (+0.25 bonus)
			float checkBonus = session.board->inCheck() ? 0.25f : 0.0f;

			// Decisive positions (e.g. 99% win chance, mate in 1) or checks directly drive high intensity
			float target = std::max(evalIntensity * 0.90f, gameProgress * 0.65f) + checkBonus;
			return std::clamp(target, 0.0f, 1.0f);
		}

		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessSessionState& session = getSessionState(registry);
			ChessBoardState& board = getBoardState(registry);
			float dt = services.time().deltaTime();

			// ---- 1. Match background intensity smoothing ----
			session.targetIntensity = computeGameIntensity(session);
			// Smooth, responsive transition without sudden phase jerks
			float lerpRate = 1.6f;
			session.currentIntensity =
				glm::mix(session.currentIntensity, session.targetIntensity, std::clamp(dt * lerpRate, 0.0f, 1.0f));
			services.render().getBackground().intensity = std::powf(session.currentIntensity, 2.2f);

			// ---- 2. Checkmate Jingle Sequencer (Chill & Spaced with Initial Pause) ----
			if (session.checkmateJingleTimer >= 0.0f)
			{
				session.checkmateJingleTimer += dt;
				// Initial pause of 0.40s before first note
				if (session.checkmateJingleStep == 0 && session.checkmateJingleTimer >= 0.40f)
				{
					services.audio().playSound(SimpleAudioRequest{0.55f, 261.63f, false, vec3(0.0f), 3}); // Warm C4
					session.checkmateJingleStep = 1;
				}
				if (session.checkmateJingleStep == 1 && session.checkmateJingleTimer >= 0.85f)
				{
					services.audio().playSound(SimpleAudioRequest{0.50f, 392.00f, false, vec3(0.0f), 3}); // Gentle G4
					session.checkmateJingleStep = 2;
				}
				if (session.checkmateJingleStep == 2 && session.checkmateJingleTimer >= 1.30f)
				{
					services.audio().playSound(SimpleAudioRequest{0.45f, 659.25f, false, vec3(0.0f), 4}); // Soft E5
					session.checkmateJingleStep = 3;
				}
				if (session.checkmateJingleStep == 3 && session.checkmateJingleTimer >= 1.75f)
				{
					services.audio().playSound(SimpleAudioRequest{0.40f, 523.25f, false, vec3(0.0f), 5}); // Peaceful C5
					session.checkmateJingleStep = 4;
				}
				if (session.checkmateJingleStep == 4 && session.checkmateJingleTimer >= 2.60f)
				{
					session.checkmateJingleTimer = -1.0f; // Jingle complete
				}
			}

			// ---- 3. Piece move animations & friction sound ----
			float totalFriction = 0.0f;
			bool anyActive = false;

			registry.forEach<PieceComp>(
				[&](Entity piece, PieceComp& pc)
				{
					if (!pc.animating)
						return;
					if (!registry.hasComponent<Transform>(piece))
					{
						pc.animating = false;
						return;
					}

					float duration = pc.animDuration;
					if (duration <= 0.0f)
						duration = ChessConfig::MOVE_ANIM_MIN_SECONDS;

					float t = std::min(1.0f, pc.animT + dt / duration);
					pc.animT = t;

					// Ease-in ease-out linear interpolation along travel path
					float e = easeInOutCubic(t);
					vec2 pos = glm::mix(pc.fromPos, pc.toPos, e);
					float scale = ChessConfig::PIECE_SCALE;

					bool isKnight = (pc.type == PieceType::Knight);

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

					if (t >= 1.0f)
					{
						// Land: restore final resting scale/position and stop.
						pc.animating = false;
						PieceShapes::setPiecePositionAndScale(registry, piece, pc.toPos, ChessConfig::PIECE_SCALE);
					}
					else
					{
						anyActive = true;
					}
				});

			float frictionLevel = std::clamp(totalFriction * 0.75f, 0.0f, 1.0f);
			FrictionRig::setLevel(registry, frictionLevel);

			// All piece animations finished this frame: reset friction, play the
			// landing sound and hide captured pieces that were waiting.
			if (!anyActive)
			{
				FrictionRig::setLevel(registry, 0.0f);

				if (session.lastMoveWasCapture)
				{
					if (session.lastMoveMover == Color::Black)
					{
						// Deeper, heavier capture sound for Black
						services.audio().playSound(SimpleAudioRequest{1.00f, 110.0f, false, vec3(0.0f), 4});
					}
					else
					{
						// Punchy piece capture sound for White
						services.audio().playSound(SimpleAudioRequest{0.95f, 196.0f, false, vec3(0.0f), 3});
					}
					session.lastMoveWasCapture = false;
					session.lastMoveDeliveredCheck = false;
				}
				else if (session.lastMoveDeliveredCheck)
				{
					// Tension alert tone for Check (D5)
					services.audio().playSound(SimpleAudioRequest{0.85f, 587.33f, false, vec3(0.0f), 2});
					session.lastMoveDeliveredCheck = false;
				}

				if (!board.capturedPiecesPendingRemoval.empty())
				{
					for (Entity captured : board.capturedPiecesPendingRemoval)
					{
						if (captured != INVALID_ENTITY)
							PieceShapes::destroyPiece(registry, captured);
					}
					board.capturedPiecesPendingRemoval.clear();
				}
			}
		}
	} // namespace AnimationSystem
} // namespace wchess
