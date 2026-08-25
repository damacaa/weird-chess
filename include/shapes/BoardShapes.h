#pragma once

// Builds the board: 64 square shapes, a highlight overlay per square, the
// rank/file labels and helpers to convert between square indices and world
// coordinates. The board lives in world space; the camera is framed so the
// board occupies the left half of the window (see systems/layoutSystem.h).

#include "components/ChessState.h"
#include "components/SquareComp.h"
#include "config.h"
#include "globals.h"
#include "shapes/PieceShapes.h"

namespace wchess
{
	namespace BoardShapes
	{
		// Custom SDF for Approach 2: Stippled / Halftone dot matrix inside square
		struct StippledSquareSDF : public IMathExpression
		{
			[[nodiscard]]
			float getValue(const float* parameters) const override
			{
				float px = parameters[0];
				float py = parameters[1];
				float hw = parameters[2];
				float spacing = parameters[3];
				float radius = parameters[4];
				float border = parameters[5];

				float worldX = parameters[Primitives::WORLD_X];
				float worldY = parameters[Primitives::WORLD_Y];

				float dx = std::abs(worldX - px) - hw;
				float dy = std::abs(worldY - py) - hw;
				float d_box = std::max(dx, dy);

				float lx = (worldX - px) + 0.5f * spacing;
				float ly = (worldY - py) + 0.5f * spacing;
				float mx = std::fmod(std::fmod(lx, spacing) + spacing, spacing) - 0.5f * spacing;
				float my = std::fmod(std::fmod(ly, spacing) + spacing, spacing) - 0.5f * spacing;
				float d_dots = std::sqrt(mx * mx + my * my) - radius;

				float d_stipple = std::max(d_box, d_dots);
				if (border > 0.0f)
				{
					float d_border = std::abs(d_box) - border;
					return std::min(d_border, d_stipple);
				}
				return d_stipple;
			}

			[[nodiscard]]
			std::string print() const override
			{
				// var0: px, var1: py, var2: hw, var3: spacing, var4: radius, var5: border
				// var9: worldX (p.x), var10: worldY (p.y)
				return "min(abs(max(abs(var9 - var0) - var2, abs(var10 - var1) - var2)) - var5, "
					   "max(max(abs(var9 - var0) - var2, abs(var10 - var1) - var2), "
					   "length(mod(vec2(var9 - var0, var10 - var1) + (0.5 * var3), var3) - (0.5 * var3)) - var4))";
			}
		};

		// Custom SDF for Approach 3: Concentric Inset Rings / Grooves inside square
		struct ConcentricSquareSDF : public IMathExpression
		{
			[[nodiscard]]
			float getValue(const float* parameters) const override
			{
				float px = parameters[0];
				float py = parameters[1];
				float hw = parameters[2];
				float spacing = parameters[3];
				float thickness = parameters[4];

				float worldX = parameters[Primitives::WORLD_X];
				float worldY = parameters[Primitives::WORLD_Y];

				float dx = std::abs(worldX - px) - hw;
				float dy = std::abs(worldY - py) - hw;
				float d_box = std::max(dx, dy);

				float dist_in = -d_box;
				if (dist_in > 0.0f)
				{
					float shifted = dist_in + 0.5f * spacing;
					float m = std::fmod(std::fmod(shifted, spacing) + spacing, spacing) - 0.5f * spacing;
					float ring = std::abs(m) - thickness;
					return std::max(d_box, ring);
				}
				return d_box;
			}

			[[nodiscard]]
			std::string print() const override
			{
				// var0: px, var1: py, var2: hw, var3: spacing, var4: thickness
				// Outer box: max(abs(var9 - var0) - var2, abs(var10 - var1) - var2)
				// Nested rings: abs(mod(-(max(abs(var9 - var0) - var2, abs(var10 - var1) - var2)) + (0.5 * var3), var3)
				// - (0.5 * var3)) - var4
				return "max(max(abs(var9 - var0) - var2, abs(var10 - var1) - var2), "
					   "abs(mod(-(max(abs(var9 - var0) - var2, abs(var10 - var1) - var2)) + (0.5 * var3), var3) - "
					   "(0.5 * var3)) - var4)";
			}
		};

		inline ShapeId s_stippleShapeId = 0;
		inline ShapeId s_concentricShapeId = 0;

