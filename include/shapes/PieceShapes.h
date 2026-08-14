#pragma once

// Custom SDF shapes for the six piece types. Each shape is a union of simple
// primitives (boxes, circles, triangles, lines) parameterised by
//   parameters[0] = piece center x
//   parameters[1] = piece center y
//   parameters[2] = uniform scale (designed for s = 1.0 to fit a 0.95-wide
//                   footprint, i.e. ~62% of a chess square; the spawn scale
//                   is CELL-relative, see MoveSystem::PIECE_SCALE)
// The shapes are intentionally simple silhouettes - good enough to tell the
// pieces apart; they can be replaced later without touching game logic.

#include "chess/ChessTypes.h"
#include "config.h"
#include "globals.h"

namespace wchess {
namespace PieceShapes {
enum PieceShapeIdx {
  PAWN = 0,
  ROOK = 1,
  KNIGHT = 2,
  BISHOP = 3,
  QUEEN = 4,
  KING = 5,
  COUNT = 6
};

inline ShapeId s_ids[PieceShapeIdx::COUNT]{};

inline PieceShapeIdx indexFor(PieceType type) {
  switch (type) {
  case PieceType::Rook:
    return PieceShapeIdx::ROOK;
  case PieceType::Knight:
    return PieceShapeIdx::KNIGHT;
  case PieceType::Bishop:
    return PieceShapeIdx::BISHOP;
  case PieceType::Queen:
    return PieceShapeIdx::QUEEN;
  case PieceType::King:
    return PieceShapeIdx::KING;
  default:
    return PieceShapeIdx::PAWN;
  }
}

namespace detail {
// Small builder that anchors every sub-primitive to the piece's
// (x, y) center and scales it by s.
struct Builder {
  std::shared_ptr<IMathExpression> x = std::make_shared<FloatVariable>(0);
  std::shared_ptr<IMathExpression> y = std::make_shared<FloatVariable>(1);
  std::shared_ptr<IMathExpression> s = std::make_shared<FloatVariable>(2);

  std::shared_ptr<IMathExpression> px(float ox) const {
    return std::make_shared<Addition>(x,
                                      std::make_shared<Multiplication>(s, ox));
  }

  std::shared_ptr<IMathExpression> py(float oy) const {
    return std::make_shared<Addition>(y,
                                      std::make_shared<Multiplication>(s, oy));
  }

  std::shared_ptr<IMathExpression> scaled(float v) const {
    return std::make_shared<Multiplication>(s, v);
  }

  std::shared_ptr<IMathExpression> box(float ox, float oy, float w,
                                       float h) const {
    return std::make_shared<Primitives::Box>(px(ox), py(oy), scaled(w),
                                             scaled(h));
  }

  std::shared_ptr<IMathExpression> circle(float ox, float oy, float r) const {
    return std::make_shared<Primitives::Circle>(px(ox), py(oy), scaled(r));
  }

  std::shared_ptr<IMathExpression> line(float ax, float ay, float bx, float by,
                                        float width) const {
    return std::make_shared<Primitives::Line>(px(ax), py(ay), px(bx), py(by),
                                              scaled(width));
  }

  std::shared_ptr<IMathExpression> triangle(float ox, float oy, float w,
                                            float h) const {
    return std::make_shared<Primitives::Triangle>(
        px(ox), py(oy), scaled(w), scaled(h),
        std::make_shared<FloatConstant>(0.0f));
  }

  // union of two shapes with a soft seam
  std::shared_ptr<IMathExpression>
  softUnion(std::shared_ptr<IMathExpression> a,
            std::shared_ptr<IMathExpression> b) const {
    // smooth radius scales with the piece size so seams look
    // the same regardless of the world scale
    return std::make_shared<SDFSmoothAddition>(std::move(a), std::move(b),
                                               scaled(0.03f));
  }

