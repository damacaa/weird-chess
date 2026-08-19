#pragma once

// Player interaction: click-to-select, click-to-move, promotion selection
// (keys Q/R/B/N), deselection (Esc), all guarded by UI-click detection and
// turn state. Uses the ShapeButton/ShapeToggle components for the UI strip.

#include "components/ChessState.h"
#include "globals.h"
#include "shapes/PieceShapes.h"
#include "systems/moveSystem.h"

#include <cstdlib>
#include <random>

namespace wchess
{
	namespace InputSystem
	{
		// Converts the mouse position to a board square index (0-63) or -1.
		inline int squareAtMouse(Registry& registry, ServiceProvider& services)
		{
			auto& camTransform = registry.getComponent<Transform>(services.render().getCameraEntity());
			vec2 mouseWorld = ECS::Camera::screenPositionToWorldPosition2D(
				camTransform, vec2(services.input().getMouseX(), services.input().getMouseY()));

			int file = static_cast<int>(std::floor(mouseWorld.x / ChessConfig::CELL));
			int rank = static_cast<int>(std::floor(mouseWorld.y / ChessConfig::CELL));
			if (file < 0 || file >= 8 || rank < 0 || rank >= 8)
				return -1;
			return rank * 8 + file;
		}

		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessState& state = getState(registry);
			if (!state.board || !state.ai || state.gameOver)
				return;

			Color currentTurn = state.board->sideToMove();
			Color humanColor = state.playerIsWhite ? Color::White : Color::Black;
			Color activeColor = state.disableAI ? currentTurn : humanColor;

			float mouseX = services.input().getMouseX();
			float mouseY = services.input().getMouseY();
			bool mouseClicked = services.input().getMouseButtonDown(Input::LeftClick);
			bool isUI = services.input().isUIClick();

			// ---- promotion selection (shape buttons + board square click + keyboard fallback) ----
			if (state.awaitingPromotion)
			{
				PieceType chosen = PieceType::Queen;
				bool pick = false;

				const float halfW = static_cast<float>(Display::width) * 0.5f;
				const float halfH = static_cast<float>(Display::height) * 0.5f;
				const float panelLeft = halfW + ChessConfig::PANEL_LEFT_MARGIN_PX;
				const float panelCenterX = panelLeft + (static_cast<float>(Display::width) - 32.0f - panelLeft) * 0.5f;
				const float promoSpacing = 100.0f;
				const float promoStartX = panelCenterX - 1.5f * promoSpacing;
				const float promoY = halfH + 10.0f;

				auto checkSlot = [&](Entity btn, int index, PieceType type, const char* name)
				{
					(void)name;
					if (btn != INVALID_ENTITY &&
						registry.getComponent<ShapeButton>(btn).state == ButtonState::Down)
					{
						chosen = type;
						pick = true;
						return;
					}
					if (mouseClicked)
					{
						float slotX = promoStartX + static_cast<float>(index) * promoSpacing;
						if (std::abs(mouseX - slotX) < 45.0f && std::abs(mouseY - promoY) < 55.0f)
						{
							chosen = type;
							pick = true;
						}
					}
				};

				checkSlot(state.promoQueenButton, 0, PieceType::Queen, "Queen");
				checkSlot(state.promoRookButton, 1, PieceType::Rook, "Rook");
				checkSlot(state.promoBishopButton, 2, PieceType::Bishop, "Bishop");
				checkSlot(state.promoKnightButton, 3, PieceType::Knight, "Knight");

				// Keyboard fallback
				if (services.input().getKeyDown(Input::Q) || services.input().getKeyDown(Input::Num1))
				{
					chosen = PieceType::Queen;
					pick = true;
				}
				else if (services.input().getKeyDown(Input::R) || services.input().getKeyDown(Input::Num2))
				{
					chosen = PieceType::Rook;
					pick = true;
				}
				else if (services.input().getKeyDown(Input::B) || services.input().getKeyDown(Input::Num3))
				{
					chosen = PieceType::Bishop;
					pick = true;
				}
				else if (services.input().getKeyDown(Input::N) || services.input().getKeyDown(Input::Num4))
				{
					chosen = PieceType::Knight;
					pick = true;
				}
				else if (services.input().getKeyDown(Input::Esc))
				{
					state.awaitingPromotion = false;
					state.promoFrom = -1;
					state.promoTo = -1;
					state.selectedSquare = -1;
					state.legalTargets.clear();
					state.layoutDirty = true;
					MoveSystem::refreshHighlights(state, registry, services);
					return;
				}

				if (pick)
				{
					Move move;
					move.from = Square::fromIndex(state.promoFrom);
					move.to = Square::fromIndex(state.promoTo);
					move.isPromotion = true;
					move.promotion = chosen;
					state.awaitingPromotion = false;
					state.promoFrom = -1;
					state.promoTo = -1;
					state.selectedSquare = -1;
					state.legalTargets.clear();
					state.layoutDirty = true;
					MoveSystem::applyMove(state, registry, services, move, activeColor);
				}
				return;
			}

