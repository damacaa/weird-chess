#pragma once

// onDestroy: stops the narrator worker thread and shuts down the AI
// subprocess cleanly so the app never leaks threads or processes.

#include "components/ChessState.h"
#include "globals.h"

namespace wchess
{
	inline void onDestroySystem(Registry& registry, ServiceProvider& services)
	{
		ChessState& state = getState(registry);

		if (state.narrator)
		{
			state.narrator->stop();
			state.narrator.reset();
		}
		state.narratorImpl.reset();

		if (state.annotator)
		{
			state.annotator->stop();
			state.annotator.reset();
		}

		if (state.ai)
		{
			state.ai->shutdown();
			state.ai.reset();
		}
		state.board.reset();

		std::cout << "[WeirdChess] scene destroyed" << std::endl;
	}
} // namespace wchess
