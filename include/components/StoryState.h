#pragma once

// Story / UI scene state: the right-panel story text, the typewriter
// progress, the UI entity handles and the layout dirty bookkeeping.

#include "config.h"
#include "weird-engine/Assert.h"
#include "weird-engine/ecs/Entity.h"
#include "weird-engine/ecs/Registry.h"

#include <string>
#include <vector>

using WeirdEngine::Entity;
using WeirdEngine::INVALID_ENTITY;
using WeirdEngine::Registry;

namespace wchess
{
	struct ChessStoryState
	{
		// ---- story text + typewriter ----
		std::vector<Entity> storyLines;									 // one UITextRenderer per line
		int storyVisibleLines = ChessConfig::STORY_MAX_LINES;			 // set by layoutSystem
		std::vector<std::string> rawStoryChunks;						 // full history of raw story paragraphs
		std::vector<std::string> formattedStoryLines;					 // wrapped story lines
		float storyRevealedChars = 0.0f;								 // character reveal progress for typewriter
		size_t lastTotalStoryChars = 0;									 // total chars across wrapped lines
		float typewriterSpeed = ChessConfig::STORY_TYPEWRITER_MIN_SPEED; // chars/sec
		int lastWrapChars = 0;											 // last computed wrap width in characters

		// ---- UI entity handles ----
		Entity titleText = INVALID_ENTITY;
		Entity newGameButton = INVALID_ENTITY;
		Entity disableAIToggle = INVALID_ENTITY;
		Entity promoCard = INVALID_ENTITY;
		Entity promoQueenButton = INVALID_ENTITY;
		Entity promoRookButton = INVALID_ENTITY;
		Entity promoBishopButton = INVALID_ENTITY;
		Entity promoKnightButton = INVALID_ENTITY;

		// ---- layout ----
		int lastResolutionHash = 0;
		bool layoutDirty = false;
	};

	inline ChessStoryState& getStoryState(Registry& registry)
	{
		ChessStoryState* state = registry.getState<ChessStoryState>();
		WEIRD_ASSERT(state != nullptr, "ChessStoryState is missing: stateInitSystem must be the first start system");
		return *state;
	}
} // namespace wchess
