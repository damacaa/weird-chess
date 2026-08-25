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
			auto p = translate(worldPoint(), { Expr(var(0)), Expr(var(1)) }) / s;
			
			constexpr float smooth = 0.03f;
			
			constexpr float BASE_Y = -0.38f;
			constexpr float BASE_HW = 0.38f;
			constexpr float BASE_HH = 0.08f;
			
			auto makeBase = [&]() { return sdBox(translate(p, { 0.0f, BASE_Y }), { BASE_HW, BASE_HH }); };

			// Pawn
			{
				auto shape = makeBase();
				auto phase = var(0) * (1.5123f + var(1));
				auto t = sin(1.0f * time() + phase);
				auto displacement = 0.1f * t * t;
				shape = sdfUnion(shape, sdCircle(translate(p, {0.0f, displacement + 0.1f}), 0.21f));
				shape = sdfUnion(shape, sdBox(translate(p, {0.0f, displacement - 0.15f}), {0.2f, 0.025f}));

				shape =
					sdfSmoothUnion(shape, sdTriangle(translate(p, {0.0f, -0.15f}), 0.3f, 0.35f), 5.0f * smooth);

				s_ids[PieceShapeIdx::PAWN] = shapes.registerSDF((shape * s).node);
			}

			// Rook
			{
				auto shape = makeBase();

				// Stepped plinth
				auto plinth = sdBox(translate(p, { 0.0f, -0.27f }), { 0.30f, 0.03f });

				// Tapered tower shaft (column)
				auto towerCone = sdTriangle(translate(p, { 0.0f, 0.34f }), 0.65f, 1.74f);
				auto towerBounds = sdBox(translate(p, { 0.0f, -0.035f }), { 0.25f, 0.205f });
				auto column = sdfIntersect(towerCone, towerBounds);

				// Capital collar ledge & parapet base
				auto collar = sdBox(translate(p, { 0.0f, 0.19f }), { 0.28f, 0.02f });
				auto parapetLedge = sdBox(translate(p, { 0.0f, 0.245f }), { 0.30f, 0.035f });

				// Modulo repeating boxes for battlements, moving horizontally with time
				constexpr float SPACING = 0.22f;
				auto animatedX = p.x + 0.1f * time();
				auto modX = mod(animatedX + 0.5f * SPACING, SPACING) - 0.5f * SPACING;
				auto repeatingBoxes = sdBox({ modX, p.y - 0.33f }, { 0.07f, 0.055f });

				// Intersect repeating boxes with the tower width bounding box
				auto towerWidthBox = sdBox(translate(p, { 0.0f, 0.33f }), { 0.30f, 0.055f });
				auto movingBattlements = sdfIntersect(repeatingBoxes, towerWidthBox);

				auto battlements = sdfUnion(parapetLedge, movingBattlements);

				// Assemble: solid base and column, crisp crown, and a single smooth blend at the collar
				auto castleBase = sdfUnion(sdfUnion(shape, plinth), column);
				auto castleCrown = sdfUnion(collar, battlements);
				shape = sdfSmoothUnion(castleBase, castleCrown, smooth);

				s_ids[PieceShapeIdx::ROOK] = shapes.registerSDF((shape * s).node);
			}

			// Knight
			{
				auto shape = makeBase();
				
				// Head
				auto headCoords = p;
				headCoords = rotate(headCoords, 0.2f);
				headCoords = translate(headCoords, { 0.0f, 0.32f });
				shape = sdfUnion(shape, sdBox(headCoords, { 0.25f, 0.12f }));
				
				// Neck
				auto neckCoords = translate(p, {-0.19f, -0.0f});
				auto neck = sdTriangle(neckCoords, 0.20f, 0.78f);
				neck = sdfSmoothUnion(neck, sdCircle(translate(p, {0.2f, -0.1f}), 0.05f), 28.0f * smooth);

				neck = sdfSubtract(neck, sdCircle(translate(p, {-0.4f, -0.3f}), 0.2f));

				shape = sdfSmoothUnion(shape, neck, 3.0f * smooth);

				s_ids[PieceShapeIdx::KNIGHT] = shapes.registerSDF((shape * s).node);
			}

			// Bishop
			{
				auto phase = var(0) * (1.5123f + var(1));
				auto sway = sin(1.5f * time() + phase);
				auto bodyAngle = 0.08f * sway;
				auto headCounterAngle = -2.0f * bodyAngle;

				// 1. Body coordinate frame (pivoted near base at y = -0.33)
				auto bodyP = rotate(translate(p, { 0.0f, -0.33f }), bodyAngle);
				auto body = sdTriangle(translate(bodyP, { 0.0f, 0.23f }), 0.40f, 0.70f);
				auto collar = sdBox(translate(bodyP, { 0.0f, 0.45f }), { 0.22f, 0.035f });

				auto shape = sdfSmoothUnion(makeBase(), body, 10.0f * smooth);
				shape = sdfSmoothUnion(shape, collar, smooth);

				// 2. Chained head coordinate frame (pivoted at collar on the body, tilting opposite)
				auto headP = rotate(translate(bodyP, { 0.0f, 0.45f }), headCounterAngle);
				auto headCircle = sdCircle(translate(headP, { 0.0f, 0.14f }), 0.17f);
				auto topBall = sdCircle(translate(headP, { 0.0f, 0.34f }), 0.06f);

				shape = sdfSmoothUnion(shape, headCircle, smooth);
				shape = sdfSmoothUnion(shape, topBall, smooth);

				s_ids[PieceShapeIdx::BISHOP] = shapes.registerSDF((shape * s).node);
			}

			// Crown
			constexpr float CROWN_Y = 0.4f;
			constexpr float CROWN_W = 0.5f;
			constexpr float CROWN_H = 0.4f;

			// Queen
			{
				auto shape = makeBase();
				auto body = sdTriangle(translate(p, {0.0f, 0.0f}), 0.35f, 0.90f);
				shape = sdfSmoothUnion(shape, body, 10.0f * smooth);

				// Crown
				constexpr float CROWN_WAVE_AMP = 0.04f;

				auto crownP = translate(p, {0.0f, CROWN_Y});
				auto crown = sdTriangle(rotate(crownP, 3.14159265f), CROWN_W, CROWN_H);
				auto waveAmp = CROWN_WAVE_AMP;
				auto waveOffset = (CROWN_H / 3.0f) - waveAmp * 0.5f;
				auto wave = sdSineWave(crownP, waveAmp, 35.0f, 4.0f, waveOffset);
				crown = sdfIntersect(crown, wave);

				auto crownCircle = sdCircle(translate(crownP, {0.0f, CROWN_H / 3.0f + 0.017f}), 0.075f);
				crown = sdfSmoothUnion(crown, crownCircle, smooth);

				auto crownBase = sdBox(translate(crownP, {0.0f, -0.15f}), {0.25f, 0.01f});
				crown = sdfSmoothUnion(crown, crownBase, 5.0f * smooth);

				shape = sdfUnion(shape, crown);

				s_ids[PieceShapeIdx::QUEEN] = shapes.registerSDF((shape * s).node);
			}

			// King
			{
				auto shape = makeBase();
				auto body = sdTriangle(translate(p, {0.0f, 0.0f}), 0.35f, 0.90f);
				shape = sdfSmoothUnion(shape, body, 10.0f * smooth);

				constexpr float CROSS_W = 0.05f;

				auto crownP = translate(p, {0.0f, CROWN_Y});
				auto crown = sdTriangle(rotate(crownP, 3.14159265f), CROWN_W, CROWN_H);
				auto crownHole = sdCircle(translate(crownP, {0.0f, CROWN_H / 3.0f + 0.1f}), 0.15f);
				crown = sdfSubtract(crown, crownHole);

				auto cross = sdStar(translate(crownP, {0.0f, CROWN_H / 3.0f + 0.1f}), CROSS_W, CROSS_W, 4.0f, 5.0f);
				crown = sdfSmoothUnion(crown, cross, smooth);

				auto crownBase = sdBox(translate(crownP, {0.0f, -0.15f}), {0.25f, 0.01f});

				crown = sdfSmoothUnion(crown, crownBase, 5.0f * smooth);

				shape = sdfUnion(shape, crown);

				s_ids[PieceShapeIdx::KING] = shapes.registerSDF((shape * s).node);
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
