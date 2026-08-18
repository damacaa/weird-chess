#pragma once

// Annotation system (main thread): drains finished annotations from the
// AsyncAnnotator worker and publishes them - pushes to the narrator worker
// thread (stage 1: PassThroughNarrator; stage 2: LlamaNarrator), updates the
// move log and refreshes the highlights. The heavy evaluation work itself
// happens on the annotator thread so the piece animation never stalls.

#include "chess/AnnotationWriter.h"
#include "chess/AsyncAnnotator.h"
#include "components/ChessState.h"
#include "globals.h"

namespace wchess
{
	namespace AnnotationSystem
	{
		// Pushes the annotation into the narrator queue and the move log.
		inline void publish(ChessState& state, const MoveAnnotation& ann)
		{
			Logger::log("[Story Input] " + AnnotationWriter::formatLLMEvent(ann));

			if (state.narrator)
				state.narrator->push(ann);

			std::string log = std::to_string(ann.fullMoveNumber) + ".";
			if (ann.mover == Color::Black)
				log += "..";
			log += " " + (ann.san.empty() ? "?" : ann.san);
			state.moveLog.push_back(log);
			if (state.moveLog.size() > 6)
				state.moveLog.erase(state.moveLog.begin());
		}

		// Registered update system: picks up finished annotations. Highlights
		// are already up to date (applyMove refreshes them), so only the
		// text side is published here.
		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessState& state = getState(registry);
			if (!state.moveAppliedPendingAnnotation || !state.annotator)
				return;
			MoveAnnotation ann;
			if (state.annotator->poll(ann))
			{
				state.moveAppliedPendingAnnotation = false;
				state.lastAnnotation = ann;
				state.hasLastAnnotation = true;
				publish(state, ann);

				if (state.board && state.board->isGameOver())
				{
					state.gameOver = true;
					if (state.board->gameState() == GameState::Checkmate && !state.checkmateJingleTriggered)
					{
						state.checkmateJingleTriggered = true;
						state.checkmateJingleTimer = 0.0f;
						state.checkmateJingleStep = 0;
					}
				}
			}
		}
	} // namespace AnnotationSystem
} // namespace wchess
