#include "triaevum_screen_layout.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>

namespace Oot3dNativeGame {
namespace {

std::optional<TriAevumScreenRect> gLowerScreenTouchRect;
std::vector<TriAevumScreenCrop> gLowerScreenTouchCrops;
bool gScreenLayoutActive = false;

// Largest rectangle of the given aspect inside an area (in pixels), centred.
TriAevumScreenRect Fit(float areaX, float areaY, float areaWidth,
                       float areaHeight, float aspect) noexcept {
  float width = areaWidth;
  float height = width / aspect;
  if (height > areaHeight) {
    height = areaHeight;
    width = height * aspect;
  }
  return {areaX + (areaWidth - width) * 0.5F,
          areaY + (areaHeight - height) * 0.5F, width, height};
}

TriAevumScreenRect Normalize(const TriAevumScreenRect &pixels, float windowWidth,
                             float windowHeight) noexcept {
  return {pixels.X / windowWidth, pixels.Y / windowHeight,
          pixels.Width / windowWidth, pixels.Height / windowHeight};
}

constexpr std::array<std::string_view,
                     static_cast<size_t>(TriAevumScreenLayout::Count)>
    kNames{"single_screen", "top_with_lower_inset", "side_by_side",
           "lower_focus", "top_only"};

std::vector<TriAevumHudPart> gHudParts;
std::vector<TriAevumHudPart> gMenuParts;

// Each part cut from its screen and placed on the upper screen's rectangle
// (pixels).
void PlaceParts(const std::vector<TriAevumHudPart> &parts,
                const TriAevumScreenRect &upper, float width, float height,
                TriAevumScreenPlacements &placements) {
  for (const auto &part : parts) {
    const float sourceWidth = part.SourceX1 - part.SourceX0;
    const float sourceHeight = part.SourceY1 - part.SourceY0;
    if (sourceWidth <= 0.0F || sourceHeight <= 0.0F) {
      continue;
    }
    const float screenWidth =
        part.Upper ? kTriAevumUpperScreenWidth : kTriAevumLowerScreenWidth;
    float destinationWidth = part.DestinationWidth * upper.Height;
    float destinationHeight = destinationWidth * sourceHeight / sourceWidth;
    if (part.FullWidth) {
      destinationWidth = upper.Width;
      destinationHeight = upper.Height * sourceHeight / kTriAevumScreenHeight;
    }
    if (part.Fill) {
      destinationWidth = upper.Width;
      destinationHeight = upper.Height;
    }
    TriAevumScreenCrop crop;
    crop.Source = {part.SourceX0 / screenWidth, part.SourceY0 / kTriAevumScreenHeight,
                   sourceWidth / screenWidth, sourceHeight / kTriAevumScreenHeight};
    // Positions are fractions of the upper screen; negative ones count from
    // the right/bottom edge.
    const float xUnit = part.HeightUnitsX ? upper.Height : upper.Width;
    const float x = part.DestinationX >= 0.0F
                        ? upper.X + part.DestinationX * xUnit
                        : upper.X + upper.Width + part.DestinationX * xUnit -
                              destinationWidth;
    const float y = part.DestinationY >= 0.0F
                        ? upper.Y + part.DestinationY * upper.Height
                        : upper.Y + upper.Height + part.DestinationY * upper.Height -
                              destinationHeight;
    crop.Destination = Normalize({x, y, destinationWidth, destinationHeight}, width, height);
    (part.Upper ? (part.Fill ? placements.UpperFills : placements.UpperCrops)
                : placements.LowerCrops)
        .push_back(crop);
  }
}
std::optional<std::pair<uint16_t, uint16_t>> gShortcutTouch;
std::chrono::steady_clock::time_point gShortcutTouchStart;
// A drag (menu item to an item slot): press, move, hold, release.
std::optional<std::array<uint16_t, 4>> gShortcutDrag;

} // namespace

void SetTriAevumHudParts(std::vector<TriAevumHudPart> parts) {
  gHudParts = std::move(parts);
}

void SetTriAevumMenuParts(std::vector<TriAevumHudPart> parts) {
  gMenuParts = std::move(parts);
}

void RequestTriAevumShortcutTouch(uint16_t x, uint16_t y) noexcept {
  gShortcutTouch = std::make_pair(x, y);
  gShortcutTouchStart = std::chrono::steady_clock::now();
}

void RequestTriAevumShortcutDrag(uint16_t fromX, uint16_t fromY, uint16_t toX,
                                 uint16_t toY) noexcept {
  gShortcutTouch.reset();
  gShortcutDrag = std::array<uint16_t, 4>{fromX, fromY, toX, toY};
  gShortcutTouchStart = std::chrono::steady_clock::now();
}

std::optional<std::pair<uint16_t, uint16_t>> TakeTriAevumShortcutTouch() noexcept {
  if (gShortcutDrag.has_value()) {
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - gShortcutTouchStart)
                          .count();
    const auto &d = *gShortcutDrag;
    if (ms > 480.0) {
      gShortcutDrag.reset();
      return std::nullopt;
    }
    const double t = std::clamp((ms - 100.0) / 280.0, 0.0, 1.0);
    return std::make_pair(static_cast<uint16_t>(d[0] + (d[2] - d[0]) * t),
                          static_cast<uint16_t>(d[1] + (d[3] - d[1]) * t));
  }
  if (!gShortcutTouch.has_value()) {
    return std::nullopt;
  }
  // Pressed for a few game frames, then released (a tap).
  if (std::chrono::steady_clock::now() - gShortcutTouchStart >
      std::chrono::milliseconds(120)) {
    gShortcutTouch.reset();
    return std::nullopt;
  }
  return gShortcutTouch;
}

