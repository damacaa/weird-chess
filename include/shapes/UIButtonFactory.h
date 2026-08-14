#pragma once

// Small factory for the right-panel UI: text entities, buttons and toggles,
// all in screen space (UIShape / UITextRenderer). Positions are set by the
// layout system on start and on every window resize.

#include "globals.h"

#include <string>

namespace wchess
{
	namespace UIButtonFactory
	{
		// Screen-space text entity.
		inline Entity createText(Registry& registry, float x, float y, const std::string& text, uint16_t material,
								 TextRenderer::HorizontalAlignment align = TextRenderer::HorizontalAlignment::Left,
								 TextRenderer::VerticalAlignment valign = TextRenderer::VerticalAlignment::Top)
		{
			Entity e = registry.createEntity();
			auto& t = registry.addComponent<Transform>(e);
			t.position = vec3(x, y, 0.0f);

			auto& uiText = registry.addComponent<UITextRenderer>(e);
			uiText.text = text;
			uiText.material = material;
			uiText.horizontalAlignment = align;
			uiText.verticalAlignment = valign;
			registry.setComponentDirty(uiText);
			return e;
		}

		// Screen-space shape (no interaction).
		inline Entity createShape(Registry& registry, ShapeService& shapes, ShapeId id,
								  const std::array<float, 8>& vars, uint16_t material)
		{
			return shapes.addUIShape(
				{.shapeId = id,
				 .variables = {vars[0], vars[1], vars[2], vars[3], vars[4], vars[5], vars[6], vars[7]},
				 .material = material,
				 .combination = CombinationType::Addition,
				 .group = 0});
		}

		// A clickable button: box outline with hover highlight. Returns the
		// entity; the caller stores it and reads ShapeButton::state each frame.
		inline Entity createButton(Registry& registry, ShapeService& shapes, float x, float y, float w, float h,
								   uint16_t material)
		{
			Entity e = shapes.addUIShape({.shapeId = DefaultShapes::BOX,
										  .variables = {x, y, w * 0.5f, h * 0.5f},
										  .material = material,
										  .combination = CombinationType::Addition,
										  .group = 0});

			auto& button = registry.addComponent<ShapeButton>(e);
			button.clickPadding = 14.0f;
			button.parameterModifierMask.set(2);
			button.parameterModifierMask.set(3);
			button.modifierAmount = 2.0f;
			return e;
		}

		// A toggle: a circle outline that flips `active` on click.
		inline Entity createToggle(Registry& registry, ShapeService& shapes, float x, float y, float r,
								   uint16_t material)
		{
			Entity e = shapes.addUIShape({.shapeId = DefaultShapes::CIRCLE_LINE,
										  .variables = {x, y, r, 4.0f},
										  .material = material,
										  .combination = CombinationType::Addition,
										  .group = 0});

			auto& toggle = registry.addComponent<ShapeToggle>(e);
			toggle.clickPadding = 14.0f;
			toggle.parameterModifierMask.set(3); // thickness grows while active
			toggle.modifierAmount = 4.0f;
			return e;
		}
	} // namespace UIButtonFactory
} // namespace wchess
