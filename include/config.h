#pragma once

// Game tuning constants. Central place for every number that affects the
// board layout, the camera framing and the AI behaviour.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <json/json.h>

namespace ChessConfig
{
	// ---- Board (world units) ----
	// The board is a 8x8 grid in world space. Square (file 0, rank 0) is the
	// world origin (a1), x grows towards the h-file, y grows towards rank 8.
	// CELL = 15 world units: the whole board is 120x120, so the world is
	// comfortably large (the camera zooms out to frame it).
	inline constexpr float CELL = 15.0f;
	inline constexpr int BOARD_FILES = 8;
	inline constexpr int BOARD_RANKS = 8;
	inline constexpr float BOARD_WORLD = CELL * 8.0f; // 120.0
	inline constexpr float PIECE_SCALE = CELL * 0.62f;
	inline constexpr int MAX_TARGET_HIGHLIGHTS = 28; // max legal moves for any piece (Queen max is 27)

	// Board positioning inside the window: the board's right edge is kept a
	// few pixels left of the screen centre so the story panel has its own
	// half. These are used by the layout system (see systems/layoutSystem.h).
	inline constexpr float BOARD_RIGHT_MARGIN_PX = 24.0f;  // board right edge -> screen centre distance
	inline constexpr float BOARD_MAX_HEIGHT_RATIO = 0.82f; // board may occupy up to 82% of the window height
	inline constexpr float BOARD_MAX_WIDTH_RATIO = 0.92f;  // board may occupy up to 92% of the left half width
	inline constexpr float RANK_LABEL_GAP_PX = 10.0f;	   // rank labels sit this many px left of the board
	inline constexpr float FILE_LABEL_GAP_PX = 8.0f;	   // file labels sit this many px below the board

	// ---- Story panel (screen px) ----
	inline constexpr float PANEL_LEFT_MARGIN_PX = 40.0f; // story text starts this far right of the centre
	inline constexpr float PANEL_RIGHT_MARGIN_PX = 24.0f;
	inline constexpr float PANEL_TOP_MARGIN_PX = 56.0f;
	inline constexpr int STORY_MAX_LINES = 64;
	inline constexpr int STORY_MAX_HISTORY_BEATS = 6;	 // max recent story paragraphs in rolling context window
	inline constexpr int STORY_MAX_CONTEXT_TOKENS = 400; // max token budget for story context prompt (tuned for 512 context)
	inline constexpr std::string_view STORY_INTRO_PLACEHOLDER = "The conflict begins.";

	// Typewriter text pacing (characters per second)
	// Slows down text rendering with a minimum speed floor so AI text updates smoothly char by char
	inline constexpr float STORY_TYPEWRITER_MIN_SPEED = 18.0f; // characters per second floor (~55ms / char)
	inline constexpr float STORY_TYPEWRITER_MAX_SPEED = 36.0f; // catch-up speed ceiling during large queues

	// ---- Animation ----
	// Constant average movement speed (world units per second).
	// CELL = 15.0f world units, so 60.0f speed = 4 squares/sec (0.25s per single square).
	inline constexpr float MOVE_ANIM_SPEED = 60.0f;
	inline constexpr float MOVE_ANIM_MIN_SECONDS = 0.15f; // minimum duration floor so short moves are not instantaneous

	// ---- AI / engine ----
	// Path candidates for the Stockfish binary (checked in order, first hit
	// wins). Can be overridden with the STOCKFISH_PATH environment variable.
	inline const std::string STOCKFISH_PATH_ENV = "STOCKFISH_PATH";
	inline const std::vector<std::string> STOCKFISH_CANDIDATES = {"bin/stockfish", "../bin/stockfish"};

	// ---- LLM Narrator (llama.cpp) ----
	inline const std::string LLAMA_MODEL_PATH_ENV = "LLAMA_MODEL_PATH";
	inline const std::vector<std::string> LLAMA_MODEL_DIRECTORIES = {
		"assets/model",
		"assets/models",
		"bin/models",
		"models"
	};

