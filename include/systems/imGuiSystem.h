#pragma once

// Debug overlay: engine + game state, useful while developing.

#include "components/ChessState.h"
#include "globals.h"

namespace wchess
{
	inline void imGuiSystem(Registry& registry, ServiceProvider& services)
	{
		ChessState& state = getState(registry);

		ImGui::Text("Weird Chess");
		ImGui::Separator();
		if (state.board)
		{
			ImGui::Text("FEN: %s", state.board->getFen().c_str());
			ImGui::Text("Turn: %s", state.board->sideToMove() == Color::White ? "White" : "Black");
			ImGui::Text("Halfmove clock: %d", state.board->halfMoveClock());
			ImGui::Text("Fullmove: %d", state.board->fullMoveNumber());
		}
		ImGui::Text("AI: %s", state.ai ? state.ai->name().c_str() : "none");
		ImGui::Text("Narrator: %s", state.narrator ? state.narrator->narratorName().c_str() : "none");
		if (state.hasLastAnnotation)
		{
			ImGui::Text("Last: %s %s (loss %.2f pawns)",
						state.lastAnnotation.quality == MoveQuality::Blunder ? "BLUNDER" : "move",
						state.lastAnnotation.san.c_str(), static_cast<float>(state.lastAnnotation.lossCp) / 100.0f);
		}
		ImGui::Text("Game over: %s", state.gameOver ? "yes" : "no");
	}
} // namespace wchess
