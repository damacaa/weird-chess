#pragma once

// Project-wide includes. Every scene/system includes this first so the
// engine API, the vendored chess library and the common math SDF helpers
// are always available.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <weird-engine.h>
#include <weird-engine/math/Default2DSDFs.h>
#include <weird-renderer/core/Display.h>

#include "chess.hpp"

using namespace WeirdEngine;

// The camera position shared by scenes (mirrors sample-scenes/globals.h).
extern vec3 g_cameraPositon;

// Material ids used by the 2D renderer.
namespace ChessPalette
{
	inline constexpr uint16_t BLACK_PIECE_MATERIAL_IDX = 0;     // repurposed: dark piece color
	inline constexpr uint16_t WHITE_PIECE_MATERIAL_IDX = 1;
	inline constexpr uint16_t LIGHT_SQUARE_MATERIAL_IDX = 3;    // repurposed: tan light square
	inline constexpr uint16_t DARK_SQUARE_MATERIAL_IDX = 15;    // repurposed: wood dark square
	inline constexpr uint16_t HIGHLIGHT_CYAN_MATERIAL_IDX = 10;
	inline constexpr uint16_t HIGHLIGHT_YELLOW_MATERIAL_IDX = 7;
	inline constexpr uint16_t HIGHLIGHT_GREEN_MATERIAL_IDX = 5;
	inline constexpr uint16_t CHECK_RED_MATERIAL_IDX = 4;
	inline constexpr uint16_t PANEL_TEXT_MATERIAL_IDX = 12;
	inline constexpr uint16_t PANEL_TEXT_DIM_MATERIAL_IDX = 2;
	inline constexpr uint16_t PANEL_TITLE_MATERIAL_IDX = 8;
	inline constexpr uint16_t PANEL_ACCENT_MATERIAL_IDX = 11;
	inline constexpr uint16_t BUTTON_BOX_MATERIAL_IDX = 2;
	inline constexpr uint16_t BUTTON_TEXT_MATERIAL_IDX = 1;
	inline constexpr uint16_t TOGGLE_CIRCLE_MATERIAL_IDX = 10;
} // namespace ChessPalette