TriAevumScreenPlacements ResolveTriAevumScreenPlacements(
    TriAevumScreenLayout layout, uint32_t windowWidth, uint32_t windowHeight,
    float insetScale, float upperAspect, const TriAevumSingleScreenState &single) {
  const bool lowerOverlay = single.LowerOverlay;
  const float width = static_cast<float>(std::max<uint32_t>(windowWidth, 1U));
  const float height = static_cast<float>(std::max<uint32_t>(windowHeight, 1U));
  if (!(upperAspect > 0.0F)) {
    upperAspect = kTriAevumUpperScreenWidth / kTriAevumScreenHeight;
  }
  constexpr float lowerAspect = kTriAevumLowerScreenWidth / kTriAevumScreenHeight;
  TriAevumScreenPlacements placements;
  switch (layout) {
  case TriAevumScreenLayout::SingleScreen: {
    const auto upper = Fit(0.0F, 0.0F, width, height, upperAspect);
    placements.Upper = Normalize(upper, width, height);
    placements.LowerOverUpper = true;
    if (single.MergedMenu && !lowerOverlay) {
      PlaceParts(gMenuParts, upper, width, height, placements);
      break;
    }
    const float menu = std::clamp(single.MenuBlend, 0.0F, 1.0F);
    if (menu > 0.0F && !lowerOverlay) {
      // Both screens side by side, centred; from the full upper screen the
      // upper one shrinks to its place while the lower one slides in.
      const auto group = Fit(0.0F, 0.0F, width, height, upperAspect + lowerAspect);
      const float upperWidth = group.Height * upperAspect;
      const float eased = menu * menu * (3.0F - 2.0F * menu);
      const auto lerp = [eased](float from, float to) {
        return from + (to - from) * eased;
      };
      placements.Upper = Normalize(
          {lerp(upper.X, group.X), lerp(upper.Y, group.Y),
           lerp(upper.Width, upperWidth), lerp(upper.Height, group.Height)},
          width, height);
      const float lowerX = group.X + upperWidth;
      placements.Lower = Normalize(
          {lerp(width, lowerX), group.Y, group.Width - upperWidth, group.Height},
          width, height);
      break;
    }
    const float inset = std::clamp(single.InsetBlend, 0.0F, 1.0F);
    if (inset > 0.0F && !lowerOverlay) {
      const float eased = inset * inset * (3.0F - 2.0F * inset);
      const float insetHeight = upper.Height * 0.5F;
      const float insetWidth = insetHeight * lowerAspect;
      const float margin = upper.Height * 0.03F;
      const float shownX = upper.X + upper.Width - insetWidth - margin;
      placements.Lower = Normalize(
          {width + (shownX - width) * eased,
           upper.Y + (upper.Height - insetHeight) * 0.5F, insetWidth, insetHeight},
          width, height);
      break;
    }
    if (lowerOverlay) {
      // The lower screen large in the middle, for menus and touch.
      const float overlayHeight = upper.Height * 0.86F;
      const float overlayWidth = overlayHeight * lowerAspect;
      placements.Lower = Normalize(
          {upper.X + (upper.Width - overlayWidth) * 0.5F,
           upper.Y + (upper.Height - overlayHeight) * 0.5F, overlayWidth,
           overlayHeight},
          width, height);
      break;
    }
    PlaceParts(gHudParts, upper, width, height, placements);
    break;
  }
  case TriAevumScreenLayout::TopWithLowerInset: {
    const auto upper = Fit(0.0F, 0.0F, width, height, upperAspect);
    placements.Upper = Normalize(upper, width, height);
    // The inset sits in the lower right corner of the upper screen.
    const float scale = std::clamp(insetScale, 0.15F, 0.6F);
    const float insetHeight = upper.Height * scale;
    const float insetWidth = insetHeight * lowerAspect;
    const float margin = upper.Height * 0.02F;
    placements.Lower = Normalize(
        {upper.X + upper.Width - insetWidth - margin,
         upper.Y + upper.Height - insetHeight - margin, insetWidth, insetHeight},
        width, height);
    placements.LowerOverUpper = true;
    break;
  }
  case TriAevumScreenLayout::SideBySide: {
    // 400 + 320 logical columns at one height, centred as a group.
    const auto group = Fit(0.0F, 0.0F, width, height, upperAspect + lowerAspect);
    const float upperWidth = group.Height * upperAspect;
    placements.Upper =
        Normalize({group.X, group.Y, upperWidth, group.Height}, width, height);
    placements.Lower = Normalize(
        {group.X + upperWidth, group.Y, group.Width - upperWidth, group.Height},
        width, height);
    break;
  }
  case TriAevumScreenLayout::LowerFocus:
    placements.Lower =
        Normalize(Fit(0.0F, 0.0F, width, height, lowerAspect), width, height);
    break;
  case TriAevumScreenLayout::TopOnly:
  case TriAevumScreenLayout::Count:
    placements.Upper =
        Normalize(Fit(0.0F, 0.0F, width, height, upperAspect), width, height);
    break;
  }
  return placements;
}

