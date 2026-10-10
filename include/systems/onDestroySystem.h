#pragma once

// onDestroy: stops the narrator worker thread and shuts down the AI
// subprocess cleanly so the app never leaks threads or processes.

#include "components/SessionState.h"
#include "globals.h"

namespace wchess
{
	inline void onDestroySystem(Registry& registry, ServiceProvider& services)
	{
		ChessSessionState& session = getSessionState(registry);

		if (session.narrator)
		{
			session.narrator->stop();
			session.narrator.reset();
		}
		session.narratorImpl.reset();

		if (session.annotator)
		{
			session.annotator->stop();
			session.annotator.reset();
		}

		if (session.ai)
		{
			session.ai->shutdown();
			session.ai.reset();
		}
		session.board.reset();

		std::cout << "[WeirdChess] scene destroyed" << std::endl;
	}
} // namespace wchess
