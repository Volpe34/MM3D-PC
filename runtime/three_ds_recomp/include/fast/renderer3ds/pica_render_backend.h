#pragma once

#include "fast/renderer3ds/pica_composition.h"
#include "fast/renderer3ds/pica_frame_timing.h"
#include "fast/renderer3ds/pica_native_state.h"
#include "fast/renderer3ds/pica_shader_hooks.h"
#include "fast/renderer3ds/pica_shader_source_identity.h"
#include "fast/renderer3ds/pica_vertex_artifact.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Fast::Renderer3ds {

struct PicaDrawView {
    uint64_t SubmissionId = 0;
    uint64_t RenderTargetNamespace = 0;
    uint32_t CommandListAddress = 0;
    uint32_t CommandListOffsetWords = 0;
    PicaCompositionDomain CompositionDomain =
        PicaCompositionDomain::Unknown;
    PicaCompositionAttribution Composition;
    uint32_t CanonicalDescriptorSchemaVersion = 0;
    uint64_t CanonicalVertexProgramId = 0;
    uint64_t CanonicalFragmentProgramId = 0;
    uint64_t CanonicalRasterStateId = 0;
    uint64_t CanonicalPipelineId = 0;
    uint64_t CanonicalDynamicStateId = 0;
    uint64_t CanonicalFullRegisterStateId = 0;
    uint64_t VertexShaderKey = 0;
    uint64_t FragmentShaderKey = 0;
    std::string_view VertexShaderSource;
    std::string_view FragmentShaderSource;
    PicaShaderSourceIdentity VertexShaderSourceIdentity;
    PicaShaderSourceIdentity FragmentShaderSourceIdentity;
    PicaTemporalVertexProgramView TemporalVertexProgram;
    PicaShaderHookLayout FragmentShaderHooks;
    PicaFragmentFeatureView FragmentFeatures;
    std::span<const uint8_t> VertexUniformBytes;
    std::span<const uint8_t> FragmentUniformBytes;
    std::span<const PicaVertexBindingView> VertexBindings;
    std::span<const PicaVertexAttributeView> VertexAttributes;
    uint64_t GeometryIdentity = 0;
    uint64_t GeometryContentVersion = 0;
    bool GeometryIdentityAvailable = false;
    uint8_t PositionAttributeLocation = 0xff;
    uint8_t TexCoord0AttributeLocation = 0xff;
    std::span<const uint8_t> IndexBytes;
    bool Indexed = false;
    bool IndicesAre16Bit = false;
    int32_t BaseVertex = 0;
    uint32_t VertexCount = 0;
    std::span<const PicaTextureView> Textures;
    PicaLightingLutView LightingLut;
    PicaTopology Topology = PicaTopology::TriangleList;
    NativeCullMode CullMode = NativeCullMode::KeepAll;
    float ViewportX = 0.0F;
    float ViewportY = 0.0F;
    float ViewportWidth = 0.0F;
    float ViewportHeight = 0.0F;
    float DepthRange = 0.0F;
    float NearPlane = 0.0F;
    bool WBuffering = false;
    bool FramebufferFlipped = false;
    uint16_t FramebufferWidth = 0;
    uint16_t FramebufferHeight = 0;
    uint32_t FramebufferColorPhysicalAddress = 0;
    uint32_t FramebufferDepthPhysicalAddress = 0;
    uint8_t FramebufferColorFormat = 0;
    uint8_t FramebufferDepthFormat = 0;
    uint8_t ScissorMode = 0;
    uint16_t ScissorX1 = 0;
    uint16_t ScissorY1 = 0;
    uint16_t ScissorX2 = 0;
    uint16_t ScissorY2 = 0;
    uint8_t ColorWriteMask = 0;
    uint8_t FragmentOperationMode = 0;
    PicaLogicOperation LogicOperation = PicaLogicOperation::Copy;
    NativeBlendState Blend;
    bool AlphaTestEnabled = false;
    PicaCompareFunction AlphaCompare = PicaCompareFunction::Always;
    uint8_t AlphaReference = 0;
    bool DepthTestEnabled = false;
    bool DepthWriteEnabled = false;
    PicaCompareFunction DepthCompare = PicaCompareFunction::Always;
    PicaStencilState Stencil;
    std::span<const PicaVertexArtifact> VertexArtifacts;
};