TriAevumScreenLayout NextTriAevumScreenLayout(TriAevumScreenLayout layout) noexcept {
  const auto next = static_cast<uint8_t>(layout) + 1U;
  return next >= static_cast<uint8_t>(TriAevumScreenLayout::Count)
             ? TriAevumScreenLayout::SingleScreen
             : static_cast<TriAevumScreenLayout>(next);
}

std::string_view TriAevumScreenLayoutName(TriAevumScreenLayout layout) noexcept {
  const auto index = static_cast<size_t>(layout);
  return index < kNames.size() ? kNames[index] : "single_screen";
}

std::optional<TriAevumScreenLayout>
ParseTriAevumScreenLayout(std::string_view name) noexcept {
  for (size_t index = 0; index < kNames.size(); ++index) {
    if (kNames[index] == name) {
      return static_cast<TriAevumScreenLayout>(index);
    }
  }
  return std::nullopt;
}

void SetTriAevumLowerScreenTouchRect(std::optional<TriAevumScreenRect> rect) noexcept {
  gLowerScreenTouchRect = rect;
}

void SetTriAevumLowerScreenTouchCrops(std::vector<TriAevumScreenCrop> crops) {
  gLowerScreenTouchCrops = std::move(crops);
}

std::optional<std::pair<float, float>> TriAevumLowerScreenPointThroughCrops(float x, float y) {
  // The last part is drawn on top.
  for (auto crop = gLowerScreenTouchCrops.rbegin(); crop != gLowerScreenTouchCrops.rend(); ++crop) {
    const auto &d = crop->Destination;
    if (d.Width <= 0.0F || d.Height <= 0.0F || x < d.X || y < d.Y || x >= d.X + d.Width ||
        y >= d.Y + d.Height) {
      continue;
    }
    return std::make_pair(crop->Source.X + (x - d.X) / d.Width * crop->Source.Width,
                          crop->Source.Y + (y - d.Y) / d.Height * crop->Source.Height);
  }
  return std::nullopt;
}

std::optional<TriAevumScreenRect> TriAevumLowerScreenTouchRect() noexcept {
  return gLowerScreenTouchRect;
}

void SetTriAevumScreenLayoutActive(bool active) noexcept {
  gScreenLayoutActive = active;
}

bool TriAevumWidescreenEnabled() noexcept {
  static const bool enabled = [] {
    const char *value = std::getenv("TRIAEVUM_WIDESCREEN");
    return value == nullptr || std::string_view(value) != "0";
  }();
  return enabled;
}

bool TriAevumLowerScreenTouchRectActive() noexcept {
  return gScreenLayoutActive;
}

} // namespace Oot3dNativeGame