		inline void registerAll(ShapeService& shapes)
		{
			s_stippleShapeId = shapes.registerSDF(std::make_shared<StippledSquareSDF>());
			s_concentricShapeId = shapes.registerSDF(std::make_shared<ConcentricSquareSDF>());
		}

		// Light and dark square material ids (palette slots, see globals.h).
		inline constexpr uint16_t LIGHT = ChessPalette::LIGHT_SQUARE_MATERIAL_IDX;
		inline constexpr uint16_t DARK = ChessPalette::DARK_SQUARE_MATERIAL_IDX;

		// Helper to create a single BOX_LINE highlight overlay with a fixed material.
		inline Entity createHighlightShape(ShapeService& shapes, uint16_t material)
		{
			float hw2 = ChessConfig::CELL * 0.46f;
			float th = ChessConfig::CELL * 0.05f;
			return shapes.addShape({.shapeId = DefaultShapes::BOX_LINE,
									.variables = {-1000.0f, -1000.0f, hw2, hw2, th},
									.material = material,
									.combination = CombinationType::Addition,
									.hasCollision = false,
									.group = 0});
		}

		// Creates the 64 square entities + pool of legal move overlays (cyan) + dedicated overlays for
		// selection (green), last move (yellow), and check (red).
		inline std::vector<Entity> createBoard(Registry& registry, ShapeService& shapes, ChessState& state)
		{
			std::vector<Entity> squares;
			squares.reserve(64);

			for (int index = 0; index < 64; ++index)
			{
				int file = index & 7;
				int rank = index >> 3;

				Entity square = registry.createEntity();
				SquareComp& comp = registry.addComponent<SquareComp>(square);
				comp.file = file;
				comp.rank = rank;
				comp.index = index;
				registry.setComponentDirty(comp);

				squares.push_back(square);
			}

			// Pool of legal target overlays (Cyan, fixed color).
			// A piece in chess can have at most 27 legal moves (e.g. Queen in center),
			// so a pool of 28 is sufficient for all possible legal move destinations.
			state.highlightEntities.clear();
			state.highlightEntities.reserve(ChessConfig::MAX_TARGET_HIGHLIGHTS);
			for (int i = 0; i < ChessConfig::MAX_TARGET_HIGHLIGHTS; ++i)
			{
				Entity highlight = createHighlightShape(shapes, ChessPalette::HIGHLIGHT_CYAN_MATERIAL_IDX);
				registry.setComponentDirty(registry.getComponent<CustomShape>(highlight));
				state.highlightEntities.push_back(highlight);
			}

			// Dedicated overlays with fixed colors so no shader recompilation is needed
			state.selectionHighlight = createHighlightShape(shapes, ChessPalette::HIGHLIGHT_GREEN_MATERIAL_IDX);
			registry.setComponentDirty(registry.getComponent<CustomShape>(state.selectionHighlight));

			state.lastMoveFromHighlight = createHighlightShape(shapes, ChessPalette::HIGHLIGHT_YELLOW_MATERIAL_IDX);
			registry.setComponentDirty(registry.getComponent<CustomShape>(state.lastMoveFromHighlight));

			state.lastMoveToHighlight = createHighlightShape(shapes, ChessPalette::HIGHLIGHT_YELLOW_MATERIAL_IDX);
			registry.setComponentDirty(registry.getComponent<CustomShape>(state.lastMoveToHighlight));

			state.checkHighlight = createHighlightShape(shapes, ChessPalette::CHECK_RED_MATERIAL_IDX);
			registry.setComponentDirty(registry.getComponent<CustomShape>(state.checkHighlight));

			return squares;
		}

		// Sets a highlight overlay visible at a square (or hides it when
		// squareIndex < 0). Only updates parameters (position), never the material,
		// so no shader recompilation is triggered.
		inline void setHighlight(Registry& registry, Entity highlight, int squareIndex)
		{
			if (highlight == INVALID_ENTITY)
				return;
			auto& shape = registry.getComponent<CustomShape>(highlight);
			if (squareIndex < 0)
			{
				shape.parameters[0] = -1000.0f;
				shape.parameters[1] = -1000.0f;
			}
			else
			{
				vec2 c = PieceShapes::squareCenterWorld(squareIndex);
				shape.parameters[0] = c.x;
				shape.parameters[1] = c.y;
			}
			registry.setComponentDirty(shape);
		}
	} // namespace BoardShapes
} // namespace wchess
