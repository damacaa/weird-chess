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

// Material ids used by the 2D renderer. The 2D pipelines index the
// DisplaySettings::colorPalette[16] by material id (see main.cpp where the
// palette is tuned for chess).
namespace ChessPalette
{
	inline constexpr uint16_t BlackPiece = DisplaySettings::DefaultColors::Black; // repurposed: dark piece color
	inline constexpr uint16_t WhitePiece = DisplaySettings::DefaultColors::White;
	inline constexpr uint16_t LightSquare = DisplaySettings::DefaultColors::LightGray; // repurposed: tan light square
	inline constexpr uint16_t DarkSquare = DisplaySettings::DefaultColors::Brown;	   // repurposed: wood dark square
	inline constexpr uint16_t HighlightCyan = DisplaySettings::DefaultColors::Cyan;
	inline constexpr uint16_t HighlightYellow = DisplaySettings::DefaultColors::Yellow;
	inline constexpr uint16_t HighlightGreen = DisplaySettings::DefaultColors::Green;
	inline constexpr uint16_t CheckRed = DisplaySettings::DefaultColors::Red;
	inline constexpr uint16_t PanelText = DisplaySettings::DefaultColors::LightGreen;
	inline constexpr uint16_t PanelTextDim = DisplaySettings::DefaultColors::Gray;
	inline constexpr uint16_t PanelTitle = DisplaySettings::DefaultColors::Orange;
	inline constexpr uint16_t PanelAccent = DisplaySettings::DefaultColors::Magenta;
	inline constexpr uint16_t ButtonBox = DisplaySettings::DefaultColors::Gray;
	inline constexpr uint16_t ButtonText = DisplaySettings::DefaultColors::White;
	inline constexpr uint16_t ToggleCircle = DisplaySettings::DefaultColors::Cyan;
} // namespace ChessPalette