// Projects the full transient draw facade into the compact ordered metadata
// retained by the shared Nintendo 3DS composition scheduler.
[[nodiscard]] inline PicaCompositionDrawReference
BuildPicaCompositionDrawReference(const PicaDrawView& draw) noexcept {
    return {
        draw.SubmissionId,
        {
            draw.RenderTargetNamespace,
            draw.FramebufferColorPhysicalAddress,
            draw.FramebufferDepthPhysicalAddress,
            draw.FramebufferWidth,
            draw.FramebufferHeight,
            draw.FramebufferColorFormat,
            draw.FramebufferDepthFormat,
        },
        draw.CompositionDomain,
        draw.Composition,
    };
}

enum class PicaPresentationMode : uint8_t {
    Replace,
    AlphaOverlay,
    // Drawn over what is already in the target, without blending (an inset).
    Opaque,
};

struct PicaDisplayTransferView {
    uint64_t CompletionId = 0;
    uint64_t RenderTargetNamespace = 0;
    uint32_t InputPhysicalAddress = 0;
    uint32_t OutputPhysicalAddress = 0;
    uint16_t InputWidth = 0;
    uint16_t InputHeight = 0;
    uint16_t OutputWidth = 0;
    uint16_t OutputHeight = 0;
    uint32_t Flags = 0;
    bool Present = false;
    PicaPresentationMode PresentationMode = PicaPresentationMode::Replace;
    // Where the screen goes, as a fraction of the target (x, y, width,
    // height). A zero width fits the whole target.
    float DestinationX = 0.0F;
    float DestinationY = 0.0F;
    float DestinationWidth = 0.0F;
    float DestinationHeight = 0.0F;
    // Columns of the screen that are shown, counted from its left edge (the
    // LCD scans out fewer lines than a framebuffer may hold). Zero shows all.
    uint16_t VisibleLogicalWidth = 0;
    // Fill the destination exactly instead of fitting the image's aspect.
    bool StretchToDestination = false;
    // Part of the (visible) screen shown, as fractions; x1 <= x0 shows all.
    float SourceX0 = 0.0F;
    float SourceY0 = 0.0F;
    float SourceX1 = 0.0F;
    float SourceY1 = 0.0F;
    // Native input line where the transfer starts inside its render target.
    uint16_t InputOriginY = 0;
    uint32_t TextureCopyBytes = 0;
    uint64_t AfterDrawSubmissionId = 0;
};

struct PicaPhysicalMemoryWrite {
    uint32_t Address = 0;
    std::vector<uint8_t> Before;
    std::vector<uint8_t> Bytes;
};

struct PicaMemoryFillView {
    uint64_t RenderTargetNamespace = 0;
    uint32_t StartPhysicalAddress = 0;
    uint32_t EndPhysicalAddress = 0;
    uint32_t Value = 0;
    uint16_t Control = 0;
};

enum class PicaTextureSnapshotFormat : uint8_t {
    Rgba8 = 0,
    R32Uint = 1,
};

// Portable decoded texture-cache entry. The source PICA identity is retained
// so resumed draws resolve the same immutable texture even after guest memory
// at its original physical address has been reused.
struct PicaTextureCacheEntrySnapshot {
    uint64_t ContentHash = 0;
    uint64_t ReplacementGeneration = 0;
    uint32_t PhysicalAddress = 0;
    uint16_t SourceWidth = 0;
    uint16_t SourceHeight = 0;
    uint8_t NativeFormat = 0;
    uint8_t NativeType = 0;
    uint8_t NativeWrapS = 0;
    uint8_t NativeWrapT = 0;
    bool MinLinear = false;
    bool MagLinear = false;
    bool MipLinear = false;
    int16_t LodBiasRaw = 0;
    uint8_t MinMipLevel = 0;
    uint8_t MaxMipLevel = 0;
    bool CustomReplacement = false;
    uint32_t ImageWidth = 0;
    uint32_t ImageHeight = 0;
    uint8_t MipLevels = 1;
    PicaTextureSnapshotFormat ImageFormat =
        PicaTextureSnapshotFormat::Rgba8;
    uint64_t CustomReplacementHash = 0;
    bool CustomReplacementPending = false;
    bool CustomReplacementReady = false;
    std::vector<uint8_t> PixelBytes;
};

