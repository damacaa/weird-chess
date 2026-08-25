#include "globals.h"
#include "scenes/ChessScene.h"

// Shared camera position (defined here so every scene can read it).
WeirdEngine::vec3 g_cameraPositon = WeirdEngine::vec3(120.0f, 60.0f, 80.0f);

int main(int argc, char* argv[])
{
	DisplaySettings displaySettings;
	displaySettings.width = 1280;
	displaySettings.height = 720;
	displaySettings.windowTitle = "Weird Chess";
	displaySettings.vSyncEnabled = true;
	displaySettings.distanceSampleScale = 0.5f;
	displaySettings.internalResolutionScale = 1.0f;
	displaySettings.enableMaterialBlending = true;
	displaySettings.uiSmoothFactor = 10.0f;
	displaySettings.enable2DLigthing = true;

	PhysicsSettings physicsSettings{};
	physicsSettings.gravity = 0.0f;

	AudioSettings audioSettings{};
	audioSettings.enableAmbient = false;

	SceneManager& sceneManager = SceneManager::getInstance();
	sceneManager.registerScene<wchess::ChessScene>("chess");

	start(sceneManager, displaySettings, physicsSettings, audioSettings, argc, argv);
	return 0;
}
