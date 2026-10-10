#pragma once

// Move application glue: turns a validated ChessMove into board + ECS updates,
// animations, highlights, annotations and game-over handling. Shared by the
// input system (player moves) and the AI system (enemy moves).

#include "components/BoardState.h"
#include "components/PieceComp.h"
#include "components/SessionState.h"
#include "components/StoryState.h"
#include "config.h"
#include "globals.h"
#include "shapes/BoardShapes.h"
#include "shapes/PieceShapes.h"
#include "systems/frictionRig.h"

namespace wchess
{
	namespace MoveSystem
	{
		// Synchronizes piece positions from the board state without creating or destroying any entities.
		inline void syncPieces(Registry& registry, ShapeService& shapes)
		{
			ChessSessionState& session = getSessionState(registry);
			ChessBoardState& board = getBoardState(registry);

			std::vector<bool> used(board.allPieceEntities.size(), false);
			std::vector<Entity> newBoardPieces(64, INVALID_ENTITY);

			// First pass: keep existing pieces on squares that haven't moved or changed
			for (int index = 0; index < 64; ++index)
			{
				Square square{index & 7, index >> 3};
				auto piece = session.board->pieceAt(square);
				if (!piece)
					continue;

				Entity existing = board.pieceEntities[index];
				if (existing != INVALID_ENTITY && registry.hasComponent<PieceComp>(existing))
				{
					auto& pc = registry.getComponent<PieceComp>(existing);
					if (pc.color == piece->first && pc.type == piece->second)
					{
						newBoardPieces[index] = existing;
						for (size_t k = 0; k < board.allPieceEntities.size(); ++k)
						{
							if (board.allPieceEntities[k] == existing)
							{
								used[k] = true;
								break;
							}
						}
					}
				}
			}

			// Second pass: assign unused matching entities from the pool to new/moved squares
			for (int index = 0; index < 64; ++index)
			{
				if (newBoardPieces[index] != INVALID_ENTITY)
					continue;

				Square square{index & 7, index >> 3};
				auto piece = session.board->pieceAt(square);
				if (!piece)
					continue;

				Entity chosen = INVALID_ENTITY;
				for (size_t k = 0; k < board.allPieceEntities.size(); ++k)
				{
					if (!used[k])
					{
						Entity e = board.allPieceEntities[k];
						if (registry.hasComponent<PieceComp>(e))
						{
							auto& pc = registry.getComponent<PieceComp>(e);
							if (pc.color == piece->first && pc.type == piece->second)
							{
								chosen = e;
								used[k] = true;
								break;
							}
						}
					}
				}

				// If no matching piece is available in the pool (e.g. pawn promotion to extra Queen),
				// spawn it on-demand at runtime.
				if (chosen == INVALID_ENTITY)
				{
					vec2 c = PieceShapes::squareCenterWorld(index);
					chosen = PieceShapes::spawnPiece(registry, shapes, piece->first, piece->second, c.x, c.y,
													 ChessConfig::PIECE_SCALE);
					auto& pc = registry.addComponent<PieceComp>(chosen);
					pc.color = piece->first;
					pc.type = piece->second;
					registry.setComponentDirty(pc);
					board.allPieceEntities.push_back(chosen);
					used.push_back(true);
				}
				else
				{
					vec2 c = PieceShapes::squareCenterWorld(index);
					PieceShapes::setPiecePosition(registry, chosen, c);
				}

				newBoardPieces[index] = chosen;
			}

			// Third pass: park any unused pieces (e.g. captured or unpromoted pieces) off-screen,
			// EXCEPT pieces pending capture removal (they will be hidden once the attacking animation lands).
			for (size_t k = 0; k < board.allPieceEntities.size(); ++k)
			{
				if (!used[k])
				{
					Entity e = board.allPieceEntities[k];

					bool isPendingCapture = false;
					for (Entity pending : board.capturedPiecesPendingRemoval)
					{
						if (pending == e)
						{
							isPendingCapture = true;
							break;
						}
					}

					if (!isPendingCapture)
					{
						PieceShapes::setPiecePosition(registry, e, vec2(-1000.0f, -1000.0f));
					}
				}
			}

			board.pieceEntities = std::move(newBoardPieces);
		}

		// Starts an eased animation for the piece moving from one square to
		// another (usually the just-moved piece) with duration proportional to distance.
		inline void animatePiece(Registry& registry, int fromIndex, int toIndex)
		{
			ChessBoardState& board = getBoardState(registry);
			Entity piece = board.pieceEntities[toIndex];
			if (piece == INVALID_ENTITY)
				return;

			vec2 from = PieceShapes::squareCenterWorld(fromIndex);
			vec2 to = PieceShapes::squareCenterWorld(toIndex);

			float dist = glm::length(to - from);
			float duration = std::max(ChessConfig::MOVE_ANIM_MIN_SECONDS, dist / ChessConfig::MOVE_ANIM_SPEED);

			PieceShapes::setPiecePosition(registry, piece, from);

			auto& pc = registry.getComponent<PieceComp>(piece);
			pc.animating = true;
			pc.fromPos = from;
			pc.toPos = to;
			pc.animT = 0.0f;
			pc.animDuration = duration;
			registry.setComponentDirty(pc);
		}

