#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/scene_presentation_policy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace Fast::Oot3d {

// A padded top-screen target (512 lines wide, as Majora's Mask 3D uses, also
// holding the lower screen) follows the window like OoT3D's 480-line one:
// 0 = native size, 1 = window resolution at the 3DS aspect, 2 = window
// resolution and aspect (widescreen).
inline int gPaddedTopScreenTargetMode = 0;

struct RenderExtent {
    uint32_t Width = 1;
    uint32_t Height = 1;
};

[[nodiscard]] inline float ResolveInternalResolutionScale(
    const GraphicsSettings& settings) {
    const float requested =
        settings.AntiAliasing == AntiAliasingMode::Upscaler
            ? 1.0F / UpscalerScalingFactor(settings.UpscalerMode)
            : settings.InternalResolutionScale;
    return std::clamp(requested, 0.5F, 2.0F);
}

[[nodiscard]] inline RenderExtent ScaleRenderExtent(
    RenderExtent extent, float scale) {
    const float sanitized =
        std::isfinite(scale) ? std::clamp(scale, 0.5F, 2.0F) : 1.0F;
    return {
        std::max(1U, static_cast<uint32_t>(std::lround(
                         static_cast<float>(extent.Width) * sanitized))),
        std::max(1U, static_cast<uint32_t>(std::lround(
                         static_cast<float>(extent.Height) * sanitized)))};
}

[[nodiscard]] inline RenderExtent ResolveNativePicaRenderExtent(
    RenderExtent guestExtent, RenderExtent outputExtent,
    float internalScale) {
    RenderExtent base = guestExtent;
    if (guestExtent.Width == kNativeTopScreenPhysicalWidth &&
        guestExtent.Height == kNativeTopScreenPhysicalHeight &&
        outputExtent.Width != 0U && outputExtent.Height != 0U) {
        // The top LCD is stored rotated: physical X is twice logical height.
        base = {outputExtent.Height * 2U, outputExtent.Width};
    } else if (gPaddedTopScreenTargetMode != 0 && guestExtent.Width == 512U &&
               guestExtent.Height == kNativeTopScreenPhysicalHeight &&
               outputExtent.Width != 0U && outputExtent.Height != 0U) {
        const uint32_t lines = static_cast<uint32_t>(
            static_cast<uint64_t>(outputExtent.Height) * 2U * guestExtent.Width /
            kNativeTopScreenPhysicalWidth);
        const uint32_t columns = gPaddedTopScreenTargetMode == 2
            ? outputExtent.Width
            : static_cast<uint32_t>(static_cast<uint64_t>(outputExtent.Height) *
                                    kNativeTopScreenLogicalWidth /
                                    kNativeTopScreenLogicalHeight);
        base = {lines, columns};
    } else if (guestExtent.Width == 512U &&
               guestExtent.Height == kNativeTopScreenPhysicalHeight &&
               std::getenv("TRIAEVUM_MM3D_UPSCALE") != nullptr) {
        base = {guestExtent.Width * 2U, guestExtent.Height * 2U};
    }
    return ScaleRenderExtent(base, internalScale);
}

} // namespace Fast::Oot3d