	// Human-strength defaults: tuned for very easy / casual play.
	inline constexpr int DEFAULT_SKILL = 0;			  // 0-20 Stockfish internal skill knob
	inline constexpr int DEFAULT_ELO = 1320;		  // minimum UCI_Elo
	inline constexpr int AI_SEARCH_DEPTH = 1;		  // shallow search depth (1-ply) for easy play
	inline constexpr float AI_BLUNDER_CHANCE = 0.35f; // 35% chance of casual random moves
	inline constexpr int AI_MOVETIME_MS = 50;		  // max think time for AI reply
	inline constexpr int EVAL_MOVETIME_MS = 350;	  // think time used for move classification / narrator

	// Human-like opponent thinking delay (seconds spent pondering before move)
	inline constexpr float AI_MIN_THINK_SECONDS = 1.0f;
	inline constexpr float AI_MAX_THINK_SECONDS = 3.0f;

	// ---- Move classification thresholds (in pawns, 1 pawn = 100cp) ----
	inline constexpr float BEST_LOSS_PAWNS = 0.15f;
	inline constexpr float EXCELLENT_LOSS_PAWNS = 0.40f;
	inline constexpr float GOOD_LOSS_PAWNS = 0.85f;
	inline constexpr float INACCURACY_LOSS_PAWNS = 1.75f;
	inline constexpr float MISTAKE_LOSS_PAWNS = 3.50f;
	inline constexpr float MISS_WIN_PAWNS = 2.0f; // a winning advantage that was surrendered = "miss"

	// ---- Human-readable Configuration (assets/config.json) ----
	struct GameSettings
	{
		std::string modelName = "tinyllama-15M-stories-Q2_K.gguf";
		std::string device = "cpu"; // "cpu" or "gpu" (Vulkan / CUDA offload)
		int gpuLayers = 99;         // number of layers to offload to GPU when device is "gpu"
		int threads = 4;          // Number of CPU threads for inference (default 4 on Steam Deck / multi-core)
		std::string premise = ""; // custom starting premise; empty = auto-generate random premise
		int64_t seed = -1;        // -1 = random seed, >= 0 = forced deterministic seed
		std::string difficulty = "easy";
		int skillLevel = DEFAULT_SKILL; // 0
		int elo = DEFAULT_ELO;          // 1320
		int searchDepth = AI_SEARCH_DEPTH; // 1
		float blunderChance = AI_BLUNDER_CHANCE; // 0.35f
		float typewriterSpeed = STORY_TYPEWRITER_MIN_SPEED; // 18.0f chars/sec
	};

	inline GameSettings loadGameSettings(const std::string& assetsPath = "")
	{
		GameSettings settings;

		std::vector<std::string> candidates;
		if (!assetsPath.empty())
		{
			candidates.push_back(assetsPath + "/config.json");
			candidates.push_back(assetsPath + "config.json");
		}
#ifdef ASSETS_PATH
		candidates.push_back(std::string(ASSETS_PATH) + "config.json");
#endif
		candidates.push_back("assets/config.json");
		candidates.push_back("../assets/config.json");
		candidates.push_back("config.json");

		std::string foundPath;
		for (const auto& path : candidates)
		{
			std::error_code ec;
			if (std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec))
			{
				foundPath = path;
				break;
			}
		}

		if (foundPath.empty())
			return settings;

