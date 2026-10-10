#pragma once

// Annotation system (main thread): drains finished annotations from the
// AsyncAnnotator worker and publishes them - pushes to the narrator worker
// thread (stage 1: PassThroughNarrator; stage 2: LlamaNarrator) and refreshes
// the highlights. The heavy evaluation work itself happens on the annotator
// thread so the piece animation never stalls.

#include "chess/AnnotationWriter.h"
#include "chess/AsyncAnnotator.h"
#include "components/SessionState.h"
#include "globals.h"

namespace wchess
{
	namespace AnnotationSystem
	{
		// Pushes the annotation into the narrator queue and the move log.
		inline void publish(ChessSessionState& session, const MoveAnnotation& ann)
		{
			if (session.narrator)
				session.narrator->push(ann);

			std::string log = std::to_string(ann.fullMoveNumber) + ".";
			if (ann.mover == Color::Black)
				log += "..";
			log += " " + (ann.san.empty() ? "?" : ann.san);
			session.moveLog.push_back(log);
			if (session.moveLog.size() > 6)
				session.moveLog.erase(session.moveLog.begin());
		}

		// Registered update system: picks up finished annotations. Highlights
		// are already up to date (applyMove refreshes them), so only the
		// text side is published here.
		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessSessionState& session = getSessionState(registry);
			if (!session.moveAppliedPendingAnnotation || !session.annotator)
				return;
			MoveAnnotation ann;
			if (session.annotator->poll(ann))
			{
				session.moveAppliedPendingAnnotation = false;
				session.lastAnnotation = ann;
				session.hasLastAnnotation = true;
				publish(session, ann);

				if (session.board && session.board->isGameOver())
				{
					session.gameOver = true;
					if (session.board->gameState() == GameState::Checkmate && !session.checkmateJingleTriggered)
					{
						session.checkmateJingleTriggered = true;
						session.checkmateJingleTimer = 0.0f;
						session.checkmateJingleStep = 0;
					}
				}
			}
		}
	} // namespace AnnotationSystem
} // namespace wchess