  // hollow outline of a shape
  std::shared_ptr<IMathExpression> onion(std::shared_ptr<IMathExpression> a,
                                         float thickness) const {
    return std::make_shared<SDFOnion>(std::move(a), scaled(thickness));
  }
};
} // namespace detail

// Registers the six piece SDFs and stores their ShapeIds in s_ids[].
// All pieces share a unified, cohesive design language: identical pedestal
// bases, harmonious body tapers, consistent collars, and distinct iconic
// crowns/finials.
inline void registerAll(ShapeService &shapes) {
  detail::Builder b;

  // Unified base pedestal across all 6 pieces (half-width 0.38f, bottom at
  // -0.50f)
  constexpr float BASE_Y = -0.38f;
  constexpr float BASE_HW = 0.38f;
  constexpr float BASE_HH = 0.12f;

  auto makeBase = [&]() { return b.box(0.0f, BASE_Y, BASE_HW, BASE_HH); };

  // Pawn: base + tapered body + collar + spherical head
  {
    auto shape =
        b.softUnion(makeBase(), b.triangle(0.0f, -0.06f, 0.28f, 0.26f));
    shape = b.softUnion(shape, b.box(0.0f, 0.04f, 0.16f, 0.035f));
    shape = b.softUnion(shape, b.circle(0.0f, 0.18f, 0.18f));
    s_ids[PieceShapeIdx::PAWN] = shapes.registerSDF(shape);
  }

  // Rook: base + tower column + battlement parapet + merlons
  {
    auto shape = b.softUnion(makeBase(), b.box(0.0f, -0.06f, 0.28f, 0.24f));
    shape = b.softUnion(shape, b.box(0.0f, 0.24f, 0.36f, 0.09f));
    shape = b.softUnion(shape, b.box(-0.24f, 0.34f, 0.08f, 0.07f));
    shape = b.softUnion(shape, b.box(0.24f, 0.34f, 0.08f, 0.07f));
    shape = b.softUnion(shape, b.box(0.0f, 0.34f, 0.07f, 0.07f));
    s_ids[PieceShapeIdx::ROOK] = shapes.registerSDF(shape);
  }

  // Knight: base + angled horse neck + forward muzzle + pointed ear
  {
    auto shape =
        b.softUnion(makeBase(), b.triangle(-0.02f, 0.06f, 0.40f, 0.48f));
    shape = b.softUnion(shape, b.box(0.20f, 0.18f, 0.18f, 0.13f));
    shape = b.softUnion(shape, b.triangle(-0.10f, 0.34f, 0.10f, 0.12f));
    s_ids[PieceShapeIdx::KNIGHT] = shapes.registerSDF(shape);
  }

  // Bishop: base + body taper + collar + miter head + finial pearl
  {
    auto shape =
        b.softUnion(makeBase(), b.triangle(0.0f, -0.02f, 0.30f, 0.36f));
    shape = b.softUnion(shape, b.box(0.0f, 0.12f, 0.22f, 0.035f));
    shape = b.softUnion(shape, b.circle(0.0f, 0.26f, 0.17f));
    shape = b.softUnion(shape, b.circle(0.0f, 0.46f, 0.06f));
    s_ids[PieceShapeIdx::BISHOP] = shapes.registerSDF(shape);
  }

  // Queen: base + body taper + collar + flared coronet + crown pearl
  {
    auto shape = b.softUnion(makeBase(), b.triangle(0.0f, 0.00f, 0.32f, 0.40f));
    shape = b.softUnion(shape, b.box(0.0f, 0.18f, 0.26f, 0.04f));
    shape = b.softUnion(shape, b.triangle(0.0f, 0.32f, 0.36f, 0.15f));
    shape = b.softUnion(shape, b.circle(0.0f, 0.48f, 0.075f));
    s_ids[PieceShapeIdx::QUEEN] = shapes.registerSDF(shape);
  }

  // King: base + broad royal body + bold visible cross
  {
    auto shape = b.softUnion(makeBase(), b.triangle(0.0f, 0.02f, 0.38f, 0.40f));
    shape = b.softUnion(shape, b.box(0.0f, 0.34f, 0.24f, 0.07f));
    shape = b.softUnion(shape, b.box(0.0f, 0.42f, 0.07f, 0.17f));
    s_ids[PieceShapeIdx::KING] = shapes.registerSDF(shape);
  }
}

// World position of a square's center.
inline vec2 squareCenterWorld(int file, int rank) {
  return vec2((static_cast<float>(file) + 0.5f) * ChessConfig::CELL,
              (static_cast<float>(rank) + 0.5f) * ChessConfig::CELL);
}

inline vec2 squareCenterWorld(int index) {
  return squareCenterWorld(index & 7, index >> 3);
}

// Creates a piece entity at the given world position.
inline Entity spawnPiece(Registry &registry, ShapeService &shapes, Color color,
                         PieceType type, float x, float y, float scale) {
  Entity entity = registry.createEntity();

  auto &t = registry.addComponent<Transform>(entity);
  t.position = vec3(x, y, 0.0f);

  CustomShape &shape = registry.addComponent<CustomShape>(entity);
  shape.distanceFieldId = s_ids[indexFor(type)];
  shape.combination = CombinationType::Addition;
  shape.hasCollisions = false;
  shape.material = color == Color::White ? ChessPalette::WhitePiece
                                         : ChessPalette::BlackPiece;
  shape.parameters[0] = x;
  shape.parameters[1] = y;
  shape.parameters[2] = scale;
  registry.setComponentDirty(shape);

  return entity;
}

// Moves a piece entity to a world position (used by animations).
inline void setPiecePosition(Registry &registry, Entity entity,
                             const vec2 &worldPos) {
  auto &t = registry.getComponent<Transform>(entity);
  t.position = vec3(worldPos, 0.0f);
  registry.setComponentDirty(t);

  if (registry.hasComponent<CustomShape>(entity)) {
    auto &shape = registry.getComponent<CustomShape>(entity);
    shape.parameters[0] = worldPos.x;
    shape.parameters[1] = worldPos.y;
    registry.setComponentDirty(shape);
  }
}

// Removes the piece entity standing on a square (capture).
inline void destroyPiece(Registry &registry, Entity piece) {
  if (piece != INVALID_ENTITY)
    registry.destroyEntity(piece);
}
} // namespace PieceShapes
} // namespace wchess