		try
		{
			std::ifstream file(foundPath);
			if (!file.is_open())
				return settings;

			nlohmann::json j;
			file >> j;

			// 1. Model configuration
			if (j.contains("model"))
			{
				if (j["model"].is_object())
				{
					if (j["model"].contains("name") && j["model"]["name"].is_string())
						settings.modelName = j["model"]["name"].get<std::string>();
					if (j["model"].contains("threads") && j["model"]["threads"].is_number_integer())
						settings.threads = std::max(1, j["model"]["threads"].get<int>());
					if (j["model"].contains("device") && j["model"]["device"].is_string())
						settings.device = j["model"]["device"].get<std::string>();
					if (j["model"].contains("gpu_layers") && j["model"]["gpu_layers"].is_number_integer())
						settings.gpuLayers = std::max(0, j["model"]["gpu_layers"].get<int>());
				}
				else if (j["model"].is_string())
				{
					settings.modelName = j["model"].get<std::string>();
				}
			}
			else if (j.contains("model_name") && j["model_name"].is_string())
			{
				settings.modelName = j["model_name"].get<std::string>();
			}

			if (j.contains("threads") && j["threads"].is_number_integer())
			{
				settings.threads = std::max(1, j["threads"].get<int>());
			}

			if (j.contains("device") && j["device"].is_string())
			{
				settings.device = j["device"].get<std::string>();
			}
			if (j.contains("gpu_layers") && j["gpu_layers"].is_number_integer())
			{
				settings.gpuLayers = std::max(0, j["gpu_layers"].get<int>());
			}

			std::transform(settings.device.begin(), settings.device.end(), settings.device.begin(), ::tolower);

			// Story Premise configuration
			if (j.contains("story") && j["story"].is_object() && j["story"].contains("premise") &&
				j["story"]["premise"].is_string())
			{
				settings.premise = j["story"]["premise"].get<std::string>();
			}
			else if (j.contains("premise") && j["premise"].is_string())
			{
				settings.premise = j["premise"].get<std::string>();
			}

			// Story seed configuration (supports "seed" in "story", "model", or root)
			if (j.contains("story") && j["story"].is_object() && j["story"].contains("seed") &&
				j["story"]["seed"].is_number_integer())
			{
				settings.seed = j["story"]["seed"].get<int64_t>();
			}
			else if (j.contains("model") && j["model"].is_object() && j["model"].contains("seed") &&
					 j["model"]["seed"].is_number_integer())
			{
				settings.seed = j["model"]["seed"].get<int64_t>();
			}
			else if (j.contains("seed") && j["seed"].is_number_integer())
			{
				settings.seed = j["seed"].get<int64_t>();
			}

			// Typewriter speed configuration
			if (j.contains("typewriter_speed") && j["typewriter_speed"].is_number())
			{
				settings.typewriterSpeed = j["typewriter_speed"].get<float>();
			}
			else if (j.contains("story_speed") && j["story_speed"].is_number())
			{
				settings.typewriterSpeed = j["story_speed"].get<float>();
			}

			// 2. Enemy / AI difficulty configuration
			std::string diff = "easy";
			if (j.contains("enemy"))
			{
				const auto& enemy = j["enemy"];
				if (enemy.is_object())
				{
					if (enemy.contains("difficulty") && enemy["difficulty"].is_string())
						diff = enemy["difficulty"].get<std::string>();
					if (enemy.contains("skill_level") && enemy["skill_level"].is_number_integer())
						settings.skillLevel = enemy["skill_level"].get<int>();
					if (enemy.contains("elo") && enemy["elo"].is_number_integer())
						settings.elo = enemy["elo"].get<int>();
					if (enemy.contains("search_depth") && enemy["search_depth"].is_number_integer())
						settings.searchDepth = enemy["search_depth"].get<int>();
					if (enemy.contains("blunder_chance") && enemy["blunder_chance"].is_number())
						settings.blunderChance = enemy["blunder_chance"].get<float>();
				}
				else if (enemy.is_string())
				{
					diff = enemy.get<std::string>();
				}
			}
			else if (j.contains("difficulty") && j["difficulty"].is_string())
			{
				diff = j["difficulty"].get<std::string>();
			}

			std::transform(diff.begin(), diff.end(), diff.begin(), ::tolower);
			settings.difficulty = diff;

			if (!j.contains("enemy") || !j["enemy"].is_object() || !j["enemy"].contains("skill_level"))
			{
				if (diff == "easy")
				{
					settings.skillLevel = 0;
					settings.elo = 1320;
					settings.searchDepth = 1;
					settings.blunderChance = 0.35f;
				}
				else if (diff == "medium" || diff == "normal")
				{
					settings.skillLevel = 5;
					settings.elo = 1600;
					settings.searchDepth = 3;
					settings.blunderChance = 0.15f;
				}
				else if (diff == "hard")
				{
					settings.skillLevel = 12;
					settings.elo = 2000;
					settings.searchDepth = 5;
					settings.blunderChance = 0.05f;
				}
				else if (diff == "master" || diff == "expert" || diff == "max")
				{
					settings.skillLevel = 20;
					settings.elo = 2500;
					settings.searchDepth = 8;
					settings.blunderChance = 0.0f;
				}
			}
		}
		catch (const std::exception&)
		{
			// parsing failed, use default settings
		}

		return settings;
	}
} // namespace ChessConfig