// Portable color state for an incrementally rendered native PICA target.
// GPU handles and backend layouts deliberately remain outside this contract.
struct PicaRenderTargetColorSnapshot {
    uint64_t RenderTargetNamespace = 0;
    uint32_t ColorPhysicalAddress = 0;
    uint32_t DepthPhysicalAddress = 0;
    uint16_t FramebufferWidth = 0;
    uint16_t FramebufferHeight = 0;
    uint8_t FramebufferColorFormat = 0;
    uint8_t FramebufferDepthFormat = 0;
    uint16_t RenderScalePermille = 1000;
    uint32_t ImageWidth = 0;
    uint32_t ImageHeight = 0;
    uint8_t SampleCount = 1;
    bool WBuffering = false;
    std::vector<uint8_t> ColorRgba8;
    std::vector<uint8_t> NormalGuideRgba8;
    std::vector<uint8_t> MaterialGuideRgba8;
    std::vector<uint8_t> RigidMotionGuideRgba16FloatLe;
    std::vector<uint8_t> AmbientGuideRgba8;
    std::vector<uint8_t> ShadowR32UintLe;
    std::vector<float> DepthValues;
    std::vector<uint8_t> StencilValues;
};

// Portable scanout state for display images that can outlive their source
// render target contents. This is required by incrementally updated native
// UI surfaces and intentionally excludes backend handles and image layouts.
struct PicaDisplayImageColorSnapshot {
    uint64_t RenderTargetNamespace = 0;
    uint32_t OutputPhysicalAddress = 0;
    uint32_t ImageWidth = 0;
    uint32_t ImageHeight = 0;
    bool Initialized = false;
    std::vector<uint8_t> ColorRgba8;

    bool HasDepthTarget = false;
    uint64_t DepthTargetNamespace = 0;
    uint32_t DepthTargetColorPhysicalAddress = 0;
    uint32_t DepthTargetDepthPhysicalAddress = 0;
    uint16_t DepthTargetFramebufferWidth = 0;
    uint16_t DepthTargetFramebufferHeight = 0;
    uint8_t DepthTargetFramebufferColorFormat = 0;
    uint8_t DepthTargetFramebufferDepthFormat = 0;
    uint16_t DepthTargetRenderScalePermille = 1000;
};

struct PicaPresentationStateSnapshot {
    std::vector<PicaDisplayImageColorSnapshot> DisplayImages;
    std::optional<PicaDisplayTransferView> LastPresentedTransfer;
};

// Renderer-facing PICA transport shared by Nintendo 3DS title adapters. It
// contains no title gameplay, actor, camera or effect-activation policy.
// Draws of one screen region copied into a separate overlay target (cleared
// to transparent each frame), so parts of a screen can be composited
// elsewhere without their backgrounds (MM3D's lower-screen HUD).
struct PicaHudLayerRule {
    bool Enabled = false;
    uint32_t SourceColorPhysicalAddress = 0;
    float ViewportX = 0.0F;
    float ViewportY = 0.0F;
    float ViewportWidth = 0.0F;
    float ViewportHeight = 0.0F;
    uint32_t OverlayColorPhysicalAddress = 0;
    uint32_t OverlayDepthPhysicalAddress = 0;
    std::vector<uint64_t> ExcludedTextureHashes;
    std::vector<uint32_t> ExcludedTextureAddresses;
    // Layer slot (each an independent rule).
    uint32_t Slot = 0;
    // The draws leave the screen (only the layer has them).
    bool MoveDraws = false;
    // Untextured draws are copied too (otherwise only textured ones: the
    // lower screen's untextured fills dim or clear it).
    bool IncludeUntextured = false;
    // The layer's alpha holds how much of what is behind shows through
    // (cleared to 1; opaque draws set 0, blended ones scale it by their
    // destination factor) and its color is premultiplied: models whose
    // textures carry alpha the screen never shows stay solid. Copy it out
    // with copy transform 32 for straight alpha.
    bool CoverageAlpha = false;
};
inline constexpr uint32_t kPicaHudLayerSlots = 3U;