		// Clears selection + highlight overlays and repaints them from the
		// current state (selection, legal targets, last move, check).
		// Overlays have fixed materials, so no shader recompilation is needed.
		inline void refreshHighlights(Registry& registry, ServiceProvider& services)
		{
			ChessSessionState& session = getSessionState(registry);
			ChessBoardState& board = getBoardState(registry);

			// Selection highlight (green)
			BoardShapes::setHighlight(registry, board.selectionHighlight, board.selectedSquare);

			// Legal move targets (cyan pool of 28)
			for (size_t i = 0; i < board.highlightEntities.size(); ++i)
			{
				if (i < board.legalTargets.size())
					BoardShapes::setHighlight(registry, board.highlightEntities[i], board.legalTargets[i]);
				else
					BoardShapes::setHighlight(registry, board.highlightEntities[i], -1);
			}

			// Last move highlight (yellow) on both squares.
			if (session.hasLastMove)
			{
				int fromIdx = session.lastMove.from.index();
				int toIdx = session.lastMove.to.index();
				BoardShapes::setHighlight(registry, board.lastMoveFromHighlight, fromIdx);
				BoardShapes::setHighlight(registry, board.lastMoveToHighlight,
										  (toIdx != board.selectedSquare) ? toIdx : -1);
			}
			else
			{
				BoardShapes::setHighlight(registry, board.lastMoveFromHighlight, -1);
				BoardShapes::setHighlight(registry, board.lastMoveToHighlight, -1);
			}

			// King in check (red).
			if (!session.gameOver && session.board->inCheck())
			{
				Color kingColor = session.board->sideToMove();
				int kingIdx = session.board->kingSquare(kingColor).index();
				BoardShapes::setHighlight(registry, board.checkHighlight, kingIdx);
			}
			else
			{
				BoardShapes::setHighlight(registry, board.checkHighlight, -1);
			}
		}

		// Applies a validated move to the board + scene immediately (the piece
		// animates right away) and hands the heavy annotation work to the
		// background annotator. `mover` must equal the side to move. The
		// enemy reply is deferred until the annotation lands and the
		// animation finishes (see aiSystem).
		inline void applyMove(Registry& registry, ServiceProvider& services, const Move& move, Color mover)
		{
			ChessSessionState& session = getSessionState(registry);
			ChessBoardState& board = getBoardState(registry);

			// Clean up any previously pending captured pieces immediately
			for (Entity captured : board.capturedPiecesPendingRemoval)
			{
				if (captured != INVALID_ENTITY)
					PieceShapes::destroyPiece(registry, captured);
			}
			board.capturedPiecesPendingRemoval.clear();

			// Cancel any running lerp before the new move starts.
			PieceShapes::clearAllAnimations(registry);

			int fromIdx = move.from.index();
			int toIdx = move.to.index();

			// Detect if a piece is being captured BEFORE making the board move
			Entity capturedEntity = INVALID_ENTITY;
			if (move.isEnPassant)
			{
				int epSquare = move.from.rank * 8 + move.to.file;
				if (epSquare >= 0 && epSquare < 64)
					capturedEntity = board.pieceEntities[epSquare];
			}
			else
			{
				Entity targetPiece = board.pieceEntities[toIdx];
				if (targetPiece != INVALID_ENTITY && targetPiece != board.pieceEntities[fromIdx])
				{
					capturedEntity = targetPiece;
				}
			}

			if (capturedEntity != INVALID_ENTITY)
			{
				board.capturedPiecesPendingRemoval.push_back(capturedEntity);
			}

			bool isCapture = (capturedEntity != INVALID_ENTITY) || move.isCapture;
			session.lastMoveWasCapture = isCapture;
			session.lastMoveMover = mover;

			int legalBefore = static_cast<int>(session.board->legalMoves().size());
			std::string beforeFen = session.board->getFen();
			int fullMoveBefore = session.board->fullMoveNumber();
			if (!session.board->makeMove(move))
			{
				board.capturedPiecesPendingRemoval.clear();
				session.lastMoveWasCapture = false;
				session.lastMoveDeliveredCheck = false;
				return;
			}
			std::string afterFen = session.board->getFen();

			auto lastApplied = session.board->lastMove();
			Move fullMove = lastApplied.value_or(move);

			// Visuals first: update piece pool mirror and start the animation.
			syncPieces(registry, services.shapes());
			animatePiece(registry, fromIdx, toIdx);

			// If castling, also animate the accompanying rook
			if (fullMove.isCastling)
			{
				int rank = fullMove.from.rank;
				bool kingSide = fullMove.to.file > fullMove.from.file;
				int rookFrom = kingSide ? (rank * 8 + 7) : (rank * 8 + 0);
				int rookTo = kingSide ? (rank * 8 + 5) : (rank * 8 + 3);
				animatePiece(registry, rookFrom, rookTo);
			}

			bool hasPrev = session.hasLastMove;
			Move prevM = session.hasLastMove ? session.lastMove : Move{};

			board.selectedSquare = -1;
			board.legalTargets.clear();
			session.lastMove = fullMove;
			session.hasLastMove = true;
			refreshHighlights(registry, services);

			// Game over? (instant check, no evaluation needed)
			if (session.board->isGameOver())
			{
				session.gameOver = true;
				if (session.board->gameState() == GameState::Checkmate && !session.checkmateJingleTriggered)
				{
					session.checkmateJingleTriggered = true;
					session.checkmateJingleTimer = 0.0f;
					session.checkmateJingleStep = 0;
				}
				session.lastMoveDeliveredCheck = false;
			}
			else
			{
				session.lastMoveDeliveredCheck = session.board->inCheck();
			}

			// Queue the annotation on the worker thread (non-blocking).
			if (session.annotator)
			{
				AnnotationJob job;
				job.beforeFen = beforeFen;
				job.afterFen = afterFen;
				job.move = fullMove;
				job.mover = mover;
				job.hasPrevMove = hasPrev;
				job.prevMove = prevM;
				job.prevEvalValid = session.hasLastAnnotation;
				job.prevEval = session.lastAnnotation.evalAfterCp;
				job.legalMoveCount = legalBefore;
				job.fullMoveNumber = fullMoveBefore;
				job.isGameOver = session.board->isGameOver();
				job.gameState = session.board->gameState();
				if (session.annotator->submit(job))
					session.moveAppliedPendingAnnotation = true;
			}
		}

