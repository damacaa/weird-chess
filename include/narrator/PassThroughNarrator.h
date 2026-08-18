#pragma once

// Stage-1 narrator: echoes the engine-generated move annotations to the story
// stream (no LLM). It still runs on the worker thread (via NarratorThread) so
// the threading pipeline is exercised from day one, and it honours the story
// rules that stage 2 will follow:
//   - a blunder ends the story abruptly
//   - a checkmate ends the story abruptly
//   - otherwise the story keeps growing for as long as the game lasts
// Swap in LlamaNarrator in stage 2 (see llamacpp-integration.md).

#include "chess/AnnotationWriter.h"
#include "config.h"
#include "narrator/INarrator.h"

#include <chrono>
#include <string>
#include <thread>

namespace wchess
{
	class PassThroughNarrator : public INarrator
	{
	public:
		std::string name() const override
		{
			return "passthrough";
		}

		void narrateIntro(StoryStream& out) override
		{
			out.append(std::string(ChessConfig::STORY_INTRO_PLACEHOLDER));
			out.setStatus(StoryStatus::Idle);
		}

		void narrate(const MoveAnnotation& annotation, StoryStream& out) override
		{
			using namespace std::chrono_literals;

			const std::string mover = annotation.mover == Color::White ? "White" : "Black";

			std::string moveLine = mover + " (" + std::to_string(annotation.fullMoveNumber) + ") - " +
								   annotation.title + " " + annotation.san + " (" +
								   AnnotationWriter::evalText(annotation) + ")";

			out.append(moveLine);
			std::this_thread::sleep_for(5ms); // simulate streaming so the pipeline is visible

			if (!annotation.specialEvent.empty())
				out.append(annotation.specialEvent);

			if (!annotation.tradeEvent.empty())
				out.append(annotation.tradeEvent);

			if (!annotation.gameStatus.empty())
				out.append(annotation.gameStatus);

			// Story-length rule: critical blunders and mates stop the story abruptly.
			if (annotation.tactics.checkmate || (annotation.gameEnded && annotation.gameState == GameState::Checkmate))
			{
				if (annotation.specialEvent.empty())
					out.append("Checkmate. " + mover + " wins the game.");
				out.setStatus(StoryStatus::EndedAbruptly);
			}
			else if (annotation.quality == MoveQuality::Blunder && annotation.impact == ImpactLevel::Critical)
			{
				out.setStatus(StoryStatus::EndedAbruptly);
			}
			else if (annotation.tactics.stalemate || annotation.tactics.draw ||
					 (annotation.gameEnded && annotation.gameState != GameState::Ongoing))
			{
				out.append("Game ended in a draw.");
				out.setStatus(StoryStatus::EndedNaturally);
			}
			else
			{
				out.setStatus(StoryStatus::Generating);
			}
		}
	};
} // namespace wchess
