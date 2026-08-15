#pragma once

// Drains the narrator's StoryStream on the main thread and displays it in the
// right panel (one UITextRenderer per line). Also updates the status bar and
// the move log. All text content updates happen here; positions come from the
// layout system.

#include "components/ChessState.h"
#include "config.h"
#include "globals.h"

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

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
					while (start < line.size() && line[start] == ' ')
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

		// Helper to compute max characters that fit within the story panel width
		inline int calculateWrapChars(ServiceProvider& services)
		{
			auto& uiCtx = services.render().getContextUI();
			const float width = static_cast<float>(Display::width);
			const float halfW = width * 0.5f;
			const float panelLeft = halfW + ChessConfig::PANEL_LEFT_MARGIN_PX;
			const float panelRightMargin = ChessConfig::PANEL_RIGHT_MARGIN_PX;
			const float availableWidth = width - panelLeft - panelRightMargin;

			const float glyphWidth = static_cast<float>(uiCtx.font.getCharWidth()) * 2.0f * uiCtx.dotRadious;
			const float charAdvance = glyphWidth + uiCtx.charSpacing;

			if (charAdvance > 0.0f && availableWidth > 0.0f)
			{
				return std::max(6, static_cast<int>((availableWidth + uiCtx.charSpacing) / charAdvance));
			}
			return 20;
		}

		// Formats raw story chunks into display lines wrapped at wrapChars,
		// inserting extra spacing (empty line) between distinct messages.
		inline std::vector<std::string> formatStoryLines(const std::vector<std::string>& chunks, int wrapChars)
		{
			std::vector<std::string> allLines;
			for (const auto& chunk : chunks)
			{
				if (chunk.empty())
					continue;

				auto lines = wrapLines(chunk, wrapChars);
				if (lines.empty())
					continue;

				// Add extra line spacing between distinct messages
				if (!allLines.empty() && !allLines.back().empty() && !lines.front().empty())
				{
					allLines.push_back("");
				}

				for (auto& line : lines)
				{
					if (line.empty() && !allLines.empty() && allLines.back().empty())
						continue;
					allLines.push_back(line);
				}
			}
			return allLines;
		}

		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessState& state = getState(registry);
			if (state.storyLines.empty())
				return;

			const int wrapChars = calculateWrapChars(services);

			bool newChunks = false;
			// Drain anything the narrator produced this frame.
			if (state.narrator)
			{
				auto chunks = state.narrator->stream()->drain();
				if (!chunks.empty())
				{
					newChunks = true;
					for (auto& chunk : chunks)
					{
						state.rawStoryChunks.push_back(chunk);
					}
				}
			}

			if (newChunks || wrapChars != state.lastWrapChars || state.layoutDirty)
			{
				state.lastWrapChars = wrapChars;
				state.layoutDirty = false;

				std::vector<std::string> allLines = formatStoryLines(state.rawStoryChunks, wrapChars);

				// Keep only the visible window of lines (tail)
				size_t startLineIdx = allLines.size() > static_cast<size_t>(state.storyVisibleLines)
										  ? allLines.size() - static_cast<size_t>(state.storyVisibleLines)
										  : 0;

				state.storyText.clear();
				for (size_t i = 0; i < state.storyLines.size(); ++i)
				{
					size_t lineIdx = startLineIdx + i;
					std::string content =
						(i < static_cast<size_t>(state.storyVisibleLines) && lineIdx < allLines.size())
							? allLines[lineIdx]
							: "";
					if (i < static_cast<size_t>(state.storyVisibleLines) && lineIdx < allLines.size())
					{
						state.storyText.push_back(content);
					}

					auto& text = registry.getComponent<UITextRenderer>(state.storyLines[i]);
					if (text.text != content)
					{
						text.text = content;
						registry.setComponentDirty(text);
					}
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
