#pragma once

// Move application glue: turns a validated ChessMove into board + ECS updates,
// animations, highlights, annotations and game-over handling. Shared by the
// input system (player moves) and the AI system (enemy moves).

#include "components/ChessState.h"
#include "components/PieceComp.h"
#include "components/SquareComp.h"
#include "config.h"
#include "globals.h"
#include "shapes/BoardShapes.h"
#include "shapes/PieceShapes.h"

namespace wchess
{
	namespace MoveSystem
	{
		// Synchronizes piece positions from the board state without creating or destroying any entities.
		inline void syncPieces(ChessState& state, Registry& registry, ShapeService& shapes)
		{
			std::vector<bool> used(state.allPieceEntities.size(), false);
			std::vector<Entity> newBoardPieces(64, INVALID_ENTITY);

			// First pass: keep existing pieces on squares that haven't moved or changed
			for (int index = 0; index < 64; ++index)
			{
				Square square{index & 7, index >> 3};
				auto piece = state.board->pieceAt(square);
				if (!piece)
					continue;

				Entity existing = state.pieceEntities[index];
				if (existing != INVALID_ENTITY && registry.hasComponent<PieceComp>(existing))
				{
					auto& pc = registry.getComponent<PieceComp>(existing);
					if (pc.color == piece->first && pc.type == piece->second)
					{
						newBoardPieces[index] = existing;
						for (size_t k = 0; k < state.allPieceEntities.size(); ++k)
						{
							if (state.allPieceEntities[k] == existing)
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
				auto piece = state.board->pieceAt(square);
				if (!piece)
					continue;

				Entity chosen = INVALID_ENTITY;
				for (size_t k = 0; k < state.allPieceEntities.size(); ++k)
				{
					if (!used[k])
					{
						Entity e = state.allPieceEntities[k];
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
					pc.squareIndex = index;
					registry.setComponentDirty(pc);
					state.allPieceEntities.push_back(chosen);
					used.push_back(true);
				}
				else
				{
					auto& pc = registry.getComponent<PieceComp>(chosen);
					pc.squareIndex = index;
					registry.setComponentDirty(pc);
					vec2 c = PieceShapes::squareCenterWorld(index);
					PieceShapes::setPiecePosition(registry, chosen, c);
				}

				newBoardPieces[index] = chosen;
			}

			// Third pass: park any unused pieces (e.g. captured or unpromoted pieces) off-screen,
			// EXCEPT pieces pending capture removal (they will be hidden once the attacking animation lands).
			for (size_t k = 0; k < state.allPieceEntities.size(); ++k)
			{
				if (!used[k])
				{
					Entity e = state.allPieceEntities[k];
					if (registry.hasComponent<PieceComp>(e))
					{
						auto& pc = registry.getComponent<PieceComp>(e);
						pc.squareIndex = -1;
						registry.setComponentDirty(pc);
					}

					bool isPendingCapture = false;
					for (Entity pending : state.capturedPiecesPendingRemoval)
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

			state.pieceEntities = std::move(newBoardPieces);

			for (int index = 0; index < 64; ++index)
			{
				if (registry.hasComponent<SquareComp>(state.squareEntities[index]))
					registry.getComponent<SquareComp>(state.squareEntities[index]).piece = state.pieceEntities[index];
			}
		}

		// Starts an eased animation for the piece moving from one square to
		// another (usually the just-moved piece).
		inline void animatePiece(ChessState& state, Registry& registry, int fromIndex, int toIndex)
		{
			Entity piece = state.pieceEntities[toIndex];
			if (piece == INVALID_ENTITY)
				return;

			vec2 from = PieceShapes::squareCenterWorld(fromIndex);
			vec2 to = PieceShapes::squareCenterWorld(toIndex);

			PieceShapes::setPiecePosition(registry, piece, from);

			auto& pc = registry.getComponent<PieceComp>(piece);
			pc.animating = true;
			pc.fromPos = from;
			pc.toPos = to;
			pc.animT = 0.0f;
			registry.setComponentDirty(pc);

			state.animatingPieces.push_back(piece);
			state.animFrom.push_back(from);
			state.animTo.push_back(to);
			state.animT.push_back(0.0f);
		}

		// Clears selection + highlight overlays and repaints them from the
		// current state (selection, legal targets, last move, check).
		// Overlays have fixed materials, so no shader recompilation is needed.
		inline void refreshHighlights(ChessState& state, Registry& registry, ServiceProvider& services)
		{
			// Selection highlight (green)
			BoardShapes::setHighlight(registry, state.selectionHighlight, state.selectedSquare);

			// Legal move targets (cyan pool of 28)
			for (size_t i = 0; i < state.highlightEntities.size(); ++i)
			{
				if (i < state.legalTargets.size())
					BoardShapes::setHighlight(registry, state.highlightEntities[i], state.legalTargets[i]);
				else
					BoardShapes::setHighlight(registry, state.highlightEntities[i], -1);
			}

			// Last move highlight (yellow) on both squares.
			if (state.hasLastMove)
			{
				int fromIdx = state.lastMove.from.index();
				int toIdx = state.lastMove.to.index();
				BoardShapes::setHighlight(registry, state.lastMoveFromHighlight, fromIdx);
				BoardShapes::setHighlight(registry, state.lastMoveToHighlight,
										  (toIdx != state.selectedSquare) ? toIdx : -1);
			}
			else
			{
				BoardShapes::setHighlight(registry, state.lastMoveFromHighlight, -1);
				BoardShapes::setHighlight(registry, state.lastMoveToHighlight, -1);
			}

			// King in check (red).
			if (!state.gameOver && state.board->inCheck())
			{
				Color kingColor = state.board->sideToMove();
				int kingIdx = state.board->kingSquare(kingColor).index();
				BoardShapes::setHighlight(registry, state.checkHighlight, kingIdx);
			}
			else
			{
				BoardShapes::setHighlight(registry, state.checkHighlight, -1);
			}
		}

		// Applies a validated move to the board + scene immediately (the piece
		// animates right away) and hands the heavy annotation work to the
		// background annotator. `mover` must equal the side to move. The
		// enemy reply is deferred until the annotation lands and the
		// animation finishes (see aiSystem).
		inline void applyMove(ChessState& state, Registry& registry, ServiceProvider& services, const Move& move,
							  Color mover)
		{
			// Clean up any previously pending captured pieces immediately
			for (Entity captured : state.capturedPiecesPendingRemoval)
			{
				if (captured != INVALID_ENTITY)
					PieceShapes::destroyPiece(registry, captured);
			}
			state.capturedPiecesPendingRemoval.clear();

			state.animatingPieces.clear();
			state.animFrom.clear();
			state.animTo.clear();
			state.animT.clear();

			int fromIdx = move.from.index();
			int toIdx = move.to.index();

			// Detect if a piece is being captured BEFORE making the board move
			Entity capturedEntity = INVALID_ENTITY;
			if (move.isEnPassant)
			{
				int epSquare = move.from.rank * 8 + move.to.file;
				if (epSquare >= 0 && epSquare < 64)
					capturedEntity = state.pieceEntities[epSquare];
			}
			else
			{
				Entity targetPiece = state.pieceEntities[toIdx];
				if (targetPiece != INVALID_ENTITY && targetPiece != state.pieceEntities[fromIdx])
				{
					capturedEntity = targetPiece;
				}
			}

			if (capturedEntity != INVALID_ENTITY)
			{
				state.capturedPiecesPendingRemoval.push_back(capturedEntity);
			}

			int legalBefore = static_cast<int>(state.board->legalMoves().size());
			std::string beforeFen = state.board->getFen();
			if (!state.board->makeMove(move))
			{
				state.capturedPiecesPendingRemoval.clear();
				return;
			}
			std::string afterFen = state.board->getFen();

			auto lastApplied = state.board->lastMove();
			Move fullMove = lastApplied.value_or(move);

			// Visuals first: update piece pool mirror and start the animation.
			syncPieces(state, registry, services.shapes());
			animatePiece(state, registry, fromIdx, toIdx);

			// If castling, also animate the accompanying rook
			if (fullMove.isCastling)
			{
				int rank = fullMove.from.rank;
				bool kingSide = fullMove.to.file > fullMove.from.file;
				int rookFrom = kingSide ? (rank * 8 + 7) : (rank * 8 + 0);
				int rookTo = kingSide ? (rank * 8 + 5) : (rank * 8 + 3);
				animatePiece(state, registry, rookFrom, rookTo);
			}

			bool hasPrev = state.hasLastMove;
			Move prevM = state.hasLastMove ? state.lastMove : Move{};

			state.selectedSquare = -1;
			state.legalTargets.clear();
			state.lastMove = fullMove;
			state.hasLastMove = true;
			refreshHighlights(state, registry, services);

			// Game over? (instant check, no evaluation needed)
			if (state.board->isGameOver())
				state.gameOver = true;

			// Queue the annotation on the worker thread (non-blocking).
			if (state.annotator)
			{
				AnnotationJob job;
				job.beforeFen = beforeFen;
				job.afterFen = afterFen;
				job.move = fullMove;
				job.mover = mover;
				job.hasPrevMove = hasPrev;
				job.prevMove = prevM;
				job.prevEvalValid = state.hasLastAnnotation;
				job.prevEval = state.lastAnnotation.evalAfterCp;
				job.legalMoveCount = legalBefore;
				job.fullMoveNumber = state.board->fullMoveNumber();
				job.isGameOver = state.board->isGameOver();
				job.gameState = state.board->gameState();
				if (state.annotator->submit(job))
					state.moveAppliedPendingAnnotation = true;
			}
		}

		// Full reset: back to the starting position, empty panel and log.
		inline void resetGame(ChessState& state, Registry& registry, ServiceProvider& services)
		{
			state.board->loadStartPosition();
			state.selectedSquare = -1;
			state.legalTargets.clear();
			state.gameOver = false;
			state.aiThinking = false;
			state.aiThinkingTimer = 0.0f;
			state.aiThinkingDuration = 0.0f;
			state.awaitingPromotion = false;
			state.promoFrom = -1;
			state.promoTo = -1;
			state.hasLastAnnotation = false;
			state.hasLastMove = false;
			state.moveAppliedPendingAnnotation = false; // drop any in-flight annotation
			state.moveLog.clear();
			state.animatingPieces.clear();
			state.animFrom.clear();
			state.animTo.clear();
			state.animT.clear();

			for (Entity captured : state.capturedPiecesPendingRemoval)
			{
				if (captured != INVALID_ENTITY)
					PieceShapes::destroyPiece(registry, captured);
			}
			state.capturedPiecesPendingRemoval.clear();

			if (state.narrator)
			{
				state.narrator->stream()->clear();
			}

			syncPieces(state, registry, services.shapes());
			refreshHighlights(state, registry, services);
		}
	} // namespace MoveSystem
} // namespace wchess
