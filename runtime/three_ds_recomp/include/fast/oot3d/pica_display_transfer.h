#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

namespace Fast::Oot3d {

struct PicaDisplayTransferPlanInput {
    uint32_t NativeInputWidth = 0;
    uint32_t NativeInputHeight = 0;
    uint32_t NativeOutputWidth = 0;
    uint32_t NativeOutputHeight = 0;
    uint32_t SourceWidth = 0;
    uint32_t SourceHeight = 0;
    uint32_t Flags = 0;
    // First native input line read: a transfer may start inside the render
    // target (MM3D draws its lower screen below the upper one in one target).
    uint32_t NativeInputOriginY = 0;
};

struct PicaDisplayTransferPlan {
    uint32_t SourceWidth = 0;
    uint32_t SourceHeight = 0;
    uint32_t DestinationWidth = 0;
    uint32_t DestinationHeight = 0;
    uint32_t HorizontalSamples = 1;
    uint32_t VerticalSamples = 1;
    uint32_t ScalingMode = 0;
    uint32_t SourceOriginY = 0;
    // Copy pass only (not native transfers): 1 transpose, 2/4 mirror the
    // source horizontally/vertically, 8 align the source with the
    // destination's last line, 16 write opaque alpha, 64 (no shader effect)
    // a destination larger than the source, 128 area-average the source
    // down to the destination's extent. Destination texels past the
    // source repeat its edge.
    uint32_t Transform = 0;

    [[nodiscard]] bool SamplingFitsSource() const {
        if (Transform != 0U) {
            return SourceWidth && SourceHeight && DestinationWidth &&
                   DestinationHeight && HorizontalSamples == 1 && VerticalSamples == 1;
        }
        return SourceWidth && SourceHeight && DestinationWidth && DestinationHeight &&
            (HorizontalSamples == 1 || HorizontalSamples == 2) &&
            (VerticalSamples == 1 || VerticalSamples == 2) &&
            static_cast<uint64_t>(DestinationWidth) * HorizontalSamples <=
                static_cast<uint64_t>(SourceWidth) + HorizontalSamples - 1 &&
            static_cast<uint64_t>(DestinationHeight) * VerticalSamples <=
                static_cast<uint64_t>(SourceHeight) + VerticalSamples - 1;
    }
    [[nodiscard]] uint32_t BlitSourceWidth() const {
        if ((Transform & 128U) != 0U) return SourceWidth;
        return static_cast<uint32_t>(std::min(
            static_cast<uint64_t>(SourceWidth),
            static_cast<uint64_t>(DestinationWidth) * HorizontalSamples));
    }
    [[nodiscard]] uint32_t BlitSourceHeight() const {
        if ((Transform & 128U) != 0U) return SourceHeight;
        return static_cast<uint32_t>(std::min(
            static_cast<uint64_t>(SourceHeight),
            static_cast<uint64_t>(DestinationHeight) * VerticalSamples));
    }
};

// Builds the GPU-image equivalent of the native PICA display transfer. The
// output dimensions preserve renderer supersampling while crop and 2:1 box
// filtering continue to operate in native PICA coordinates.
[[nodiscard]] std::optional<PicaDisplayTransferPlan>
BuildPicaDisplayTransferPlan(const PicaDisplayTransferPlanInput& input,
                             std::string* error = nullptr);

[[nodiscard]] std::string BuildPicaDisplayTransferComputeShader();

} // namespace Fast::Oot3d
