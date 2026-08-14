#pragma once

// onStart: builds the whole game - piece shapes, board, pieces, right-panel
// UI, narrator worker thread and AI. Also sets the dark background and the
// world/UI text sizes.

#include "chess/MinimaxAI.h"
#include "chess/NullAI.h"
#include "chess/StockfishUCIAI.h"
#include "components/ChessState.h"
#include "config.h"
#include "globals.h"
#include "shapes/BoardShapes.h"
#include "shapes/PieceShapes.h"
#include "shapes/UIButtonFactory.h"
#include "systems/layoutSystem.h"
#include "systems/moveSystem.h"

#include <cstdlib>

namespace wchess
{
	inline void onStartBoardSystem(Registry& registry, ServiceProvider& services)
	{
		ChessState& state = getState(registry);

		// ---- board rules + AI ----
		state.board = std::make_shared<ChessLibBoard>();
		state.board->loadStartPosition();

		// Stockfish if available, otherwise our in-process MinimaxAI.
		std::string sfPath;
		if (const char* env = std::getenv(ChessConfig::STOCKFISH_PATH_ENV.c_str()))
		{
			sfPath = env;
		}
		else
		{
			for (const auto& candidate : ChessConfig::STOCKFISH_CANDIDATES)
			{
				FILE* f = std::fopen(candidate.c_str(), "rb");
				if (f)
				{
					std::fclose(f);
					sfPath = candidate;
					break;
				}
			}
		}

		if (!sfPath.empty())
		{
			auto stockfish = std::make_shared<StockfishUCIAI>(sfPath);
			if (stockfish->isAvailable())
			{
				stockfish->setStrength(ChessConfig::DEFAULT_SKILL, ChessConfig::DEFAULT_ELO);
				state.ai = stockfish;
			}
		}
		if (!state.ai)
		{
			std::cout << "[WeirdChess] Stockfish binary not found, using in-process MinimaxAI." << std::endl;
			auto minimax = std::make_shared<MinimaxAI>();
			minimax->setStrength(ChessConfig::DEFAULT_SKILL, ChessConfig::DEFAULT_ELO);
			state.ai = minimax;
		}

		// Background annotation worker: keeps move evaluations off the main
		// thread so pieces animate immediately after a move.
		state.annotator = std::make_shared<AsyncAnnotator>(state.ai);
		state.annotator->start();

		// ---- narrator (worker thread) ----
		state.narratorImpl = std::make_shared<PassThroughNarrator>();
		state.narrator = std::make_shared<NarratorThread>(state.narratorImpl);
		state.narrator->start();

		// ---- background + text sizes ----
		auto& bg = services.render().getBackground();
		bg.type = BackgroundType::Solid;
		bg.primaryColor = vec4(0.055f, 0.06f, 0.08f, 1.0f);

		// World text: single-char labels next to the board.
		// services.render().getContext2D().dotRadious = 0.01f;
		// services.render().getContext2D().charSpacing = 0.3f;
		// UI text: compact so the story panel and button labels stay readable
		// (the layout system derives line pitch from these values).
		services.render().getContextUI().dotRadious = 5.0f;
		services.render().getContextUI().charSpacing = 10.0f;

		// ---- piece SDFs ----
		PieceShapes::registerAll(services.shapes());

		// ---- board ----
		state.squareEntities = BoardShapes::createBoard(registry, services.shapes(), state.highlightEntities);
		state.pieceEntities.assign(64, INVALID_ENTITY);
		MoveSystem::syncPieces(state, registry, services.shapes());

		// ---- labels ----
		auto labels = BoardShapes::createLabels(registry);
		state.rankLabels.assign(labels.begin(), labels.begin() + 8);
		state.fileLabels.assign(labels.begin() + 8, labels.end());

		// ---- right panel (pure story UI) ----
		// The main header text holds the title of the story
		state.titleText = UIButtonFactory::createText(registry, 0.0f, 0.0f, "WEIRD CHESS", ChessPalette::PanelTitle);

		for (int i = 0; i < ChessConfig::STORY_MAX_LINES; ++i)
		{
			state.storyLines.push_back(UIButtonFactory::createText(registry, 0.0f, 0.0f, "", ChessPalette::PanelText));
		}

		// ---- buttons and toggles (bottom right UI strip, distinct shapes without text) ----
		// Reset button: Square Box shape
		state.newGameButton = UIButtonFactory::createButton(registry, services.shapes(), 0.0f, 0.0f, 40.0f, 40.0f,
															ChessPalette::ButtonBox);

		// Manual opponent override toggle: Circle shape
		state.disableAIToggle =
			UIButtonFactory::createToggle(registry, services.shapes(), 0.0f, 0.0f, 18.0f, ChessPalette::ToggleCircle);

		// ---- layout (positions everything) ----
		state.lastResolutionHash = 0;
		LayoutSystem::apply(state, registry, services);
	}
} // namespace wchess
