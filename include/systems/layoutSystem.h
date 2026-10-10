#pragma once

// Layout: computes the camera framing so the board fills the left half of the
// window and positions every right-panel UI entity in screen space. Mirrors
// weird-golfing's approach: a resolution hash is compared every frame and
// everything is repositioned when it changes. The weird-engine UI is not
// responsive by itself, so ALL screen-space placement is recomputed here.

#include "components/SessionState.h"
#include "components/StoryState.h"
#include "config.h"
#include "globals.h"
#include "shapes/UIButtonFactory.h"

namespace wchess
{
	namespace LayoutSystem
	{
		// px <-> world helpers for the current camera framing.
		inline float worldToScreenX(const vec3& cam, float halfH, float worldX)
		{
			return static_cast<float>(Display::width) * 0.5f + (worldX - cam.x) * halfH / cam.z;
		}

		inline float worldToScreenY(const vec3& cam, float halfH, float worldY)
		{
			return static_cast<float>(Display::height) * 0.5f + (worldY - cam.y) * halfH / cam.z;
		}

		inline float screenToWorldX(const vec3& cam, float halfH, float px)
		{
			return cam.x + (px - static_cast<float>(Display::width) * 0.5f) * cam.z / halfH;
		}

		inline float screenToWorldY(const vec3& cam, float halfH, float py)
		{
			return cam.y + (py - static_cast<float>(Display::height) * 0.5f) * cam.z / halfH;
		}

		// Recomputes the camera and every UI element. Called on start and
		// whenever the window size changes.
		inline void apply(Registry& registry, ServiceProvider& services)
		{
			ChessSessionState& session = getSessionState(registry);
			ChessStoryState& story = getStoryState(registry);

			const float width = static_cast<float>(Display::width);
			const float height = static_cast<float>(Display::height);
			const float halfW = width * 0.5f;
			const float halfH = height * 0.5f;

			// --- camera: fit the board in the left half ---
			// damaca: I ADDED A 2.0f multiplier to the Z distance to make the board
			// smaller and leave more room for the letters
			float camZ =
				1.0f * std::max(ChessConfig::BOARD_WORLD * halfH / (ChessConfig::BOARD_MAX_HEIGHT_RATIO * height),
								ChessConfig::BOARD_WORLD * halfH / (ChessConfig::BOARD_MAX_WIDTH_RATIO * halfW));
			// Board right edge (worldX = BOARD_WORLD) lands BOARD_RIGHT_MARGIN_PX
			// left of the screen centre.
			float camX = ChessConfig::BOARD_WORLD + ChessConfig::BOARD_RIGHT_MARGIN_PX * camZ / halfH;
			float camY = ChessConfig::BOARD_WORLD * 0.5f;

			vec3 cam(camX, camY, camZ);
			auto& camTransform = registry.getComponent<Transform>(services.render().getCameraEntity());
			camTransform.position = cam;
			registry.setComponentDirty(camTransform);

			// --- right panel (screen space) ---
			const float panelLeft = halfW + ChessConfig::PANEL_LEFT_MARGIN_PX;
			const float panelTop = height - ChessConfig::PANEL_TOP_MARGIN_PX;

			// Text pitch: UI glyph cell height (font cell height x 2 x dot radius)
			// plus a gap, so no two text lines ever overlap (the header stack below
			// must respect it too).
			auto& uiCtx = services.render().getContextUI();
			const float pitch = static_cast<float>(uiCtx.font.getCharHeight()) * 2.0f * uiCtx.dotRadious + 6.0f;

			if (story.titleText != INVALID_ENTITY)
			{
				auto& t = registry.getComponent<Transform>(story.titleText);
				t.position = vec3(panelLeft, panelTop, 0.0f);
				registry.setComponentDirty(t);
			}

			const float storyTop = panelTop - 1.5f * pitch;
			const float storyBottomMargin = 64.0f;
			const int visible = std::clamp(static_cast<int>((storyTop - storyBottomMargin) / pitch), 1,
										   static_cast<int>(story.storyLines.size()));
			story.storyVisibleLines = visible;
			for (size_t i = 0; i < story.storyLines.size(); ++i)
			{
				auto& t = registry.getComponent<Transform>(story.storyLines[i]);
				t.position = vec3(panelLeft,
								  (!session.awaitingPromotion && i < static_cast<size_t>(visible))
									  ? storyTop - static_cast<float>(i) * pitch
									  : -1000.0f,
								  0.0f);
				registry.setComponentDirty(t);
			}

			// --- buttons / toggles (bottom right, geometric shapes without text) ---
			const float buttonY = 32.0f;
			const float newGameX = width - 100.0f;
			const float toggleX = newGameX + 60.0f;
			if (story.newGameButton != INVALID_ENTITY)
			{
				auto& shape = registry.getComponent<UIShape>(story.newGameButton);
				shape.parameters[0] = newGameX;
				shape.parameters[1] = buttonY;
				registry.setComponentDirty(shape);
			}
			if (story.disableAIToggle != INVALID_ENTITY)
			{
				auto& shape = registry.getComponent<UIShape>(story.disableAIToggle);
				shape.parameters[0] = toggleX;
				shape.parameters[1] = buttonY;
				registry.setComponentDirty(shape);
			}

			// --- promotion modal (centered in the right panel when awaiting promotion) ---
			const float panelCenterX = panelLeft + (width - 32.0f - panelLeft) * 0.5f;
			const float promoSpacing = 100.0f;
			const float promoStartX = panelCenterX - 1.5f * promoSpacing;
			const float promoY = halfH;

			if (story.promoCard != INVALID_ENTITY)
			{
				auto& shape = registry.getComponent<UIShape>(story.promoCard);
				shape.parameters[0] = session.awaitingPromotion ? panelCenterX : -1000.0f;
				shape.parameters[1] = session.awaitingPromotion ? halfH : -1000.0f;
				registry.setComponentDirty(shape);
			}

			auto updatePromoSlot = [&](Entity btn, int index)
			{
				float posX =
					session.awaitingPromotion ? promoStartX + static_cast<float>(index) * promoSpacing : -1000.0f;
				float posY = session.awaitingPromotion ? promoY : -1000.0f;

				if (btn != INVALID_ENTITY)
				{
					auto& shape = registry.getComponent<UIShape>(btn);
					shape.parameters[0] = posX;
					shape.parameters[1] = posY;
					registry.setComponentDirty(shape);
				}
			};

			updatePromoSlot(story.promoQueenButton, 0);
			updatePromoSlot(story.promoRookButton, 1);
			updatePromoSlot(story.promoBishopButton, 2);
			updatePromoSlot(story.promoKnightButton, 3);

			story.layoutDirty = false;
			services.render().forceShaderRefresh2D();
			services.render().forceShaderRefreshUI();
		}

		inline void update(Registry& registry, ServiceProvider& services)
		{
			ChessSessionState& session = getSessionState(registry);
			ChessStoryState& story = getStoryState(registry);

			int hash = (Display::width << 16) ^ Display::height;
			static bool s_lastPromo = false;
			if (story.lastResolutionHash != hash || story.layoutDirty || s_lastPromo != session.awaitingPromotion)
			{
				s_lastPromo = session.awaitingPromotion;
				story.lastResolutionHash = hash;
				apply(registry, services);
			}
		}
	} // namespace LayoutSystem
} // namespace wchess