		// Full reset: back to the starting position, empty panel and log.
		inline void resetGame(Registry& registry, ServiceProvider& services)
		{
			ChessSessionState& session = getSessionState(registry);
			ChessBoardState& board = getBoardState(registry);
			ChessStoryState& story = getStoryState(registry);

			// Re-read config.json dynamically on reset (for updated premise, seed, etc.)
			ChessConfig::GameSettings gameSettings = ChessConfig::loadGameSettings();
			story.typewriterSpeed = gameSettings.typewriterSpeed;

			if (session.narratorImpl)
			{
				session.narratorImpl->setSeed(gameSettings.seed);
				session.narratorImpl->setPremise(gameSettings.premise);
			}

			WeirdEngine::Logger::log(
				"[WeirdChess] Resetting match. Config reloaded: seed=" +
				(gameSettings.seed >= 0 ? std::to_string(gameSettings.seed) : "random") +
				", premise=" + (gameSettings.premise.empty() ? "(ai-generated)" : "'" + gameSettings.premise + "'"));

			session.board->loadStartPosition();
			board.selectedSquare = -1;
			board.legalTargets.clear();
			session.gameOver = false;
			session.aiThinking = false;
			session.aiThinkingTimer = 0.0f;
			session.aiThinkingDuration = 0.0f;
			session.awaitingPromotion = false;
			session.promoFrom = -1;
			session.promoTo = -1;
			session.hasLastAnnotation = false;
			session.hasLastMove = false;
			session.moveAppliedPendingAnnotation = false; // drop any in-flight annotation
			PieceShapes::clearAllAnimations(registry);
			session.lastMoveWasCapture = false;
			session.lastMoveDeliveredCheck = false;
			session.checkmateJingleTriggered = false;
			session.checkmateJingleTimer = -1.0f;
			session.checkmateJingleStep = 0;
			FrictionRig::setLevel(registry, 0.0f);

			for (Entity captured : board.capturedPiecesPendingRemoval)
			{
				if (captured != INVALID_ENTITY)
					PieceShapes::destroyPiece(registry, captured);
			}
			board.capturedPiecesPendingRemoval.clear();

			if (session.narrator)
			{
				session.narrator->reset();
				session.narrator->stream()->clear();
				session.narrator->pushIntro();
			}
			story.rawStoryChunks.clear();
			story.formattedStoryLines.clear();
			story.storyRevealedChars = 0.0f;
			story.lastTotalStoryChars = 0;
			session.currentIntensity = 0.0f;
			session.targetIntensity = 0.0f;
			services.render().getBackground().intensity = 0.0f;
			story.layoutDirty = true;

			syncPieces(registry, services.shapes());
			refreshHighlights(registry, services);
		}
	} // namespace MoveSystem
} // namespace wchess