			// ---- only interactive if it's the active player's turn ----
			if (state.aiThinking || (!state.disableAI && currentTurn != humanColor))
			{
				return;
			}

			// Debug helper: WEIRDCHESS_AUTOPLAY=1 makes the human side play
			// random legal moves so the whole pipeline (ECS, animations,
			// annotations, narrator) can be exercised headlessly.
			static const bool autoplay = []()
			{
				const char* env = std::getenv("WEIRDCHESS_AUTOPLAY");
				return env && env[0] == '1';
			}();

			if (autoplay)
			{
				auto moves = state.board->legalMoves();
				if (moves.empty())
					return;
				static std::mt19937 rng{std::random_device{}()};
				std::uniform_int_distribution<size_t> dist(0, moves.size() - 1);
				Move pick = moves[dist(rng)];
				// promote to queen when a pawn reaches the last rank
				if (pick.isPromotion)
					pick.promotion = PieceType::Queen;
				MoveSystem::applyMove(state, registry, services, pick, activeColor);
				return;
			}

			if (services.input().getKeyDown(Input::Esc))
			{
				state.selectedSquare = -1;
				state.legalTargets.clear();
				MoveSystem::refreshHighlights(state, registry, services);
				return;
			}

			if (!mouseClicked)
				return;

			if (isUI)
			{
				return;
			}

			int square = squareAtMouse(registry, services);
			if (square < 0)
			{
				return;
			}

			auto piece = state.board->pieceAt(Square::fromIndex(square));

			// ---- clicking logic ----
			// If a piece is selected, check if the clicked square (or friendly rook for
			// castling) is a legal move.
			if (state.selectedSquare >= 0)
			{
				int targetSquare = square;

				// Special case: if King is selected and player clicks the friendly castling
				// Rook, map rook square to the King's castling destination square (g-file
				// for h-rook, c-file for a-rook).
				auto selectedPiece = state.board->pieceAt(Square::fromIndex(state.selectedSquare));
				if (selectedPiece && selectedPiece->second == PieceType::King && piece &&
					piece->first == activeColor && piece->second == PieceType::Rook)
				{
					int rank = state.selectedSquare / 8;
					int rookFile = square % 8;
					if (rookFile == 7)
						targetSquare = rank * 8 + 6;
					else if (rookFile == 0)
						targetSquare = rank * 8 + 2;
				}

				for (int target : state.legalTargets)
				{
					if (target == targetSquare)
					{
						// Find the matching legal move to preserve all flags (castling, en
						// passant, promotion)
						Move move;
						bool found = false;
						for (const auto& m : state.board->legalMovesFrom(Square::fromIndex(state.selectedSquare)))
						{
							if (m.to.index() == targetSquare)
							{
								move = m;
								found = true;
								break;
							}
						}
						if (!found)
						{
							move.from = Square::fromIndex(state.selectedSquare);
							move.to = Square::fromIndex(targetSquare);
						}

						// Pawn reaching the last rank: defer until the player
						// picks a piece via promotion shape buttons.
						auto movingPiece = state.board->pieceAt(move.from);
						int promoRank = activeColor == Color::White ? 7 : 0;

						if (move.to.rank == promoRank && movingPiece && movingPiece->second == PieceType::Pawn)
						{
							state.awaitingPromotion = true;
							state.promoFrom = state.selectedSquare;
							state.promoTo = targetSquare;
							state.layoutDirty = true;
							return;
						}

						state.selectedSquare = -1;
						state.legalTargets.clear();
						MoveSystem::applyMove(state, registry, services, move, activeColor);
						return;
					}
				}
			}

			// Clicking a piece of the active color (re)selects it.
			if (piece && piece->first == activeColor)
			{
				state.selectedSquare = square;
				state.legalTargets.clear();
				for (const auto& m : state.board->legalMovesFrom(Square::fromIndex(square)))
				{
					state.legalTargets.push_back(m.to.index());
				}
				MoveSystem::refreshHighlights(state, registry, services);
				return;
			}

			// Empty click on an empty/foreign square: deselect.
			state.selectedSquare = -1;
			state.legalTargets.clear();
			MoveSystem::refreshHighlights(state, registry, services);
		}
	} // namespace InputSystem
} // namespace wchess
