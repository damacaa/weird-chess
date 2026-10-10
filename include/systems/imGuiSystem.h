#pragma once

// Debug overlay: engine + game state, useful while developing.

#include "components/SessionState.h"
#include "globals.h"

namespace wchess
{
	inline void imGuiSystem(Registry& registry, ServiceProvider& services)
	{
		ChessSessionState& session = getSessionState(registry);

		ImGui::Text("Weird Chess");
		ImGui::Separator();
		if (session.board)
		{
			ImGui::Text("FEN: %s", session.board->getFen().c_str());
			ImGui::Text("Turn: %s", session.board->sideToMove() == Color::White ? "White" : "Black");
			ImGui::Text("Halfmove clock: %d", session.board->halfMoveClock());
			ImGui::Text("Fullmove: %d", session.board->fullMoveNumber());
		}
		ImGui::Text("AI: %s", session.ai ? session.ai->name().c_str() : "none");
		ImGui::Text("Narrator: %s", session.narrator ? session.narrator->narratorName().c_str() : "none");
		if (session.hasLastAnnotation)
		{
			ImGui::Text("Last: %s %s (loss %.2f pawns)",
						session.lastAnnotation.quality == MoveQuality::Blunder ? "BLUNDER" : "move",
						session.lastAnnotation.san.c_str(), static_cast<float>(session.lastAnnotation.lossCp) / 100.0f);
		}
		ImGui::Text("Intensity: %.2f (target: %.2f)", session.currentIntensity, session.targetIntensity);
		ImGui::Text("Game over: %s", session.gameOver ? "yes" : "no");
	}
} // namespace wchess
