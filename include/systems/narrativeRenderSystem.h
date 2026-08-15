#pragma once

// Drains the narrator's StoryStream on the main thread and displays it in the
// right panel (one UITextRenderer per line). Also updates the status bar and
// the move log. All text content updates happen here; positions come from the
// layout system.

#include "components/ChessState.h"
#include "config.h"
#include "globals.h"

#include <deque>
#include <string>

namespace wchess
{
	namespace NarrativeRenderSystem
	{
		// Simple greedy wrap at `wrapChars` characters (no mid-word split).
		inline std::vector<std::string> wrapLines(const std::string& text, int wrapChars)
		{
			std::vector<std::string> out;
			if (wrapChars <= 0)
				wrapChars = 40;

			auto pushWrapped = [&](const std::string& line)
			{
				if (line.empty())
				{
					out.push_back("");
					return;
				}
				if (static_cast<int>(line.size()) <= wrapChars)
				{
					out.push_back(line);
					return;
				}
				size_t start = 0;
				while (start < line.size())
				{
					size_t end = std::min(line.size(), start + static_cast<size_t>(wrapChars));
					// try to break at a space
					if (end < line.size())
					{
						size_t space = line.rfind(' ', end);
						if (space != std::string::npos && space > start)
							end = space;
					}
					out.push_back(line.substr(start, end - start));
					start = end;
					if (start < line.size() && line[start] == ' ')
						++start;
				}
			};

			size_t start = 0;
			while (true)
			{
				size_t nl = text.find('\n', start);
				if (nl == std::string::npos)
				{
					pushWrapped(text.substr(start));
					break;
				}
				pushWrapped(text.substr(start, nl - start));
				start = nl + 1;
			}
			return out;
		}

		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessState& state = getState(registry);
			if (state.storyLines.empty())
				return;

			const int wrapChars = 20; // rough; layout keeps lines inside the panel

			// Drain anything the narrator produced this frame.
			if (state.narrator)
			{
				auto chunks = state.narrator->stream()->drain();
				for (auto& chunk : chunks)
				{
					for (auto& line : wrapLines(chunk, wrapChars))
					{
						state.storyText.push_back(line);
					}
				}

				// Keep only as many lines as we can display.
				while (static_cast<int>(state.storyText.size()) > state.storyVisibleLines)
					state.storyText.pop_front();
			}

			// Push the visible lines into the text entities.
			size_t i = 0;
			for (; i < state.storyLines.size(); ++i)
			{
				auto& text = registry.getComponent<UITextRenderer>(state.storyLines[i]);
				std::string content = i < state.storyText.size() ? state.storyText[i] : "";
				if (text.text != content)
				{
					text.text = content;
					registry.setComponentDirty(text);
				}
			}

			// ---- status bar ----
			if (state.statusText != INVALID_ENTITY)
			{
				std::string status;
				if (state.gameOver)
				{
					switch (state.board->gameState())
					{
						case GameState::Checkmate:
							status = state.board->sideToMove() == Color::White ? "CHECKMATE - BLACK WINS"
																			   : "CHECKMATE - WHITE WINS";
							break;
						case GameState::Stalemate:
							status = "STALEMATE - DRAW";
							break;
						default:
							status = "DRAW";
							break;
					}
				}
				else if (state.awaitingPromotion)
				{
					status = "PROMOTE: Q R B N (KEYS)";
				}
				else if (state.aiThinking)
				{
					status = "AI THINKING...";
				}
				else if (state.hasLastAnnotation)
				{
					status = state.lastAnnotation.title + " " + state.lastAnnotation.san;
				}
				else if (state.disableAI)
				{
					status = state.board->sideToMove() == Color::White ? "WHITE TO MOVE" : "BLACK TO MOVE";
				}
				else
				{
					status = state.board->sideToMove() == (state.playerIsWhite ? Color::White : Color::Black)
								 ? "YOUR TURN"
								 : "AI TURN";
				}

				auto& text = registry.getComponent<UITextRenderer>(state.statusText);
				if (text.text != status)
				{
					text.text = status;
					registry.setComponentDirty(text);
				}
			}

			// ---- story status ----
			if (state.storyStatus != INVALID_ENTITY && state.narrator)
			{
				std::string s;
				switch (state.narrator->stream()->status())
				{
					case StoryStatus::EndedAbruptly:
						s = "THE STORY ENDS ABRUPTLY";
						break;
					case StoryStatus::EndedNaturally:
						s = "THE STORY SETTLES";
						break;
					case StoryStatus::Generating:
						s = "THE STORY UNFOLDS...";
						break;
					default:
						s = "AWAITING THE STORY...";
						break;
				}
				auto& text = registry.getComponent<UITextRenderer>(state.storyStatus);
				if (text.text != s)
				{
					text.text = s;
					registry.setComponentDirty(text);
				}
			}

			// ---- move log ----
			if (state.moveLogText != INVALID_ENTITY)
			{
				std::string log;
				for (size_t i2 = 0; i2 < state.moveLog.size(); ++i2)
				{
					if (i2 > 0)
						log += "  ";
					log += state.moveLog[i2];
				}
				auto& text = registry.getComponent<UITextRenderer>(state.moveLogText);
				if (text.text != log)
				{
					text.text = log;
					registry.setComponentDirty(text);
				}
			}
		}
	} // namespace NarrativeRenderSystem
} // namespace wchess
