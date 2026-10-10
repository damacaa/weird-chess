#pragma once

// Friction audio proxy. The engine no longer exposes a manual friction level:
// the audio engine synthesizes friction voices only from real rigidbody-vs-shape
// contacts. Chess pieces are kinematic SDF shapes with no rigidbody, so this rig
// provides a physical source: an invisible body pressed against an off-screen
// rail. AnimationSystem sets its tangential speed to the desired friction level
// and the engine turns the resulting contact into a continuous friction voice.

#include "components/BoardState.h"
#include "globals.h"

namespace wchess
{
	namespace FrictionRig
	{
		// Simulation2D models every rigidbody as a circle of fixed diameter
		// (m_diameter = 1.0f), shared by all bodies.
		inline constexpr float BALL_RADIUS = 0.5f;
		// Tiny overlap that keeps the contact alive without triggering an impact
		// (Scene only emits an impact when intensity > 0.02).
		inline constexpr float CONTACT_PENETRATION = 0.001f;
		// Far above the board so the rail never enters the camera view, whatever
		// the window aspect ratio. Chess runs with spatial audio disabled, so the
		// off-screen position does not attenuate the voice.
		inline constexpr float RAIL_X = 0.0f;
		inline constexpr float RAIL_Y = 1000.0f;
		inline constexpr float RAIL_HALF_WIDTH = 40.0f;
		inline constexpr float RAIL_HALF_HEIGHT = 4.0f;
		inline constexpr float RAIL_TOP_Y = RAIL_Y + RAIL_HALF_HEIGHT;
		// Scene derives the friction level as
		// friction(0.05) * 0.08 * multiplier(0.25) * speed = 0.001 * speed,
		// so this speed reproduces the old 0..1 manual level one-to-one.
		inline constexpr float SPEED_PER_LEVEL = 1000.0f;

		inline void create(Registry& registry, ServiceProvider& services)
		{
			ChessBoardState& board = getBoardState(registry);
			board.frictionRail =
				services.shapes().addShape({.shapeId = DefaultShapes::Box,
											.variables = {{DefaultShapes::Box::PosX, RAIL_X},
														  {DefaultShapes::Box::PosY, RAIL_Y},
														  {DefaultShapes::Box::SizeX, RAIL_HALF_WIDTH},
														  {DefaultShapes::Box::SizeY, RAIL_HALF_HEIGHT}},
											.material = ChessPalette::PANEL_TEXT_DIM_MATERIAL_IDX,
											.hasCollision = true});

			// No Shape or Dot component: the proxy is never rendered, only
			// simulated against the rail.
			board.frictionBall = registry.createEntity();
			auto& transform = registry.addComponent<Transform>(board.frictionBall);
			transform.position = vec3(RAIL_X, RAIL_TOP_Y + BALL_RADIUS - CONTACT_PENETRATION, 0.0f);
			registry.addComponent<RigidBody2D>(board.frictionBall);
		}

		// level: 0.0 (silent) to 1.0 (full-scale friction voice)
		inline void setLevel(Registry& registry, float level)
		{
			ChessBoardState& board = getBoardState(registry);
			if (board.frictionBall == INVALID_ENTITY)
				return;

			auto& rb = registry.getComponent<RigidBody2D>(board.frictionBall);
			rb.velocity = vec2(std::clamp(level, 0.0f, 1.0f) * SPEED_PER_LEVEL, 0.0f);
			registry.setComponentDirty(rb);

			// Re-pin the proxy on the rail every frame. The reported velocity
			// drives the friction voice while the position keeps the contact
			// continuous: no START/END flicker, no impact sounds, no drift.
			auto& transform = registry.getComponent<Transform>(board.frictionBall);
			transform.position.x = RAIL_X;
			transform.position.y = RAIL_TOP_Y + BALL_RADIUS - CONTACT_PENETRATION;
			registry.setComponentDirty(transform);
		}
	} // namespace FrictionRig
} // namespace wchess
