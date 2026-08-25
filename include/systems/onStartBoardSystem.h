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
#include <filesystem>

namespace wchess
{
	inline void onStartBoardSystem(Registry& registry, ServiceProvider& services)
	{
		ChessState& state = getState(registry);

		// ---- 2D materials ----
		for (size_t i = 0; i < ColorPalette::Default.size() && i < 16; ++i)
		{
			auto& m = services.materials2D().get(static_cast<uint16_t>(i));
			m.color = ColorPalette::Default[i];
		}
		
		services.materials2D().get(ChessPalette::BLACK_PIECE_MATERIAL_IDX).color = vec4(0.02f, 0.02f, 0.07f, 1.0f);
		services.materials2D().get(ChessPalette::WHITE_PIECE_MATERIAL_IDX).color = vec4(0.93f, 0.93f, 0.90f, 1.0f);
		services.materials2D().get(ChessPalette::PANEL_TEXT_DIM_MATERIAL_IDX).color = vec4(0.52f, 0.52f, 0.58f, 1.0f);
		services.materials2D().get(ChessPalette::LIGHT_SQUARE_MATERIAL_IDX).color = vec4(0.84f, 0.78f, 0.66f, 1.0f);
		services.materials2D().get(ChessPalette::DARK_SQUARE_MATERIAL_IDX).color = vec4(0.50f, 0.37f, 0.24f, 1.0f);

		// ---- board rules + AI ----
		state.board = std::make_shared<ChessLibBoard>();
		state.board->loadStartPosition();

		// ---- load human-readable config (assets/config.json) ----
		ChessConfig::GameSettings gameSettings = ChessConfig::loadGameSettings();
		state.typewriterSpeed = gameSettings.typewriterSpeed;
		WeirdEngine::Logger::log("[WeirdChess] Config loaded: difficulty='" + gameSettings.difficulty +
								 "' (skill=" + std::to_string(gameSettings.skillLevel) +
								 ", elo=" + std::to_string(gameSettings.elo) +
								 "), model='" + gameSettings.modelName + "'" +
								 ", device=" + gameSettings.device +
								 (gameSettings.device == "gpu" ? " (" + std::to_string(gameSettings.gpuLayers) + " layers)" : "") +
								 ", threads=" + std::to_string(gameSettings.threads) +
								 ", seed=" + (gameSettings.seed >= 0 ? std::to_string(gameSettings.seed) : "random") +
								 ", typewriter_speed=" + std::to_string(gameSettings.typewriterSpeed) + ")");

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
				stockfish->setStrength(gameSettings.skillLevel, gameSettings.elo);
				state.ai = stockfish;
			}
		}
		if (!state.ai)
		{
			WeirdEngine::Logger::log("[WeirdChess] Stockfish binary not found, using in-process MinimaxAI.");
			auto minimax = std::make_shared<MinimaxAI>();
			minimax->setStrength(gameSettings.skillLevel, gameSettings.elo);
			state.ai = minimax;
		}

		// Background annotation worker: keeps move evaluations off the main
		// thread so pieces animate immediately after a move.
		state.annotator = std::make_shared<AsyncAnnotator>(state.ai);
		state.annotator->start();

		// ---- narrator (worker thread) ----
		// Model discovery: look for configured modelName or any .gguf files
		std::string modelPath;
		if (const char* env = std::getenv(ChessConfig::LLAMA_MODEL_PATH_ENV.c_str()))
		{
			WeirdEngine::Logger::log("[WeirdChess] " + ChessConfig::LLAMA_MODEL_PATH_ENV + " environment override: " + env);
			modelPath = env;
		}
		else
		{
			std::vector<std::string> searchDirs;
#ifdef ASSETS_PATH
			searchDirs.push_back(std::string(ASSETS_PATH) + "model");
			searchDirs.push_back(std::string(ASSETS_PATH) + "models");
#endif
			for (const auto& dir : ChessConfig::LLAMA_MODEL_DIRECTORIES)
			{
				searchDirs.push_back(dir);
				searchDirs.push_back("../" + dir);
			}

			// 1. Check if configured modelName exists in any search directory
			if (!gameSettings.modelName.empty())
			{
				for (const auto& dir : searchDirs)
				{
					std::string candidate = dir + "/" + gameSettings.modelName;
					std::error_code ec;
					if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
					{
						modelPath = candidate;
						WeirdEngine::Logger::log("[WeirdChess] Found configured model: " + modelPath);
						break;
					}
				}
			}

			// 2. If not found, scan search directories for any .gguf file
			if (modelPath.empty())
			{
				for (const auto& dir : searchDirs)
				{
					std::error_code ec;
					if (std::filesystem::exists(dir, ec) && std::filesystem::is_directory(dir, ec))
					{
						for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
						{
							std::string ext = entry.path().extension().string();
							std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
							if (entry.is_regular_file() && ext == ".gguf")
							{
								modelPath = entry.path().string();
								WeirdEngine::Logger::log("[WeirdChess] Discovered model candidate: " + modelPath);
								break;
							}
						}
					}
					if (!modelPath.empty())
						break;
				}
			}

			if (modelPath.empty())
			{
				std::string searchLog = "[WeirdChess] No GGUF model files found. Checked directories:\n";
				for (const auto& dir : searchDirs)
				{
					std::error_code ec;
					bool exists = std::filesystem::exists(dir, ec);
					searchLog += "  - " + dir + (exists ? " [EXISTS, no .gguf]\n" : " [NOT FOUND]\n");
				}
				WeirdEngine::Logger::warning(searchLog);
			}
		}

		auto llama = std::make_shared<LlamaNarrator>();
		llama->setDevice(gameSettings.device, gameSettings.gpuLayers);
		if (gameSettings.threads > 0)
		{
			llama->setThreadCount(gameSettings.threads);
		}
		if (gameSettings.seed >= 0)
		{
			llama->setSeed(gameSettings.seed);
		}
		if (!modelPath.empty() && llama->load(modelPath))
		{
			state.narratorImpl = llama;
		}
		else
		{
			WeirdEngine::Logger::log("[WeirdChess] Falling back to engine move annotation narrator.");
			state.narratorImpl = std::make_shared<PassThroughNarrator>();
		}

		if (gameSettings.seed >= 0)
		{
			state.narratorImpl->setSeed(gameSettings.seed);
		}
		if (!gameSettings.premise.empty())
		{
			state.narratorImpl->setPremise(gameSettings.premise);
		}

		state.narrator = std::make_shared<NarratorThread>(state.narratorImpl);
		state.narrator->start();
		state.narrator->pushIntro();

		// ---- background + text sizes ----
		auto& bg = services.render().getBackground();
		bg.type = BackgroundType::Custom;
		bg.intensity = 0.0f; // dynamically driven each frame by AnimationSystem::computeGameIntensity
		bg.customShaderCode = R"(
			vec3 getBackground(vec2 uv, vec2 worldPos)
			{
				// Derive true world position matching the camera framing and SDF foreground shapes.
				// u_camMatrix[3].xy = -camPos.xy, u_camMatrix[3].z = -camPos.z
				vec2 trueWorldPos = -u_camMatrix[3].xy + (uv * (-u_camMatrix[3].z));

				const float CELL = 15.0; // ChessConfig::CELL
				const float BOARD_SIZE = 8.0 * CELL; // 120.0

				// Balatro-style swirling procedural background:
				// Slow time-based domain warping with contour isolines
				float t = u_time * 0.15;
				vec2 p = uv * 2.2;

				for (int i = 1; i <= 3; i++)
				{
					float fi = float(i);
					vec2 np = p;
					np.x += (0.35 / fi) * sin(fi * 2.4 * p.y + t * 0.7 + fi * 1.3);
					np.y += (0.35 / fi) * cos(fi * 2.4 * p.x + t * 0.5 + fi * 0.9);
					p = np;
				}

				float wave1 = sin(p.x * 2.5 + p.y * 2.0 + t * 0.3);
				float wave2 = cos(length(p) * 2.2 - t * 0.5);
				float pattern = sin(wave1 * 3.14159 + wave2 * 2.0);
				float mask = 0.5 + 0.5 * pattern;

				// Subtle stepped contour lines (posterized bands)
				float stepped = floor(mask * 7.0) / 7.0;
				float subtleBands = mix(mask, stepped, 0.15);
				float bandEdge = 0.5 + 0.5 * sin(mask * 22.0 + t * 0.4);

				// Dark moody palette with rich subtle colors (midnight indigo, deep wine/plum, ocean teal)
				vec3 darkBase   = vec3(0.030, 0.032, 0.045);
				vec3 deepPlum   = vec3(0.085, 0.048, 0.110);
				vec3 oceanTeal  = vec3(0.038, 0.078, 0.105);
				vec3 glowAccent = vec3(0.160, 0.080, 0.125);

				vec3 tableColor = mix(darkBase, deepPlum, smoothstep(0.1, 0.6, subtleBands));
				tableColor = mix(tableColor, oceanTeal, smoothstep(0.35, 0.85, sin(p.y * 1.6 + t * 0.25) * 0.5 + 0.5));
				tableColor = mix(tableColor, glowAccent, smoothstep(0.72, 1.0, subtleBands) * (0.40 + u_bgIntensity * 0.35));
				tableColor += bandEdge * (0.012 + u_bgIntensity * 0.015);

				// Simple horizontal left-to-right wave repeating ~10 times across board height:
				float waveSpeed = 2.8;
				float waveFreq = (10.0 * 6.2831853) / BOARD_SIZE; // exactly ~10 wave cycles over the board height
				float waveAmp = u_bgIntensity * 0.8; // horizontal displacement

				float wave = sin(u_time * waveSpeed + trueWorldPos.y * waveFreq);
				vec2 wiggledPos = vec2(trueWorldPos.x + wave * waveAmp, trueWorldPos.y);

				// Antialiased outer board border with rounded corners (rounded box SDF)
				float cornerRadius = 2.0; // 2px corner radius
				vec2 boxCenter = vec2(BOARD_SIZE * 0.5);
				vec2 boxHalf = vec2(BOARD_SIZE * 0.5);
				vec2 q = abs(wiggledPos - boxCenter) - boxHalf + cornerRadius;
				float boardDist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - cornerRadius;
				float borderFw = max(length(fwidth(wiggledPos)) * 0.7071, 0.001);
				float borderAlpha = smoothstep(0.5 * borderFw, -0.5 * borderFw, boardDist);

				// Analytic antialiased checkerboard calculation
				vec2 gridPos = wiggledPos / CELL;
				vec2 gridFw = max(fwidth(gridPos), vec2(0.0001));
				vec2 gridStep = 2.0 * (abs(fract((gridPos - 0.5) * 0.5) - 0.5) - 0.25);
				gridStep = clamp(gridStep / gridFw, -1.0, 1.0);
				float checker = clamp(0.5 - 0.5 * gridStep.x * gridStep.y, 0.0, 1.0);

				vec3 lightColor = u_bgPrimaryColor.rgb;
				vec3 darkColor  = u_bgSecondaryColor.rgb;
				vec3 boardColor = mix(darkColor, lightColor, checker);

				// Antialiased composite between Balatro table background and the chessboard
				return mix(tableColor, boardColor, borderAlpha);
			}
			)";

		// World text: single-char labels next to the board.
		// services.render().getContext2D().dotRadious = 0.01f;
		// services.render().getContext2D().charSpacing = 0.3f;
		// UI text: compact so the story panel and button labels stay readable
		// (the layout system derives line pitch from these values).
		services.render().getContextUI().dotRadious = 5.0f;
		services.render().getContextUI().charSpacing = 10.0f;

		// ---- piece & board SDFs ----
		PieceShapes::registerAll(services.shapes());
		BoardShapes::registerAll(services.shapes());

		// ---- pieces pool & board ----
		PieceShapes::initPiecePool(registry, services.shapes(), state.allPieceEntities, ChessConfig::PIECE_SCALE);
		state.squareEntities = BoardShapes::createBoard(registry, services.shapes(), state);
		state.pieceEntities.assign(64, INVALID_ENTITY);
		MoveSystem::syncPieces(state, registry, services.shapes());

		// ---- right panel (pure story UI) ----
		// The main header text holds the title of the story
		state.titleText = UIButtonFactory::createText(registry, 0.0f, 0.0f, "WEIRD CHESS", ChessPalette::PANEL_TITLE_MATERIAL_IDX);

		for (int i = 0; i < ChessConfig::STORY_MAX_LINES; ++i)
		{
			state.storyLines.push_back(UIButtonFactory::createText(registry, 0.0f, 0.0f, "", ChessPalette::PANEL_TEXT_MATERIAL_IDX));
		}

		// ---- buttons and toggles (bottom right UI strip, distinct shapes without text) ----
		// Reset button: Square Box shape
		state.newGameButton = UIButtonFactory::createButton(registry, services.shapes(), 0.0f, 0.0f, 40.0f, 40.0f,
															ChessPalette::BUTTON_BOX_MATERIAL_IDX);

		// Manual opponent override toggle: Circle shape
		state.disableAIToggle =
			UIButtonFactory::createToggle(registry, services.shapes(), 0.0f, 0.0f, 18.0f, ChessPalette::TOGGLE_CIRCLE_MATERIAL_IDX);

		// ---- promotion modal UI (centered overlay when promoting) ----
		state.promoCard = UIButtonFactory::createBoxLine(
			registry, services.shapes(), -1000.0f, -1000.0f, 440.0f, 160.0f, 6.0f, ChessPalette::PANEL_TEXT_MATERIAL_IDX);

		// Promotion shape buttons (Queen, Rook, Bishop, Knight - green like text)
		state.promoQueenButton = UIButtonFactory::createPieceButton(
			registry, services.shapes(), PieceShapes::s_ids[PieceShapes::QUEEN], -1000.0f, -1000.0f, 50.0f,
			ChessPalette::PANEL_TEXT_MATERIAL_IDX);

		state.promoRookButton = UIButtonFactory::createPieceButton(
			registry, services.shapes(), PieceShapes::s_ids[PieceShapes::ROOK], -1000.0f, -1000.0f, 50.0f,
			ChessPalette::PANEL_TEXT_MATERIAL_IDX);

		state.promoBishopButton = UIButtonFactory::createPieceButton(
			registry, services.shapes(), PieceShapes::s_ids[PieceShapes::BISHOP], -1000.0f, -1000.0f, 50.0f,
			ChessPalette::PANEL_TEXT_MATERIAL_IDX);

		state.promoKnightButton = UIButtonFactory::createPieceButton(
			registry, services.shapes(), PieceShapes::s_ids[PieceShapes::KNIGHT], -1000.0f, -1000.0f, 50.0f,
			ChessPalette::PANEL_TEXT_MATERIAL_IDX);

		// ---- layout (positions everything) ----
		state.lastResolutionHash = 0;
		LayoutSystem::apply(state, registry, services);
	}
} // namespace wchess