class PicaRenderBackend {
  public:
    using PhysicalMemoryRead = std::function<bool(uint32_t, std::span<uint8_t>)>;
    using PhysicalMemoryCommit = std::function<bool(uint64_t, std::span<const PicaPhysicalMemoryWrite>)>;
    void SetPicaPhysicalMemoryAccess(PhysicalMemoryRead read, PhysicalMemoryCommit commit) {
        mPhysicalMemoryRead = std::move(read);
        mPhysicalMemoryCommit = std::move(commit);
    }
    virtual ~PicaRenderBackend() = default;

    virtual bool PublishPicaCompositionSequence(
        const PicaCompositionSequenceView& sequence,
        std::string* error = nullptr) {
        if (sequence.SchemaVersion !=
            kPicaCompositionSequenceSchemaVersion) {
            if (error != nullptr) {
                *error = "native PICA composition sequence schema is unsupported";
            }
            return false;
        }
        return true;
    }

    virtual bool SubmitPicaDraw(
        const PicaDrawView& draw, std::string* error = nullptr) = 0;
    virtual bool SubmitPicaDisplayTransfer(
        const PicaDisplayTransferView& transfer,
        std::string* error = nullptr) = 0;
    virtual bool ClearPicaRenderTarget(
        uint64_t renderTargetNamespace, uint32_t colorPhysicalAddress,
        std::string* error = nullptr) = 0;
    virtual bool SubmitPicaMemoryFill(
        const PicaMemoryFillView& fill, std::string* error = nullptr) = 0;
    // Makes a display image from CPU pixels (RGBA8, width x height in the
    // framebuffer's own orientation), as if a display transfer had written
    // it to outputPhysicalAddress. Used for screens the guest composes
    // without drawing (clears and CPU copies).
    virtual void SetPicaHudLayerRule(const PicaHudLayerRule& rule) {
        (void)rule;
    }
    // Draws copied into a HUD layer so far.
    virtual uint64_t PicaHudLayerDrawCount(uint32_t slot = 0U) const {
        (void)slot;
        return 0U;
    }
    // Draws sampling a guest texture use a GPU copy of a display image
    // instead, made at this call (MM3D's pause background, which the guest
    // builds on the CPU from a framebuffer only the GPU holds here).
    struct PicaTextureSubstitute {
        uint64_t RenderTargetNamespace = 0;
        uint32_t DisplayAddress = 0;  // the display image copied
        uint32_t TextureAddress = 0;
        uint32_t TextureWidth = 0;
        uint32_t TextureHeight = 0;
        uint32_t UsedWidth = 0;   // texels of the texture the image fills
        uint32_t UsedHeight = 0;
        uint32_t Transform = 0;   // copy pass transform (1 transpose, 2/4 mirror)
    };
    virtual bool SetPicaTextureSubstitute(const PicaTextureSubstitute& substitute,
                                          std::string* error = nullptr) {
        (void)substitute;
        (void)error;
        return false;
    }
    virtual void ClearPicaTextureSubstitutes() {}
    // Copies a display image to another key (e.g. to keep a presented frame),
    // through the copy pass with a transform (see PicaTextureSubstitute; 32
    // turns a CoverageAlpha layer into straight alpha).
    virtual bool CopyPicaDisplayImage(uint64_t renderTargetNamespace,
                                      uint32_t sourceAddress,
                                      uint32_t destinationAddress,
                                      std::string* error = nullptr,
                                      uint32_t transform = 0U) {
        (void)renderTargetNamespace;
        (void)sourceAddress;
        (void)destinationAddress;
        (void)error;
        (void)transform;
        return false;
    }
    virtual bool UploadPicaDisplayImage(
        uint64_t renderTargetNamespace, uint32_t outputPhysicalAddress,
        uint32_t width, uint32_t height, std::span<const uint8_t> rgba8,
        std::string* error = nullptr) {
        (void)renderTargetNamespace;
        (void)outputPhysicalAddress;
        (void)width;
        (void)height;
        (void)rgba8;
        if (error != nullptr) {
            *error = "rendering backend does not upload display images";
        }
        return false;
    }
    virtual bool CapturePicaTextureCache(
        std::vector<PicaTextureCacheEntrySnapshot>& snapshots,
        std::string* error = nullptr) {
        snapshots.clear();
        if (error != nullptr) {
            *error = "rendering backend does not capture native PICA texture cache";
        }
        return false;
    }
    virtual bool RestorePicaTextureCache(
        std::span<const PicaTextureCacheEntrySnapshot> snapshots,
        std::string* error = nullptr) {
        if (snapshots.empty()) {
            return true;
        }
        if (error != nullptr) {
            *error = "rendering backend does not restore native PICA texture cache";
        }
        return false;
    }
    virtual bool CapturePicaColorTargets(
        std::vector<PicaRenderTargetColorSnapshot>& snapshots,
        std::string* error = nullptr) {
        snapshots.clear();
        if (error != nullptr) {
            *error = "rendering backend does not capture native PICA color targets";
        }
        return false;
    }
    virtual bool RestorePicaColorTargets(
        std::span<const PicaRenderTargetColorSnapshot> snapshots,
        std::string* error = nullptr) {
        if (snapshots.empty()) {
            return true;
        }
        if (error != nullptr) {
            *error = "rendering backend does not restore native PICA color targets";
        }
        return false;
    }
    virtual bool CapturePicaPresentationState(
        PicaPresentationStateSnapshot& snapshot,
        std::string* error = nullptr) {
        snapshot = {};
        if (error != nullptr) {
            *error = "rendering backend does not capture native PICA presentation state";
        }
        return false;
    }
    virtual bool RestorePicaPresentationState(
        const PicaPresentationStateSnapshot& snapshot,
        std::string* error = nullptr) {
        if (snapshot.DisplayImages.empty() &&
            !snapshot.LastPresentedTransfer.has_value()) {
            return true;
        }
        if (error != nullptr) {
            *error = "rendering backend does not restore native PICA presentation state";
        }
        return false;
    }

