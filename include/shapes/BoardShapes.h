#pragma once

// Builds the board: 64 square shapes, a highlight overlay per square, the
// rank/file labels and helpers to convert between square indices and world
// coordinates. The board lives in world space; the camera is framed so the
// board occupies the left half of the window (see systems/layoutSystem.h).

#include "components/SquareComp.h"
#include "config.h"
#include "globals.h"
#include "shapes/PieceShapes.h"

namespace wchess {
namespace BoardShapes {
// Light and dark square material ids (palette slots, see globals.h).
inline constexpr uint16_t LIGHT = ChessPalette::LightSquare;
inline constexpr uint16_t DARK = ChessPalette::DarkSquare;

// Creates the 64 square entities + 64 highlight overlays. Squares are
// border-only (BOX_LINE) so they never cover the pieces with a filled
// interior. Returns the square entities in index order (a1=0 ... h8=63).
inline std::vector<Entity> createBoard(Registry &registry, ShapeService &shapes,
                                       std::vector<Entity> &outHighlights) {
  std::vector<Entity> squares;
  squares.reserve(64);
  outHighlights.clear();
  outHighlights.reserve(64);

  for (int index = 0; index < 64; ++index) {
    int file = index & 7;
    int rank = index >> 3;
    vec2 c = PieceShapes::squareCenterWorld(index);
    bool light = ((file + rank) & 1) == 1;

    // Square border (hollow box) rendered only for light squares.
    // Kept slightly smaller than cell size (0.46 * CELL) so neighbouring
    // diagonal boxes never touch or overlap.
    float hw = ChessConfig::CELL * 0.46f;
    float thickness = ChessConfig::CELL * 0.045f;
    Entity square = INVALID_ENTITY;
    if (light) {
      square = shapes.addShape({.shapeId = DefaultShapes::BOX_LINE,
                                .variables = {c.x, c.y, hw, hw, thickness},
                                .material = LIGHT,
                                .combination = CombinationType::Addition,
                                .hasCollision = false,
                                .group = 0});
      registry.setComponentDirty(registry.getComponent<CustomShape>(square));
    } else {
      square = registry.createEntity();
    }

    SquareComp &comp = registry.addComponent<SquareComp>(square);
    comp.file = file;
    comp.rank = rank;
    comp.index = index;
    registry.setComponentDirty(comp);

    // Highlight overlay (hidden off-screen until used).
    float hw2 = ChessConfig::CELL * 0.46f;
    float th = ChessConfig::CELL * 0.05f;
    Entity highlight =
        shapes.addShape({.shapeId = DefaultShapes::BOX_LINE,
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

// Rank labels (1-8) on the left of the board, file labels (a-h) below.
// Positions are world-space and provided by the caller (layoutSystem
// recomputes them on resize). Returns rank labels then file labels.
inline std::vector<Entity> createLabels(Registry &registry) {
  std::vector<Entity> out;
  out.reserve(16);

  auto makeLabel = [&](float x, float y, const std::string &text) {
    Entity e = registry.createEntity();
    auto &t = registry.addComponent<Transform>(e);
    t.position = vec3(x, y, 0.0f);

    auto &label = registry.addComponent<TextRenderer>(e);
    label.text = text;
    label.material = ChessPalette::PanelTextDim;
    label.horizontalAlignment = TextRenderer::HorizontalAlignment::Center;
    label.verticalAlignment = TextRenderer::VerticalAlignment::Center;
    registry.setComponentDirty(label);
    return e;
  };

  for (int rank = 0; rank < 8; ++rank)
    out.push_back(makeLabel(0.0f, 0.0f, std::to_string(rank + 1)));
  for (int file = 0; file < 8; ++file)
    out.push_back(
        makeLabel(0.0f, 0.0f, std::string(1, static_cast<char>('a' + file))));

  return out;
}

// Sets a highlight overlay visible at a square (or hides it when
// index < 0). Does NOT force a shader refresh - callers batch that
// once after updating a set of highlights (material changes are baked
// into the generated shader, so one refresh covers all of them).
inline void setHighlight(Registry &registry, Entity highlight, int squareIndex,
                         uint16_t material) {
  auto &shape = registry.getComponent<CustomShape>(highlight);
  if (squareIndex < 0) {
    shape.parameters[0] = -1000.0f;
    shape.parameters[1] = -1000.0f;
  } else {
    vec2 c = PieceShapes::squareCenterWorld(squareIndex);
    shape.parameters[0] = c.x;
    shape.parameters[1] = c.y;
  }
  shape.material = material;
  registry.setComponentDirty(shape);
}
} // namespace BoardShapes
} // namespace wchess
