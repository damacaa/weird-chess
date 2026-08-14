#pragma once

// The ChessScene: a thin Scene2D that only registers systems with the
// dispatcher. All game logic lives in the free functions under systems/ -
// nothing is implemented inline here (unlike the old weird-golfing style).
//
// Registered systems (run in registration order):
//   onCreate    : state entity + physics pause
//   onStart     : board, pieces, UI, narrator thread, AI
//   onUpdate    : input -> ai -> animation -> narrative -> layout -> ui
//   onDestroy   : narrator + AI shutdown
//   ImGui       : debug overlay

#include "globals.h"
#include "systems/aiSystem.h"
#include "systems/animationSystem.h"
#include "systems/annotationSystem.h"
#include "systems/imGuiSystem.h"
#include "systems/inputSystem.h"
#include "systems/layoutSystem.h"
#include "systems/narrativeRenderSystem.h"
#include "systems/onCreateSystem.h"
#include "systems/onDestroySystem.h"
#include "systems/onStartBoardSystem.h"
#include "systems/uiSystem.h"

namespace wchess
{
	class ChessScene : public Scene2D
	{
	public:
		ChessScene()
		{
			// ---- lifecycle ----
			addCreateSystem(onCreateSystem);
			addStartSystem(onStartBoardSystem);

			// ---- per frame, in order ----
			addUpdateSystem(InputSystem::update);
			addUpdateSystem(UISystem::update);
			addUpdateSystem(AnnotationSystem::update); // publish finished annotations
			addUpdateSystem(AISystem::update);		   // replies only after annotation + animation
			addUpdateSystem(AnimationSystem::update);
			addUpdateSystem(NarrativeRenderSystem::update);
			addUpdateSystem(LayoutSystem::update);

			// ---- teardown ----
			addDestroySystem(onDestroySystem);

			// ---- debug overlay ----
			addImGuiRenderSystem(imGuiSystem);
		}
	};
} // namespace wchess
