#pragma once

#include "oot3d/renderer/pica_render_backend.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_native_pica_vulkan_plan.h"

#include <string>

namespace Oot3dNativeGame {
std::span<const Fast::Renderer3ds::PicaVertexArtifact> NativePicaVertexProgramFamily();

struct Oot3dPicaVulkanDrawBridgeTiming {
    double UniformPackingSeconds = 0.0;
    double ViewConstructionSeconds = 0.0;
    double BackendSubmissionSeconds = 0.0;
};

bool SubmitOot3dPicaVulkanDrawPlan(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaVulkanDrawPlan& plan,
    std::string* error = nullptr,
    uint64_t renderTargetNamespace = 0,
    const Oot3dPicaVulkanDrawOverrides* overrides = nullptr,
    Oot3dPicaVulkanDrawBridgeTiming* timing = nullptr);

// Where a presented screen goes in the window (fractions of the target).
struct Oot3dPicaScreenPlacement {
    Oot3d::Renderer::PicaPresentationMode Mode =
        Oot3d::Renderer::PicaPresentationMode::Replace;
    float X = 0.0F;
    float Y = 0.0F;
    float Width = 0.0F;   // zero: fit the whole target
    float Height = 0.0F;
    uint16_t VisibleLogicalWidth = 0;
    bool Stretch = false;
    float SourceX0 = 0.0F;  // shown part of the screen (fractions)
    float SourceY0 = 0.0F;
    float SourceX1 = 0.0F;
    float SourceY1 = 0.0F;
};

bool SubmitOot3dPicaVulkanDisplayTransfer(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaDisplayTransferSubmission& transfer,
    bool present,
    std::string* error = nullptr,
    uint64_t renderTargetNamespace = 0,
    Oot3d::Renderer::PicaPresentationMode presentationMode =
        Oot3d::Renderer::PicaPresentationMode::Replace,
    const Oot3dPicaScreenPlacement* placement = nullptr);

bool ClearOot3dPicaVulkanRenderTarget(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    uint64_t renderTargetNamespace,
    uint32_t colorPhysicalAddress, std::string* error = nullptr);

bool SubmitOot3dPicaVulkanMemoryFill(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaMemoryFillSubmission& fill,
    std::string* error = nullptr,
    uint64_t renderTargetNamespace = 0);

} // namespace Oot3dNativeGame