    // Completion IDs are opaque renderer fence tokens. A title adapter owns
    // their mapping to the guest GSP interrupt/service contract.
    virtual bool QueuePicaCompletion(
        uint64_t, std::string* error = nullptr) {
        if (error != nullptr) {
            *error = "rendering backend does not track PICA completions";
        }
        return false;
    }
    virtual std::vector<uint64_t> TakePicaCompletions() {
        return {};
    }

    // Invalidates all backend state derived from a Nintendo 3DS PICA stream.
    // Title gameplay and guest-service reset policy remain outside this call.
    virtual bool ResetPicaState(std::string* error = nullptr) {
        if (error != nullptr) {
            *error = "rendering backend cannot reset PICA state";
        }
        return false;
    }

    virtual bool PublishPicaFrameTemporalSample(
        const PicaFrameTemporalSample&) {
        return false;
    }
  protected:
    PhysicalMemoryRead mPhysicalMemoryRead;
    PhysicalMemoryCommit mPhysicalMemoryCommit;
};

// Whether a render target sampled a texture of the given size in the last
// presented frame (MM3D: its lower-screen area maps are 512x1024 sheets).
bool PicaTargetSampledTextureSize(uint32_t colorPhysicalAddress, uint32_t width,
                                  uint32_t height) noexcept;
// Same, by texture content hash (the native-bytes hash texture packs use).
bool PicaTargetSampledTextureHash(uint32_t colorPhysicalAddress, uint64_t hash) noexcept;

} // namespace Fast::Renderer3ds
