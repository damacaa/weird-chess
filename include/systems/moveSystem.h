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
		// Piece footprint is ~62% of a cell (design is s=1.0 fits 0.95).
		inline constexpr float PIECE_SCALE = ChessConfig::CELL * 0.62f;

		// Rebuilds all piece entities from the board state.
		inline void syncPieces(ChessState& state, Registry& registry, ShapeService& shapes)
		{
			for (auto& e : state.pieceEntities)
			{
				if (e != INVALID_ENTITY)
				{
					registry.destroyEntity(e);
					e = INVALID_ENTITY;
				}
			}
			registry.freeRemovedComponents();

			for (int index = 0; index < 64; ++index)
			{
				Square square{index & 7, index >> 3};
				auto piece = state.board->pieceAt(square);
				if (!piece)
					continue;

				vec2 c = PieceShapes::squareCenterWorld(index);
				Entity entity =
					PieceShapes::spawnPiece(registry, shapes, piece->first, piece->second, c.x, c.y, PIECE_SCALE);
				auto& pc = registry.addComponent<PieceComp>(entity);
				pc.color = piece->first;
				pc.type = piece->second;
				pc.squareIndex = index;
				registry.setComponentDirty(pc);

				state.pieceEntities[index] = entity;
				if (registry.hasComponent<SquareComp>(state.squareEntities[index]))
					registry.getComponent<SquareComp>(state.squareEntities[index]).piece = entity;
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
		inline void refreshHighlights(ChessState& state, Registry& registry, ServiceProvider& services)
		{
			for (int i = 0; i < 64; ++i)
				BoardShapes::setHighlight(registry, state.highlightEntities[i], -1, ChessPalette::HighlightCyan);

			if (state.selectedSquare >= 0)
				BoardShapes::setHighlight(registry, state.highlightEntities[state.selectedSquare], state.selectedSquare,
										  ChessPalette::HighlightGreen);
			for (int target : state.legalTargets)
				BoardShapes::setHighlight(registry, state.highlightEntities[target], target,
										  ChessPalette::HighlightCyan);

			// Last move highlight (yellow) on both squares. Painted from
			// lastMove: it is set synchronously in applyMove, while the
			// annotation (and its move) only lands later on the worker.
			if (state.hasLastMove)
			{
				int fromIdx = state.lastMove.from.index();
				int toIdx = state.lastMove.to.index();
				if (fromIdx != state.selectedSquare)
					BoardShapes::setHighlight(registry, state.highlightEntities[fromIdx], fromIdx,
											  ChessPalette::HighlightYellow);
				if (toIdx != state.selectedSquare)
					BoardShapes::setHighlight(registry, state.highlightEntities[toIdx], toIdx,
											  ChessPalette::HighlightYellow);
			}

			// King in check (red).
			if (!state.gameOver && state.board->inCheck())
			{
				Color kingColor = state.board->sideToMove();
				int kingIdx = state.board->kingSquare(kingColor).index();
				BoardShapes::setHighlight(registry, state.highlightEntities[kingIdx], kingIdx, ChessPalette::CheckRed);
			}

			// One batch refresh: materials are baked into the generated
			// shader, so a single regeneration covers all highlight changes.
			services.render().forceShaderRefresh2D();
		}

		// Applies a validated move to the board + scene immediately (the piece
		// animates right away) and hands the heavy annotation work to the
		// background annotator. `mover` must equal the side to move. The
		// enemy reply is deferred until the annotation lands and the
		// animation finishes (see aiSystem).
		inline void applyMove(ChessState& state, Registry& registry, ServiceProvider& services, const Move& move,
							  Color mover)
		{
			// Any in-flight animation references pieces that syncPieces is
			// about to destroy: drop the animation state first (the pieces
			// snap to their squares, then the new animation starts).
			state.animatingPieces.clear();
			state.animFrom.clear();
			state.animTo.clear();
			state.animT.clear();

			int legalBefore = static_cast<int>(state.board->legalMoves().size());
			std::string beforeFen = state.board->getFen();
			if (!state.board->makeMove(move))
				return;
			std::string afterFen = state.board->getFen();

			auto lastApplied = state.board->lastMove();
			Move fullMove = lastApplied.value_or(move);

			int fromIdx = fullMove.from.index();
			int toIdx = fullMove.to.index();

			// Visuals first: rebuild the mirror and start the animation.
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

			if (state.narrator)
			{
				state.narrator->stream()->clear();
			}

			syncPieces(state, registry, services.shapes());
			refreshHighlights(state, registry, services);
		}
	} // namespace MoveSystem
} // namespace wchess
