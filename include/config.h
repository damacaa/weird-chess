#pragma once

// Game tuning constants. Central place for every number that affects the
// board layout, the camera framing and the AI behaviour.

#include <cstdint>
#include <string>

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
	inline constexpr int STORY_MAX_LINES = 28;

	// ---- Animation ----
	inline constexpr float MOVE_ANIM_SECONDS = 0.18f;

	// ---- AI / engine ----
	// Path candidates for the Stockfish binary (checked in order, first hit
	// wins). Can be overridden with the STOCKFISH_PATH environment variable.
	inline const std::string STOCKFISH_PATH_ENV = "STOCKFISH_PATH";
	inline const std::vector<std::string> STOCKFISH_CANDIDATES = {"bin/stockfish", "../bin/stockfish"};

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
	inline constexpr float GOOD_LOSS_PAWNS = 0.5f;
	inline constexpr float INACCURACY_LOSS_PAWNS = 1.0f;
	inline constexpr float MISTAKE_LOSS_PAWNS = 2.5f;
	inline constexpr float MISS_WIN_PAWNS = 2.0f; // a capture win this big that was ignored = "miss"

	inline constexpr int ANNOTATION_QUEUE_DEPTH = 32;
} // namespace ChessConfig
