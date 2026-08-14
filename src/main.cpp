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

	// displaySettings.colorPalette[DisplaySettings::DefaultColors::Black] = vec4(0.22f, 0.22f, 0.27f, 1.0f);
	// displaySettings.colorPalette[DisplaySettings::DefaultColors::White] = vec4(0.93f, 0.93f, 0.90f, 1.0f);
	// displaySettings.colorPalette[DisplaySettings::DefaultColors::Gray] = vec4(0.52f, 0.52f, 0.58f, 1.0f);
	// displaySettings.colorPalette[DisplaySettings::DefaultColors::LightGray] = vec4(0.84f, 0.78f, 0.66f, 1.0f);
	// displaySettings.colorPalette[DisplaySettings::DefaultColors::Brown] = vec4(0.50f, 0.37f, 0.24f, 1.0f);

	PhysicsSettings physicsSettings{};
	physicsSettings.gravity = 0.0f;

	AudioSettings audioSettings{};
	audioSettings.mute = true;

	SceneManager& sceneManager = SceneManager::getInstance();
	sceneManager.registerScene<wchess::ChessScene>("chess");

	start(sceneManager, displaySettings, physicsSettings, audioSettings, argc, argv);
	return 0;
}
