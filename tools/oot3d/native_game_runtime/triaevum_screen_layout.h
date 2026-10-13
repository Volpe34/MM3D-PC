#pragma once

// Window layout of the two 3DS screens for titles whose interface needs the
// lower screen (Majora's Mask 3D). OoT3D keeps its own TopScreen presentation.

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

enum class TriAevumScreenLayout : uint8_t {
  SingleScreen,       // upper screen only, lower-screen HUD parts on it, the
                      // lower screen as an overlay on request
  TopWithLowerInset,  // upper screen fills the window, lower in a corner
  SideBySide,         // both screens at the same height
  LowerFocus,         // lower screen fills the window
  TopOnly,            // upper screen only
  Count,
};

// A rectangle as fractions of the window.
struct TriAevumScreenRect {
  float X = 0.0F;
  float Y = 0.0F;
  float Width = 0.0F;
  float Height = 0.0F;
};

// Part of the lower screen (Source, fractions of it) drawn at Destination
// (fractions of the window) over the upper screen.
struct TriAevumScreenCrop {
  TriAevumScreenRect Source;
  TriAevumScreenRect Destination;
};

struct TriAevumScreenPlacements {
  std::optional<TriAevumScreenRect> Upper;  // empty: not shown
  std::optional<TriAevumScreenRect> Lower;
  bool LowerOverUpper = false;  // drawn on top of the upper screen
  std::vector<TriAevumScreenCrop> LowerCrops;
  // Parts of the upper screen's UI layer (menus composed on one screen).
  std::vector<TriAevumScreenCrop> UpperCrops;
  // Backdrops cut from the presented upper screen, drawn opaque first.
  std::vector<TriAevumScreenCrop> UpperFills;
};

// Lower-screen HUD parts shown on the upper screen in the single-screen
// layout, in lower-screen pixels (320x240) and where they go.
struct TriAevumHudPart {
  float SourceX0, SourceY0, SourceX1, SourceY1;
  float DestinationX, DestinationY;  // fractions of the window (top-left)
  float DestinationWidth;            // fraction of the window height
  // Cut from the upper screen's UI layer (400x240 pixels) instead of the
  // lower screen; FullWidth spans the upper screen's width.
  bool Upper = false;
  bool FullWidth = false;
  // Stretched over the whole upper screen as an opaque backdrop (Upper: from
  // the presented upper screen, not its UI layer).
  bool Fill = false;
  // DestinationX in upper-screen heights (a place that keeps its distance
  // from the edge whatever the aspect ratio).
  bool HeightUnitsX = false;
};
void SetTriAevumHudParts(std::vector<TriAevumHudPart> parts);
// Parts of both screens composed into one for menus (single screen).
void SetTriAevumMenuParts(std::vector<TriAevumHudPart> parts);

inline constexpr float kTriAevumUpperScreenWidth = 400.0F;
inline constexpr float kTriAevumLowerScreenWidth = 320.0F;
inline constexpr float kTriAevumScreenHeight = 240.0F;

// What the single-screen layout shows besides the upper screen.
struct TriAevumSingleScreenState {
  bool LowerOverlay = false;  // the lower screen large in the middle (F)
  // 0..1 while a menu opens: the window becomes both screens side by side
  // (the lower one slides in from the right).
  float MenuBlend = 0.0F;
  // 0..1: the lower screen as an inset at the right edge (ocarina songs).
  float InsetBlend = 0.0F;
  // A menu composed on one screen from both screens' UI layers.
  bool MergedMenu = false;
};

// upperAspect: width/height the upper screen is shown at (5:3 natively, the
// window's aspect when widescreen).
TriAevumScreenPlacements ResolveTriAevumScreenPlacements(
    TriAevumScreenLayout layout, uint32_t windowWidth, uint32_t windowHeight,
    float insetScale, float upperAspect,
    const TriAevumSingleScreenState &single = {});

// A tap on the lower screen requested by a keyboard shortcut, held for a few
// input polls so the guest sees press and release.
void RequestTriAevumShortcutTouch(uint16_t x, uint16_t y) noexcept;
// Press at one point, slide to another and release (drag-and-drop on the touch screen).
void RequestTriAevumShortcutDrag(uint16_t fromX, uint16_t fromY, uint16_t toX,
                                 uint16_t toY) noexcept;
std::optional<std::pair<uint16_t, uint16_t>> TakeTriAevumShortcutTouch() noexcept;

TriAevumScreenLayout NextTriAevumScreenLayout(TriAevumScreenLayout layout) noexcept;
std::string_view TriAevumScreenLayoutName(TriAevumScreenLayout layout) noexcept;
std::optional<TriAevumScreenLayout>
ParseTriAevumScreenLayout(std::string_view name) noexcept;

// Where the lower screen currently is, for mapping the mouse to touch. Written
// by the presentation loop and read by input polling on the same thread.
void SetTriAevumLowerScreenTouchRect(std::optional<TriAevumScreenRect> rect) noexcept;
// Lower-screen parts shown elsewhere (single screen): touch through them.
void SetTriAevumLowerScreenTouchCrops(std::vector<TriAevumScreenCrop> crops);
// The lower-screen point (fractions) under a window point (fractions), if
// one of the parts shows it.
std::optional<std::pair<float, float>> TriAevumLowerScreenPointThroughCrops(float x, float y);
std::optional<TriAevumScreenRect> TriAevumLowerScreenTouchRect() noexcept;
// True once a title lays out its screens this way (touch then follows the
// lower screen's rectangle, and none is given while it is hidden).
void SetTriAevumScreenLayoutActive(bool active) noexcept;
// Widescreen upper screen (TRIAEVUM_WIDESCREEN=0 turns it off).
bool TriAevumWidescreenEnabled() noexcept;
bool TriAevumLowerScreenTouchRectActive() noexcept;

} // namespace Oot3dNativeGame
