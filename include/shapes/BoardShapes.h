#pragma once

// Builds the board: 64 square shapes, a highlight overlay per square, the
// rank/file labels and helpers to convert between square indices and world
// coordinates. The board lives in world space; the camera is framed so the
// board occupies the left half of the window (see systems/layoutSystem.h).

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
		inline constexpr uint16_t LIGHT = ChessPalette::LightSquare;
		inline constexpr uint16_t DARK = ChessPalette::DarkSquare;

		// Creates the 64 square entities + 64 highlight overlays.
		inline std::vector<Entity> createBoard(Registry& registry, ShapeService& shapes,
											   std::vector<Entity>& outHighlights)
		{
			std::vector<Entity> squares;
			squares.reserve(64);
			outHighlights.clear();
			outHighlights.reserve(64);

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

				// Highlight overlay (hidden off-screen until used).
				float hw2 = ChessConfig::CELL * 0.46f;
				float th = ChessConfig::CELL * 0.05f;
				Entity highlight = shapes.addShape({.shapeId = DefaultShapes::BOX_LINE,
													.variables = {-1000.0f, -1000.0f, hw2, hw2, th},
													.material = ChessPalette::HighlightCyan,
													.combination = CombinationType::Addition,
													.hasCollision = false,
													.group = 0});
				comp.highlight = highlight;
				registry.setComponentDirty(registry.getComponent<CustomShape>(highlight));

				squares.push_back(square);
				outHighlights.push_back(highlight);
			}
			return squares;
		}

		// Sets a highlight overlay visible at a square (or hides it when
		// index < 0). Does NOT force a shader refresh - callers batch that
		// once after updating a set of highlights (material changes are baked
		// into the generated shader, so one refresh covers all of them).
		inline void setHighlight(Registry& registry, Entity highlight, int squareIndex, uint16_t material)
		{
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
			shape.material = material;
			registry.setComponentDirty(shape);
		}
	} // namespace BoardShapes
} // namespace wchess
