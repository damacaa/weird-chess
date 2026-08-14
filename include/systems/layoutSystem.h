#pragma once

// Layout: computes the camera framing so the board fills the left half of the
// window, positions the world-space rank/file labels and every right-panel UI
// entity in screen space. Mirrors weird-golfing's approach: a resolution hash
// is compared every frame and everything is repositioned when it changes.
// The weird-engine UI is not responsive by itself, so ALL screen-space
// placement is recomputed here.

#include "components/ChessState.h"
#include "config.h"
#include "globals.h"
#include "shapes/UIButtonFactory.h"

namespace wchess {
namespace LayoutSystem {
// px <-> world helpers for the current camera framing.
inline float worldToScreenX(const vec3 &cam, float halfH, float worldX) {
  return static_cast<float>(Display::width) * 0.5f +
         (worldX - cam.x) * halfH / cam.z;
}

inline float worldToScreenY(const vec3 &cam, float halfH, float worldY) {
  return static_cast<float>(Display::height) * 0.5f +
         (worldY - cam.y) * halfH / cam.z;
}

inline float screenToWorldX(const vec3 &cam, float halfH, float px) {
  return cam.x +
         (px - static_cast<float>(Display::width) * 0.5f) * cam.z / halfH;
}

inline float screenToWorldY(const vec3 &cam, float halfH, float py) {
  return cam.y +
         (py - static_cast<float>(Display::height) * 0.5f) * cam.z / halfH;
}

// Recomputes the camera and every UI element. Called on start and
// whenever the window size changes.
inline void apply(ChessState &state, Registry &registry,
                  ServiceProvider &services) {
  const float width = static_cast<float>(Display::width);
  const float height = static_cast<float>(Display::height);
  const float halfW = width * 0.5f;
  const float halfH = height * 0.5f;

  // --- camera: fit the board in the left half ---
  // damaca: I ADDED A 2.0f multiplier to the Z distance to make the board
  // smaller and leave more room for the letters
  float camZ =
      1.0f * std::max(ChessConfig::BOARD_WORLD * halfH /
                          (ChessConfig::BOARD_MAX_HEIGHT_RATIO * height),
                      ChessConfig::BOARD_WORLD * halfH /
                          (ChessConfig::BOARD_MAX_WIDTH_RATIO * halfW));
  // Board right edge (worldX = BOARD_WORLD) lands BOARD_RIGHT_MARGIN_PX
  // left of the screen centre.
  float camX = ChessConfig::BOARD_WORLD +
               ChessConfig::BOARD_RIGHT_MARGIN_PX * camZ / halfH;
  float camY = ChessConfig::BOARD_WORLD * 0.5f;

  vec3 cam(camX, camY, camZ);
  auto &camTransform =
      registry.getComponent<Transform>(services.render().getCameraEntity());
  camTransform.position = cam;
  registry.setComponentDirty(camTransform);

  // --- world-space rank/file labels ---
  // 8 rank labels then 8 file labels (order matches creation).
  const float boardLeftPx = worldToScreenX(cam, halfH, 0.0f);
  const float boardBottomPx = worldToScreenY(cam, halfH, 0.0f);
  const float rankLabelPx =
      std::max(6.0f, boardLeftPx - ChessConfig::RANK_LABEL_GAP_PX);
  const float fileLabelPx =
      std::max(6.0f, boardBottomPx - ChessConfig::FILE_LABEL_GAP_PX);

  for (int rank = 0; rank < 8; ++rank) {
    float wx = screenToWorldX(cam, halfH, rankLabelPx);
    float wy =
        static_cast<float>(rank) * ChessConfig::CELL + ChessConfig::CELL * 0.5f;
    Entity e = state.rankLabels[rank];
    if (e == INVALID_ENTITY)
      continue;
    auto &t = registry.getComponent<Transform>(e);
    t.position = vec3(wx, wy, 0.0f);
    registry.setComponentDirty(t);
  }
  for (int file = 0; file < 8; ++file) {
    float wx =
        static_cast<float>(file) * ChessConfig::CELL + ChessConfig::CELL * 0.5f;
    float wy = screenToWorldY(cam, halfH, fileLabelPx);
    Entity e = state.fileLabels[file];
    if (e == INVALID_ENTITY)
      continue;
    auto &t = registry.getComponent<Transform>(e);
    t.position = vec3(wx, wy, 0.0f);
    registry.setComponentDirty(t);
  }

  // --- right panel (screen space) ---
  const float panelLeft = halfW + ChessConfig::PANEL_LEFT_MARGIN_PX;
  const float panelTop = height - ChessConfig::PANEL_TOP_MARGIN_PX;

  // Text pitch: UI glyph cell height (font cell height x 2 x dot radius)
  // plus a gap, so no two text lines ever overlap (the header stack below
  // must respect it too).
  auto &uiCtx = services.render().getContextUI();
  const float pitch =
      static_cast<float>(uiCtx.font.getCharHeight()) * 2.0f * uiCtx.dotRadious +
      6.0f;

  if (state.titleText != INVALID_ENTITY) {
    auto &t = registry.getComponent<Transform>(state.titleText);
    t.position = vec3(panelLeft, panelTop, 0.0f);
    registry.setComponentDirty(t);
  }
  if (state.storyTitle != INVALID_ENTITY) {
    auto &t = registry.getComponent<Transform>(state.storyTitle);
    t.position = vec3(panelLeft, panelTop - pitch, 0.0f);
    registry.setComponentDirty(t);
  }
  if (state.statusText != INVALID_ENTITY) {
    auto &t = registry.getComponent<Transform>(state.statusText);
    t.position = vec3(panelLeft, panelTop - 2.0f * pitch, 0.0f);
    registry.setComponentDirty(t);
  }
  if (state.storyStatus != INVALID_ENTITY) {
    auto &t = registry.getComponent<Transform>(state.storyStatus);
    t.position = vec3(panelLeft, panelTop - 3.0f * pitch, 0.0f);
    registry.setComponentDirty(t);
  }

  const float storyTop = panelTop - 1.5f * pitch;
  const float storyBottomMargin = 24.0f;
  const int visible =
      std::clamp(static_cast<int>((storyTop - storyBottomMargin) / pitch), 1,
                 ChessConfig::STORY_MAX_LINES);
  state.storyVisibleLines = visible;
  for (size_t i = 0; i < state.storyLines.size(); ++i) {
    auto &t = registry.getComponent<Transform>(state.storyLines[i]);
    t.position = vec3(panelLeft,
                      i < static_cast<size_t>(visible)
                          ? storyTop - static_cast<float>(i) * pitch
                          : -1000.0f,
                      0.0f);
    registry.setComponentDirty(t);
  }

  // --- buttons / toggles (bottom right) ---
  // Controls sit side by side; their labels hang below them. The columns are
  // spaced so the labels never overlap ("NEW GAME" ~128px, "STRONG AI" ~144px
  // wide at the compact UI font).
  const float buttonY = 44.0f;
  const float labelY = buttonY - 12.0f; // 3px below the 18px-tall button box
  const float newGameX = panelLeft + 40.0f;
  const float toggleX = panelLeft + 200.0f;
  if (state.newGameButton != INVALID_ENTITY) {
    auto &shape = registry.getComponent<UIShape>(state.newGameButton);
    shape.parameters[0] = newGameX;
    shape.parameters[1] = buttonY;
    registry.setComponentDirty(shape);
  }
  if (state.newGameLabel != INVALID_ENTITY) {
    auto &t = registry.getComponent<Transform>(state.newGameLabel);
    t.position = vec3(newGameX, labelY, 0.0f);
    registry.setComponentDirty(t);
  }
  if (state.disableAIToggle != INVALID_ENTITY) {
    auto &shape = registry.getComponent<UIShape>(state.disableAIToggle);
    shape.parameters[0] = toggleX;
    shape.parameters[1] = buttonY;
    registry.setComponentDirty(shape);
  }
  if (state.disableAILabel != INVALID_ENTITY) {
    auto &t = registry.getComponent<Transform>(state.disableAILabel);
    t.position = vec3(toggleX, labelY, 0.0f);
    registry.setComponentDirty(t);
  }

  // --- move log (top-left, above the board) ---
  if (state.moveLogText != INVALID_ENTITY) {
    auto &t = registry.getComponent<Transform>(state.moveLogText);
    t.position = vec3(10.0f, height - 14.0f, 0.0f);
    registry.setComponentDirty(t);
  }

  services.render().forceShaderRefresh2D();
  services.render().forceShaderRefreshUI();
}

inline void update(Registry &registry, ServiceProvider &services) {
  ChessState &state = getState(registry);

  int hash = Display::width + Display::height;
  if (state.lastResolutionHash != hash) {
    state.lastResolutionHash = hash;
    apply(state, registry, services);
  }
}
} // namespace LayoutSystem
} // namespace wchess
