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
#include "components/PieceComp.h"
#include "config.h"
#include "globals.h"

namespace wchess
{
	namespace PieceShapes
	{
		enum PieceShapeIdx
		{
			PAWN = 0,
			ROOK = 1,
			KNIGHT = 2,
			BISHOP = 3,
			QUEEN = 4,
			KING = 5,
			COUNT = 6
		};

		inline ShapeId s_ids[PieceShapeIdx::COUNT]{};

		inline PieceShapeIdx indexFor(PieceType type)
		{
			switch (type)
			{
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


		inline void registerAll(ShapeService& shapes)
		{
			using namespace SDF;
			auto s = Expr(var(2));
			auto p = translate(worldPoint(), { Expr(var(0)), Expr(var(1)) });
			
			auto smooth = 0.03f * s;
			
			constexpr float BASE_Y = -0.38f;
			constexpr float BASE_HW = 0.38f;
			constexpr float BASE_HH = 0.12f;
			
			auto makeBase = [&]() { return sdBox(translate(p, { 0.0f, BASE_Y * s }), { BASE_HW * s, BASE_HH * s }); };

			// Pawn
			{
				auto shape = smoothUnion(makeBase(), sdTriangle(translate(p, { 0.0f, -0.06f * s }), 0.28f * s, 0.26f * s, 0.0f), smooth);
				shape = smoothUnion(shape, sdBox(translate(p, { 0.0f, 0.04f * s }), { 0.16f * s, 0.035f * s }), smooth);
				shape = smoothUnion(shape, sdCircle(translate(p, { 0.0f, 0.18f * s }), 0.18f * s), smooth);
				s_ids[PieceShapeIdx::PAWN] = shapes.registerSDF(shape.node);
			}

			// Rook
			{
				auto shape = smoothUnion(makeBase(), sdBox(translate(p, { 0.0f, -0.06f * s }), { 0.28f * s, 0.24f * s }), smooth);
				shape = smoothUnion(shape, sdBox(translate(p, { 0.0f, 0.24f * s }), { 0.36f * s, 0.09f * s }), smooth);
				shape = smoothUnion(shape, sdBox(translate(p, { -0.24f * s, 0.34f * s }), { 0.08f * s, 0.07f * s }), smooth);
				shape = smoothUnion(shape, sdBox(translate(p, { 0.24f * s, 0.34f * s }), { 0.08f * s, 0.07f * s }), smooth);
				shape = smoothUnion(shape, sdBox(translate(p, { 0.0f, 0.34f * s }), { 0.07f * s, 0.07f * s }), smooth);
				s_ids[PieceShapeIdx::ROOK] = shapes.registerSDF(shape.node);
			}

			// Knight
			{
				auto shape = smoothUnion(makeBase(), sdTriangle(translate(p, { -0.02f * s, 0.06f * s }), 0.40f * s, 0.48f * s, 0.0f), smooth);
				shape = smoothUnion(shape, sdBox(translate(p, { 0.20f * s, 0.18f * s }), { 0.18f * s, 0.13f * s }), smooth);
				shape = smoothUnion(shape, sdTriangle(translate(p, { -0.10f * s, 0.34f * s }), 0.10f * s, 0.12f * s, 0.0f), smooth);
				s_ids[PieceShapeIdx::KNIGHT] = shapes.registerSDF(shape.node);
			}

			// Bishop
			{
				auto shape = smoothUnion(makeBase(), sdTriangle(translate(p, { 0.0f, -0.02f * s }), 0.30f * s, 0.36f * s, 0.0f), smooth);
				shape = smoothUnion(shape, sdBox(translate(p, { 0.0f, 0.12f * s }), { 0.22f * s, 0.035f * s }), smooth);
				shape = smoothUnion(shape, sdCircle(translate(p, { 0.0f, 0.26f * s }), 0.17f * s), smooth);
				shape = smoothUnion(shape, sdCircle(translate(p, { 0.0f, 0.46f * s }), 0.06f * s), smooth);
				s_ids[PieceShapeIdx::BISHOP] = shapes.registerSDF(shape.node);
			}

			// Queen
			{
				auto shape = smoothUnion(makeBase(), sdTriangle(translate(p, { 0.0f, 0.00f }), 0.32f * s, 0.40f * s, 0.0f), smooth);
				shape = smoothUnion(shape, sdBox(translate(p, { 0.0f, 0.18f * s }), { 0.26f * s, 0.04f * s }), smooth);
				shape = smoothUnion(shape, sdTriangle(translate(p, { 0.0f, 0.32f * s }), 0.36f * s, 0.15f * s, 0.0f), smooth);
				shape = smoothUnion(shape, sdCircle(translate(p, { 0.0f, 0.48f * s }), 0.075f * s), smooth);
				s_ids[PieceShapeIdx::QUEEN] = shapes.registerSDF(shape.node);
			}

			// King
			{
				auto shape = smoothUnion(makeBase(), sdTriangle(translate(p, { 0.0f, 0.02f * s }), 0.38f * s, 0.40f * s, 0.0f), smooth);
				shape = smoothUnion(shape, sdBox(translate(p, { 0.0f, 0.34f * s }), { 0.24f * s, 0.07f * s }), smooth);
				shape = smoothUnion(shape, sdBox(translate(p, { 0.0f, 0.42f * s }), { 0.07f * s, 0.17f * s }), smooth);
				s_ids[PieceShapeIdx::KING] = shapes.registerSDF(shape.node);
			}
		}

		// World position of a square's center.
		inline vec2 squareCenterWorld(int file, int rank)
		{
			return vec2((static_cast<float>(file) + 0.5f) * ChessConfig::CELL,
						(static_cast<float>(rank) + 0.5f) * ChessConfig::CELL);
		}

		inline vec2 squareCenterWorld(int index)
		{
			return squareCenterWorld(index & 7, index >> 3);
		}

		// Creates a piece entity at the given world position.
		inline Entity spawnPiece(Registry& registry, ShapeService& shapes, Color color, PieceType type, float x,
								 float y, float scale)
		{
			Entity entity = registry.createEntity();

			auto& t = registry.addComponent<Transform>(entity);
			t.position = vec3(x, y, 0.0f);

			CustomShape& shape = registry.addComponent<CustomShape>(entity);
			shape.distanceFieldId = s_ids[indexFor(type)];
			shape.combination = CombinationType::Addition;
			shape.hasCollisions = false;
			shape.material = color == Color::White ? ChessPalette::WhitePiece : ChessPalette::BlackPiece;
			shape.parameters[0] = x;
			shape.parameters[1] = y;
			shape.parameters[2] = scale;
			registry.setComponentDirty(shape);

			return entity;
		}

		// Moves a piece entity to a world position (used by animations and positioning).
		inline void setPiecePosition(Registry& registry, Entity entity, const vec2& worldPos)
		{
			auto& t = registry.getComponent<Transform>(entity);
			t.position = vec3(worldPos, 0.0f);
			registry.setComponentDirty(t);

			if (registry.hasComponent<CustomShape>(entity))
			{
				auto& shape = registry.getComponent<CustomShape>(entity);
				shape.parameters[0] = worldPos.x;
				shape.parameters[1] = worldPos.y;
				registry.setComponentDirty(shape);
			}
		}

		// Moves a piece entity to a world position with scale (used by animations).
		inline void setPiecePositionAndScale(Registry& registry, Entity entity, const vec2& worldPos, float scale)
		{
			auto& t = registry.getComponent<Transform>(entity);
			t.position = vec3(worldPos, 0.0f);
			registry.setComponentDirty(t);

			if (registry.hasComponent<CustomShape>(entity))
			{
				auto& shape = registry.getComponent<CustomShape>(entity);
				shape.parameters[0] = worldPos.x;
				shape.parameters[1] = worldPos.y;
				shape.parameters[2] = scale;
				registry.setComponentDirty(shape);
			}
		}

		// Parks a piece entity off-screen (used for captures / promotions instead of destroying).
		inline void destroyPiece(Registry& registry, Entity piece)
		{
			if (piece != INVALID_ENTITY)
				setPiecePosition(registry, piece, vec2(-1000.0f, -1000.0f));
		}

		// Pre-allocates the 32 standard starting pieces parked off-screen at (-1000, -1000).
		inline void initPiecePool(Registry& registry, ShapeService& shapes, std::vector<Entity>& outPieces, float scale)
		{
			outPieces.clear();
			outPieces.reserve(32);

			auto addPiece = [&](Color color, PieceType type)
			{
				Entity entity = spawnPiece(registry, shapes, color, type, -1000.0f, -1000.0f, scale);
				auto& pc = registry.addComponent<PieceComp>(entity);
				pc.color = color;
				pc.type = type;
				pc.squareIndex = -1;
				registry.setComponentDirty(pc);
				outPieces.push_back(entity);
			};

			for (Color color : {Color::White, Color::Black})
			{
				// 8 starting Pawns
				for (int i = 0; i < 8; ++i)
					addPiece(color, PieceType::Pawn);
				// 2 starting Rooks
				for (int i = 0; i < 2; ++i)
					addPiece(color, PieceType::Rook);
				// 2 starting Knights
				for (int i = 0; i < 2; ++i)
					addPiece(color, PieceType::Knight);
				// 2 starting Bishops
				for (int i = 0; i < 2; ++i)
					addPiece(color, PieceType::Bishop);
				// 1 starting Queen
				addPiece(color, PieceType::Queen);
				// 1 King
				addPiece(color, PieceType::King);
			}
		}
	} // namespace PieceShapes
} // namespace wchess
