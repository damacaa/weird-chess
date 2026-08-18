#pragma once

// Narrator seam: converts a move annotation into story text. Stage 1 uses
// PassThroughNarrator (engine annotations verbatim, no LLM). Stage 2 swaps in
// a LlamaNarrator that runs llama.cpp on the worker thread - the interface
// and threading stay identical. See narrator/llamacpp-integration.md.

#include "chess/ChessTypes.h"
#include "narrator/StoryStream.h"

#include <string>

namespace wchess
{
	class INarrator
	{
	public:
		virtual ~INarrator() = default;

		// Called on the worker thread. Implementations write prose chunks to
		// `out` (and may set the status). Never touch the ECS here.
		virtual void narrate(const MoveAnnotation& annotation, StoryStream& out) = 0;
		virtual void narrateIntro(StoryStream& out) = 0;

		virtual std::string name() const = 0;

		virtual void cancel() {}
		virtual void reset() {}
	};
} // namespace wchess
