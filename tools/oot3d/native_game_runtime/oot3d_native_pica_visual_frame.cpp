#include <map>
#include "oot3d_native_pica_visual_frame.h"
#include "fast/renderer3ds/pica_vertex_temporal_policy.h"
#include "fast/renderer3ds/pica_camera_temporal_policy.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <utility>


namespace {
FILE* FlingTraceFile() {
    static FILE* file = [] {
        const char* path = std::getenv("TRIAEVUM_FLING_TRACE");
        return path != nullptr ? std::fopen(path, "w") : nullptr;
    }();
    return file;
}
double FlingTraceMs() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}  // namespace

namespace Oot3dNativeGame {
void FlingTraceNote(const char* text) {
    if (FILE* file = FlingTraceFile()) {
        std::fprintf(file, "NOTE t=%.1f %s\n", FlingTraceMs(), text);
        std::fflush(file);
    }
}
void FlingTraceMark() {
    if (FILE* file = FlingTraceFile()) {
        std::fprintf(file, "MARK t=%.1f\n", FlingTraceMs());
        std::fflush(file);
    }
}
}  // namespace Oot3dNativeGame

namespace Oot3dNativeGame {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

template <typename Value> void HashValue(uint64_t& hash, const Value& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    for (size_t index = 0; index < sizeof(Value); ++index) {
        hash = (hash ^ bytes[index]) * kFnvPrime;
    }
}

float InterpolateFinite(float previous, float current, float alpha) {
    if (!std::isfinite(previous) || !std::isfinite(current)) {
        return current;
    }
    return previous + (current - previous) * alpha;
}

bool IsContinuousVertexUniform(size_t index) {
    // Native material slots 0x59 and 0x5C+ contain selectors/configuration.
    // Matrices, bone palettes, light values, colors and the 0x5A/0x5B
    // material vectors are continuous render inputs.
    return index < 0x59U || index == 0x5AU || index == 0x5BU;
}

size_t VertexFormatByteSize(Oot3dPicaVertexFormat format) {
    switch (format) {
    case Oot3dPicaVertexFormat::SignedByte:
    case Oot3dPicaVertexFormat::UnsignedByte:
        return 1U;
    case Oot3dPicaVertexFormat::SignedShort:
        return 2U;
    case Oot3dPicaVertexFormat::Float:
        return 4U;
    }
    return 0U;
}

const Oot3dPicaVulkanVertexBinding* FindVertexBinding(
    const Oot3dPicaVulkanDrawPlan& plan, uint8_t binding) {
    const auto found = std::find_if(
        plan.VertexBindings.begin(), plan.VertexBindings.end(),
        [binding](const auto& candidate) {
            return candidate.Binding == binding;
        });
    return found != plan.VertexBindings.end() ? &*found : nullptr;
}

Oot3dPicaVulkanVertexBinding* FindVertexBinding(std::vector<Oot3dPicaVulkanVertexBinding>& bindings, uint8_t binding) {
    const auto found = std::find_if(bindings.begin(), bindings.end(),
        [binding](const auto& candidate) {
            return candidate.Binding == binding;
        });
    return found != bindings.end() ? &*found : nullptr;
}

bool EqualBytes(std::span<const uint8_t> left, std::span<const uint8_t> right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin());
}

bool EqualBindingBytes(const Oot3dPicaVulkanVertexBinding &left,
                       const Oot3dPicaVulkanVertexBinding &right) {
  if (left.SharedBytes != nullptr && left.SharedBytes == right.SharedBytes) {
    return true;
  }
  if (left.ContentVersionAvailable && right.ContentVersionAvailable &&
      left.SourcePhysicalAddress == right.SourcePhysicalAddress &&
      left.ContentVersion == right.ContentVersion) {
    return true;
  }
  return EqualBytes(left.ResolvedBytes(), right.ResolvedBytes());
}

bool EqualIndexBytes(const Oot3dPicaVulkanDrawPlan &left,
                     const Oot3dPicaVulkanDrawPlan &right) {
  if (left.SharedIndexBytes != nullptr &&
      left.SharedIndexBytes == right.SharedIndexBytes) {
    return true;
  }
  if (left.IndexContentVersionAvailable && right.IndexContentVersionAvailable &&
      left.IndexPhysicalAddress == right.IndexPhysicalAddress &&
      left.IndexContentVersion == right.IndexContentVersion) {
    return true;
  }
  return EqualBytes(left.ResolvedIndexBytes(), right.ResolvedIndexBytes());
}

// Transparent world draws whose vertices change are effects rebuilt by the
// CPU every frame (sparks, dust, bubbles, sword trails): their quads are
// listed in spawn order, which shifts as particles come and go, so vertex i
// of one frame is another particle in the next, and blending them (positions
// and flipbook texture coordinates) drew squares and wrong animation frames.
// They keep their own vertices; the model matrix and the camera still move.
bool gInterfaceSnaps = false;

// A 2D interface draw that is shown unblended (SetOot3dPicaVisualInterfaceSnaps).
bool InterfaceDrawSnaps(const Oot3dPicaVulkanDrawPlan& current);

bool VertexDataBlendAllowed(const Oot3dPicaVulkanDrawPlan& current) {
    static const bool effectVertexBlend = std::getenv("TRIAEVUM_EFFECT_VERTEX_BLEND") != nullptr;
    if (effectVertexBlend) {
        return true;
    }
    // Also outside a recognised gameplay frame (layer unknown: a cutscene,
    // a test warp from the title): a blended draw that tests depth without
    // writing it is a world effect (Link's roll dust became squares), not a
    // 2D interface part.
    const auto& merger = current.State.OutputMerger;
    const bool worldEffect = current.Composition.Layer == Oot3dPicaCompositionLayer::Unknown &&
                             merger.Blend.Enabled && merger.Depth.TestEnabled && !merger.Depth.WriteEnabled;
    return current.Composition.Layer != Oot3dPicaCompositionLayer::TransparentWorld && !worldEffect;
}

bool InterpolatableVertexValuesDiffer(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current,
    Oot3dPicaVertexInputRate inputRate) {
    if (!VertexDataBlendAllowed(current)) {
        return false;
    }
    bool candidateBytesDiffer = false;
    for (const auto& right : current.VertexBindings) {
        if (right.InputRate != inputRate) {
            continue;
        }
        const auto* left = FindVertexBinding(previous, right.Binding);
        if (left != nullptr && left->InputRate == inputRate &&
            left->ByteStride == right.ByteStride &&
            !EqualBindingBytes(*left, right)) {
          candidateBytesDiffer = true;
          break;
        }
    }
    if (!candidateBytesDiffer) {
        return false;
    }
    for (const auto& attribute : current.VertexAttributes) {
        if (attribute.Format == Oot3dPicaVertexFormat::UnsignedByte ||
            (current.VertexShader.TemporalProgram &&
             !Fast::Renderer3ds::IsPicaVertexInputContinuousForPresentation(
                 current.VertexShader.TemporalProgram->Hooks, attribute.Location))) {
            continue;
        }
        const auto* left = FindVertexBinding(previous, attribute.Binding);
        const auto* right = FindVertexBinding(current, attribute.Binding);
        const size_t componentBytes = VertexFormatByteSize(attribute.Format);
        const size_t attributeBytes =
            componentBytes * attribute.ComponentCount;
        const auto leftBytes = left != nullptr ? left->ResolvedBytes()
                                               : std::span<const uint8_t>{};
        const auto rightBytes = right != nullptr ? right->ResolvedBytes()
                                                 : std::span<const uint8_t>{};
        if (left == nullptr || right == nullptr ||
            left->InputRate != inputRate || right->InputRate != inputRate ||
            left->ByteStride == 0U || left->ByteStride != right->ByteStride ||
            leftBytes.size() != rightBytes.size() ||
            attribute.ByteOffset > left->ByteStride ||
            attributeBytes > left->ByteStride - attribute.ByteOffset) {
          continue;
        }
        for (size_t offset = attribute.ByteOffset;
             offset + attributeBytes <= leftBytes.size();
             offset += left->ByteStride) {
          if (!std::equal(leftBytes.begin() + offset,
                          leftBytes.begin() + offset + attributeBytes,
                          rightBytes.begin() + offset)) {
            return true;
          }
        }
    }
    return false;
}

bool InterpolateVertexValues(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaVertexInputRate inputRate,
                             std::vector<Oot3dPicaVulkanVertexBinding>& outputBindings) {
    bool interpolated = false;
    for (const auto& attribute : current.VertexAttributes) {
        if (attribute.Format == Oot3dPicaVertexFormat::UnsignedByte ||
            (current.VertexShader.TemporalProgram &&
             !Fast::Renderer3ds::IsPicaVertexInputContinuousForPresentation(
                 current.VertexShader.TemporalProgram->Hooks, attribute.Location))) {
            continue;
        }
        const auto* left = FindVertexBinding(previous, attribute.Binding);
        const auto* right = FindVertexBinding(current, attribute.Binding);
        auto* destination = FindVertexBinding(outputBindings, attribute.Binding);
        const size_t componentBytes = VertexFormatByteSize(attribute.Format);
        const size_t attributeBytes =
            componentBytes * attribute.ComponentCount;
        const auto leftBytes = left != nullptr ? left->ResolvedBytes()
                                               : std::span<const uint8_t>{};
        const auto rightBytes = right != nullptr ? right->ResolvedBytes()
                                                 : std::span<const uint8_t>{};
        if (left == nullptr || right == nullptr || destination == nullptr ||
            left->InputRate != inputRate || right->InputRate != inputRate ||
            destination->InputRate != inputRate || left->ByteStride == 0U ||
            left->ByteStride != right->ByteStride ||
            left->ByteStride != destination->ByteStride ||
            leftBytes.size() != rightBytes.size() ||
            leftBytes.size() != destination->ResolvedBytes().size() ||
            attribute.ByteOffset > left->ByteStride ||
            attributeBytes > left->ByteStride - attribute.ByteOffset) {
          continue;
        }
        auto &destinationBytes = destination->MutableBytes();
        for (size_t recordOffset = attribute.ByteOffset;
             recordOffset + attributeBytes <= leftBytes.size();
             recordOffset += left->ByteStride) {
          for (size_t component = 0; component < attribute.ComponentCount;
               ++component) {
            const size_t offset = recordOffset + component * componentBytes;
            switch (attribute.Format) {
            case Oot3dPicaVertexFormat::SignedByte: {
              const int8_t leftValue = static_cast<int8_t>(leftBytes[offset]);
              const int8_t rightValue = static_cast<int8_t>(rightBytes[offset]);
              const auto value =
                  std::clamp<long>(std::lround(InterpolateFinite(
                                       static_cast<float>(leftValue),
                                       static_cast<float>(rightValue), alpha)),
                                   std::numeric_limits<int8_t>::min(),
                                   std::numeric_limits<int8_t>::max());
              destinationBytes[offset] =
                  static_cast<uint8_t>(static_cast<int8_t>(value));
              interpolated |= leftValue != rightValue;
              break;
            }
            case Oot3dPicaVertexFormat::SignedShort: {
              int16_t leftValue = 0;
              int16_t rightValue = 0;
              std::memcpy(&leftValue, leftBytes.data() + offset,
                          sizeof(leftValue));
              std::memcpy(&rightValue, rightBytes.data() + offset,
                          sizeof(rightValue));
              const auto value =
                  std::clamp<long>(std::lround(InterpolateFinite(
                                       static_cast<float>(leftValue),
                                       static_cast<float>(rightValue), alpha)),
                                   std::numeric_limits<int16_t>::min(),
                                   std::numeric_limits<int16_t>::max());
              const int16_t encoded = static_cast<int16_t>(value);
              std::memcpy(destinationBytes.data() + offset, &encoded,
                          sizeof(encoded));
              interpolated |= leftValue != rightValue;
              break;
            }
            case Oot3dPicaVertexFormat::Float: {
              float leftValue = 0.0F;
              float rightValue = 0.0F;
              std::memcpy(&leftValue, leftBytes.data() + offset,
                          sizeof(leftValue));
              std::memcpy(&rightValue, rightBytes.data() + offset,
                          sizeof(rightValue));
              const float encoded =
                  InterpolateFinite(leftValue, rightValue, alpha);
              std::memcpy(destinationBytes.data() + offset, &encoded,
                          sizeof(encoded));
              interpolated |= leftValue != rightValue;
              break;
            }
            case Oot3dPicaVertexFormat::UnsignedByte:
              break;
            }
          }
        }
    }
    return interpolated;
}

bool ContinuousVertexUniformValuesDiffer(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current) {
    for (size_t index = 0; index < previous.VertexShader.Uniforms.Floats.size();
         ++index) {
        if (!IsContinuousVertexUniform(index)) {
            continue;
        }
        if (previous.VertexShader.Uniforms.Floats[index] !=
            current.VertexShader.Uniforms.Floats[index]) {
            return true;
        }
    }
    return false;
}

bool ContinuousFragmentUniformValuesDiffer(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current) {
    return previous.FragmentShader.Uniforms.TevConstants !=
               current.FragmentShader.Uniforms.TevConstants ||
           previous.FragmentShader.Uniforms.CombinerBufferColor !=
               current.FragmentShader.Uniforms.CombinerBufferColor ||
           previous.FragmentShader.Uniforms.FogColor !=
               current.FragmentShader.Uniforms.FogColor ||
           previous.FragmentShader.Uniforms.FogLut !=
               current.FragmentShader.Uniforms.FogLut ||
           previous.FragmentShader.Uniforms.TextureLodBias !=
               current.FragmentShader.Uniforms.TextureLodBias ||
           previous.FragmentShader.Uniforms.Lighting !=
               current.FragmentShader.Uniforms.Lighting ||
           previous.FragmentShader.Uniforms.ShadowBiasConstant !=
               current.FragmentShader.Uniforms.ShadowBiasConstant ||
           previous.FragmentShader.Uniforms.ShadowBiasLinear !=
               current.FragmentShader.Uniforms.ShadowBiasLinear;
}

bool ContinuousViewportValuesDiffer(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current) {
    return previous.State.Viewport.HalfWidth !=
               current.State.Viewport.HalfWidth ||
           previous.State.Viewport.HalfHeight !=
               current.State.Viewport.HalfHeight ||
           previous.State.Viewport.DepthRange !=
               current.State.Viewport.DepthRange ||
           previous.State.Viewport.NearPlane !=
               current.State.Viewport.NearPlane;
}

double NormalizedVertexUniformDelta(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current) {
    double maximum = 0.0;
    for (size_t index = 0; index < previous.VertexShader.Uniforms.Floats.size();
         ++index) {
        if (!IsContinuousVertexUniform(index)) {
            continue;
        }
        for (size_t component = 0; component < 4U; ++component) {
            const double left =
                previous.VertexShader.Uniforms.Floats[index][component];
            const double right =
                current.VertexShader.Uniforms.Floats[index][component];
            if (!std::isfinite(left) || !std::isfinite(right)) {
                continue;
            }
            const double scale =
                std::max({1.0, std::abs(left), std::abs(right)});
            maximum =
                std::max(maximum, std::abs(right - left) / scale);
        }
    }
    return maximum;
}

uint64_t ComputeStructuralDrawIdentity(
    const Oot3dPicaVulkanDrawPlan& plan) {
    uint64_t hash = kFnvOffset;
    HashValue(hash, plan.VertexShader.StateKey);
    HashValue(hash, plan.FragmentShader.StateKey);
    HashValue(hash, plan.Indexed);
    HashValue(hash, plan.IndicesAre16Bit);
    HashValue(hash, plan.BaseVertex);
    HashValue(hash, plan.VertexCount);
    HashValue(hash, plan.State.VertexInput.AttributeCount);
    HashValue(hash, plan.State.VertexInput.VertexOffset);
    for (const auto& loader : plan.State.VertexInput.Loaders) {
        HashValue(hash, loader.DataOffset);
        HashValue(hash, loader.ByteStride);
        HashValue(hash, loader.ComponentCount);
        for (const uint8_t component : loader.Components) {
            HashValue(hash, component);
        }
    }
    for (const auto& binding : plan.VertexBindings) {
        HashValue(hash, binding.Binding);
        HashValue(hash, binding.ByteStride);
        HashValue(hash, binding.InputRate);
        HashValue(hash, binding.ResolvedBytes().size());
    }
    for (const auto& attribute : plan.VertexAttributes) {
        HashValue(hash, attribute.Location);
        HashValue(hash, attribute.Binding);
        HashValue(hash, attribute.Format);
        HashValue(hash, attribute.ComponentCount);
        HashValue(hash, attribute.ByteOffset);
    }
    for (const uint8_t byte : plan.ResolvedIndexBytes()) {
      HashValue(hash, byte);
    }
    for (const auto& texture : plan.Textures) {
        HashValue(hash, texture.Slot);
        HashValue(hash, texture.State.Enabled);
        HashValue(hash, texture.State.Width);
        HashValue(hash, texture.State.Height);
        HashValue(hash, texture.State.Format);
        HashValue(hash, texture.State.Type);
        HashValue(hash, texture.State.WrapS);
        HashValue(hash, texture.State.WrapT);
        HashValue(hash, texture.State.MinLinear);
        HashValue(hash, texture.State.MagLinear);
    }
    HashValue(hash, plan.State.Framebuffer.Width);
    HashValue(hash, plan.State.Framebuffer.Height);
    HashValue(hash, plan.State.Framebuffer.ColorFormat);
    HashValue(hash, plan.State.Framebuffer.DepthFormat);
    HashValue(hash, plan.State.Framebuffer.Flipped);
    HashValue(hash, plan.State.Topology);
    return hash;
}

uint64_t ComputePipelineDrawIdentity(const Oot3dPicaVulkanDrawPlan& plan) {
    uint64_t hash = kFnvOffset;
    HashValue(hash, plan.VertexShader.StateKey);
    HashValue(hash, plan.FragmentShader.StateKey);
    HashValue(hash, plan.Indexed);
    HashValue(hash, plan.IndicesAre16Bit);
    HashValue(hash, plan.VertexCount);
    for (const auto& binding : plan.VertexBindings) {
        HashValue(hash, binding.Binding);
        HashValue(hash, binding.ByteStride);
        HashValue(hash, binding.InputRate);
    }
    for (const auto& attribute : plan.VertexAttributes) {
        HashValue(hash, attribute.Location);
        HashValue(hash, attribute.Binding);
        HashValue(hash, attribute.Format);
        HashValue(hash, attribute.ComponentCount);
        HashValue(hash, attribute.ByteOffset);
    }
    for (const auto& texture : plan.Textures) {
        HashValue(hash, texture.Slot);
    }
    HashValue(hash, plan.State.Framebuffer.Width);
    HashValue(hash, plan.State.Framebuffer.Height);
    HashValue(hash, plan.State.Framebuffer.ColorFormat);
    HashValue(hash, plan.State.Framebuffer.DepthFormat);
    HashValue(hash, plan.State.Framebuffer.Flipped);
    HashValue(hash, plan.State.Topology);
    return hash;
}

// Off while TRIAEVUM_MODEL_PAIR_AUDIT builds the pairing without identities.
bool gModelPairsSuspended = false;

// The same domain and attribution, or one side unattributed for a frame.
bool CompatibleComposition(const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanDrawPlan& current) {
    const bool domain = previous.CompositionDomain == current.CompositionDomain ||
                        previous.CompositionDomain == Oot3dPicaCompositionDomain::Unknown ||
                        current.CompositionDomain == Oot3dPicaCompositionDomain::Unknown;
    // A model fading in or out moves between the opaque and transparent world
    // passes: still the same model.
    const auto world = [](Oot3dPicaCompositionLayer layer) {
        return layer == Oot3dPicaCompositionLayer::OpaqueWorld || layer == Oot3dPicaCompositionLayer::TransparentWorld;
    };
    // Draws of two different model instances (MM3D) are never the same draw.
    static const bool modelPairs = std::getenv("TRIAEVUM_NO_MODEL_PAIRS") == nullptr;
    if (modelPairs && !gModelPairsSuspended &&
        previous.Composition.Provenance == Oot3dPicaCompositionProvenance::NativeModelInstance &&
        current.Composition.Provenance == Oot3dPicaCompositionProvenance::NativeModelInstance &&
        previous.Composition.NativeValue != current.Composition.NativeValue)
        return false;
    return domain && (previous.Composition == current.Composition ||
                      previous.Composition.Layer == Oot3dPicaCompositionLayer::Unknown ||
                      current.Composition.Layer == Oot3dPicaCompositionLayer::Unknown ||
                      (world(previous.Composition.Layer) && world(current.Composition.Layer)));
}

bool HaveMatchingStructuralLayout(const Oot3dPicaVulkanDrawPlan& previous,
                                  const Oot3dPicaVulkanDrawPlan& current,
                                  bool ignoreFragmentState) {
  if (!CompatibleComposition(previous, current) ||
      previous.VertexShader.StateKey != current.VertexShader.StateKey ||
      (!ignoreFragmentState && previous.FragmentShader.StateKey != current.FragmentShader.StateKey) ||
      previous.Indexed != current.Indexed ||
      previous.IndicesAre16Bit != current.IndicesAre16Bit ||
      previous.BaseVertex != current.BaseVertex ||
      previous.VertexCount != current.VertexCount ||
      !EqualIndexBytes(previous, current) ||
      previous.VertexBindings.size() != current.VertexBindings.size() ||
      previous.VertexAttributes.size() != current.VertexAttributes.size() ||
      previous.Textures.size() != current.Textures.size() ||
      previous.State.Framebuffer.Width != current.State.Framebuffer.Width ||
      previous.State.Framebuffer.Height != current.State.Framebuffer.Height ||
      previous.State.Framebuffer.ColorFormat !=
          current.State.Framebuffer.ColorFormat ||
      previous.State.Framebuffer.DepthFormat !=
          current.State.Framebuffer.DepthFormat ||
      previous.State.Framebuffer.Flipped != current.State.Framebuffer.Flipped ||
      previous.State.Topology != current.State.Topology) {
    return false;
  }
    for (size_t index = 0; index < previous.State.VertexInput.Loaders.size(); ++index) {
        const auto& left = previous.State.VertexInput.Loaders[index];
        const auto& right = current.State.VertexInput.Loaders[index];
        if (left.DataOffset != right.DataOffset ||
            left.ByteStride != right.ByteStride ||
            left.ComponentCount != right.ComponentCount ||
            left.Components != right.Components) {
            return false;
        }
    }
    for (size_t index = 0; index < previous.VertexBindings.size(); ++index) {
        const auto& left = previous.VertexBindings[index];
        const auto& right = current.VertexBindings[index];
        if (left.Binding != right.Binding ||
            left.ByteStride != right.ByteStride ||
            left.InputRate != right.InputRate ||
            left.ResolvedBytes().size() != right.ResolvedBytes().size()) {
          return false;
        }
    }
    for (size_t index = 0; index < previous.VertexAttributes.size(); ++index) {
        const auto& left = previous.VertexAttributes[index];
        const auto& right = current.VertexAttributes[index];
        if (left.Location != right.Location ||
            left.Binding != right.Binding ||
            left.Format != right.Format ||
            left.ComponentCount != right.ComponentCount ||
            left.ByteOffset != right.ByteOffset) {
            return false;
        }
    }
    for (size_t index = 0; index < previous.Textures.size(); ++index) {
        const auto& left = previous.Textures[index];
        const auto& right = current.Textures[index];
        if (left.Slot != right.Slot || left.State.Enabled != right.State.Enabled ||
            left.State.Width != right.State.Width || left.State.Height != right.State.Height ||
            left.State.Format != right.State.Format || left.State.Type != right.State.Type ||
            left.State.WrapS != right.State.WrapS || left.State.WrapT != right.State.WrapT ||
            left.State.MinLinear != right.State.MinLinear || left.State.MagLinear != right.State.MagLinear) {
            return false;
        }
    }
    return true;
}

bool HaveMatchingStructuralLayout(const Oot3dPicaVulkanDrawPlan& previous,
                                  const Oot3dPicaVulkanDrawPlan& current) {
    return HaveMatchingStructuralLayout(previous, current, false);
}

// Structural identity without the fragment state.
uint64_t ComputeGeometryDrawIdentity(const Oot3dPicaVulkanDrawPlan& plan) {
    uint64_t hash = kFnvOffset;
    HashValue(hash, plan.VertexShader.StateKey);
    HashValue(hash, plan.Indexed);
    HashValue(hash, plan.IndicesAre16Bit);
    HashValue(hash, plan.BaseVertex);
    HashValue(hash, plan.VertexCount);
    for (const auto& binding : plan.VertexBindings) {
        HashValue(hash, binding.Binding);
        HashValue(hash, binding.ByteStride);
        HashValue(hash, binding.ResolvedBytes().size());
    }
    for (const uint8_t byte : plan.ResolvedIndexBytes()) HashValue(hash, byte);
    for (const auto& texture : plan.Textures) {
        HashValue(hash, texture.State.PhysicalAddress);
        HashValue(hash, texture.State.Width);
        HashValue(hash, texture.State.Height);
    }
    HashValue(hash, plan.State.Framebuffer.Width);
    HashValue(hash, plan.State.Framebuffer.Height);
    return hash;
}

bool HaveMatchingPipelineLayout(const Oot3dPicaVulkanDrawPlan& previous,
                                const Oot3dPicaVulkanDrawPlan& current) {
    if (!CompatibleComposition(previous, current) ||
        previous.VertexShader.StateKey != current.VertexShader.StateKey ||
        previous.FragmentShader.StateKey != current.FragmentShader.StateKey ||
        previous.Indexed != current.Indexed ||
        previous.IndicesAre16Bit != current.IndicesAre16Bit ||
        previous.VertexCount != current.VertexCount ||
        previous.VertexBindings.size() != current.VertexBindings.size() ||
        previous.VertexAttributes.size() != current.VertexAttributes.size() ||
        previous.Textures.size() != current.Textures.size() ||
        previous.State.Framebuffer.Width != current.State.Framebuffer.Width ||
        previous.State.Framebuffer.Height != current.State.Framebuffer.Height ||
        previous.State.Framebuffer.ColorFormat !=
            current.State.Framebuffer.ColorFormat ||
        previous.State.Framebuffer.DepthFormat !=
            current.State.Framebuffer.DepthFormat ||
        previous.State.Framebuffer.Flipped !=
            current.State.Framebuffer.Flipped ||
        previous.State.Topology != current.State.Topology) {
        return false;
    }
    for (size_t index = 0; index < previous.VertexBindings.size(); ++index) {
        const auto& left = previous.VertexBindings[index];
        const auto& right = current.VertexBindings[index];
        if (left.Binding != right.Binding ||
            left.ByteStride != right.ByteStride ||
            left.InputRate != right.InputRate) {
            return false;
        }
    }
    for (size_t index = 0; index < previous.VertexAttributes.size(); ++index) {
        const auto& left = previous.VertexAttributes[index];
        const auto& right = current.VertexAttributes[index];
        if (left.Location != right.Location || left.Binding != right.Binding ||
            left.Format != right.Format ||
            left.ComponentCount != right.ComponentCount ||
            left.ByteOffset != right.ByteOffset) {
            return false;
        }
    }
    for (size_t index = 0; index < previous.Textures.size(); ++index) {
        if (previous.Textures[index].Slot != current.Textures[index].Slot) {
            return false;
        }
    }
    return true;
}

enum class VisualDrawMatchKind : uint8_t {
    StrictUnique,
    StrictOrdinal,
    StructuralOrdinal,
    PipelineOrdinal,
    // Same model and geometry, other fragment state (the lights on a model
    // change as Tatl or a lamp comes near): its matrices are blended, its
    // fragment state is the current one.
    GeometryOrdinal,
    // The same model instance's draw at the same position in its submission,
    // as recorded from the game's model draw (MM3D).
    ModelInstance,
};

// One model's own motion from the previous frame to the current one (Link
// turning round): current = R * (previous - PreviousPivot) + CurrentPivot,
// shared by all its bones and draws. Blending each bone as its own rotation
// around its own origin while the origins move in straight lines tore the
// joints apart in between (a hand off the wrist, legs apart, turning fast).
struct ModelMotion {
    std::array<float, 4> Rotation{0.0F, 0.0F, 0.0F, 1.0F};  // x y z w
    std::array<float, 3> PreviousPivot{}, CurrentPivot{};
    float Radius = 0.0F;  // how far its bones reach from the pivot
    // Measured with the previous pose seen from the current view (the rigid
    // camera path), otherwise in each frame's own view.
    bool RigidCameraSpace = false;
    // Turned so far in one game frame that the game itself snaps (Link
    // reversing: 180 degrees, either way round): shown at the nearer frame.
    bool Snap = false;
};

// One bone of a model skeleton between two frames, in its parent's space
// (the joint stays where the parent's in-between pose puts it).
struct PoseBone {
    int16_t Parent = -1;  // -1: interpolated on its own (a root)
    bool Decomposed = false;
    std::array<float, 4> Rotation0{}, Rotation1{};  // x y z w, same hemisphere
    std::array<std::array<float, 3>, 3> Shape0{}, Shape1{};  // scale and shear
    std::array<float, 3> Translation0{}, Translation1{};
    std::array<std::array<float, 4>, 3> Local0{}, Local1{};  // blended linearly when not decomposed
    // Its orientation (and scale) in the view: blended there, the joint
    // placed by the parent.
    uint16_t Root = 0;  // the ancestor its sharp turn is blended against
    bool WorldDecomposed = false;
    bool SharpTurn = false;  // turned far in its parent's space in one game frame
    std::array<float, 4> WorldRotation0{}, WorldRotation1{};
    std::array<std::array<float, 3>, 3> WorldShape0{}, WorldShape1{};
};

// A model's skeleton in both frames (MM3D: the game's own bones and parents).
struct PoseTransition {
    const Oot3dPicaModelPose* Previous = nullptr;
    const Oot3dPicaModelPose* Current = nullptr;
    std::vector<PoseBone> Bones;
    std::vector<uint16_t> Order;  // parents before children
    // The previous bones as the snap shows them: from the current view on
    // the rigid camera path, otherwise as drawn.
    std::vector<std::array<std::array<float, 4>, 3>> PreviousBones;
    bool RigidCameraSpace = false;
    bool Snap = false;
    bool Moving = false;
    // Held by another model's bone (an item in a hand, its own model placed
    // from the hand each frame): the root bones follow that bone.
    uint32_t Carrier = std::numeric_limits<uint32_t>::max();
    uint16_t CarrierBone = 0;
    // root, its place on the bone in each frame
    std::vector<std::pair<uint16_t, std::array<std::array<std::array<float, 4>, 3>, 2>>> HeldRoots;
};

struct VisualDrawMatch {
    size_t PreviousIndex = 0;
    VisualDrawMatchKind Kind = VisualDrawMatchKind::StrictUnique;
};

struct VisualDrawMatching {
    std::vector<std::optional<VisualDrawMatch>> PreviousByCurrent;
    size_t StrictUniqueMatches = 0;
    size_t StrictOrdinalMatches = 0;
    size_t StructuralOrdinalMatches = 0;
    size_t PipelineOrdinalMatches = 0;
    size_t GeometryOrdinalMatches = 0;
    size_t ModelInstanceMatches = 0;
    size_t AmbiguousPrevious = 0;
    size_t AmbiguousCurrent = 0;
    size_t RejectedJumps = 0;
    size_t RepairedPairs = 0;
    size_t ReorderedPairs = 0;
    size_t CameraVotes = 0, CameraCandidates = 0;
    // MM3D/OoT3D world draws carry model-VIEW matrices (f[20..], f[4..6] is
    // identity): the camera's frame-to-frame change, as a view-space affine
    // map D with previous = D * current for static geometry.
    bool CameraDeltaValid = false;
    bool CameraCut = false;
    // In-between views as a rigid camera motion (fast turns), see below.
    bool RigidCamera = false;
    std::array<std::array<float, 4>, 3> CameraDelta{};
    size_t UnmatchedPrevious = 0;
    size_t UnmatchedCurrent = 0;
    std::vector<ModelMotion> Motions;
    std::vector<uint32_t> MotionByCurrent;  // index into Motions, or none
    std::vector<PoseTransition> Poses;
    // Previous poses made up for models new in view (D * current: still in
    // the world), kept for the transitions pointing at them.
    std::deque<Oot3dPicaModelPose> StillPoses;
    std::vector<uint32_t> PoseOrder;  // carriers before what they hold
    std::vector<uint32_t> PoseByCurrent;  // index into Poses, or none
    // A model's parts (meshes) drawn in one frame only (the game switched
    // them): shown with the nearer frame.
    struct VanishedPart {
        size_t PreviousIndex = 0;
        uint32_t Pose = 0;
        size_t After = 0;  // drawn after this current draw
    };
    std::vector<bool> PoseAppeared;    // per current draw: new part, hidden before alpha 0.5
    std::vector<VanishedPart> PoseVanished;  // previous parts gone, drawn before alpha 0.5
    std::vector<bool> PreviousInPose;  // per previous draw: a part of a paired model

    const ModelMotion* MotionFor(size_t currentIndex) const {
        if (currentIndex >= MotionByCurrent.size() || MotionByCurrent[currentIndex] >= Motions.size()) return nullptr;
        return &Motions[MotionByCurrent[currentIndex]];
    }
};

struct VisualDrawDeltaFlags {
    bool Continuous = false;
    bool VertexUniforms = false;
    bool FragmentUniforms = false;
    bool Viewport = false;
    bool BlendConstantColor = false;
    bool PerInstanceVertex = false;
    bool PerVertex = false;
};

template <typename Identity> struct VisualIdentityValues {
    const std::vector<uint64_t>* Existing = nullptr;
    const std::vector<Oot3dPicaVulkanDrawPlan>* Draws = nullptr;
    Identity Compute{};
    mutable std::vector<uint64_t> Owned;
    mutable std::vector<bool> Ready;

    uint64_t operator[](size_t index) const {
        if (Existing != nullptr) {
            return (*Existing)[index];
        }
        if (!Ready[index]) {
            Owned[index] = Compute((*Draws)[index]);
            Ready[index] = true;
        }
        return Owned[index];
    }
};

template <typename Identity>
VisualIdentityValues<Identity> ResolveVisualIdentities(const std::vector<uint64_t>& existing,
                                                       const std::vector<Oot3dPicaVulkanDrawPlan>& draws,
                                                       Identity identity) {
    VisualIdentityValues<Identity> values;
    if (existing.size() == draws.size()) {
        values.Existing = &existing;
        return values;
    }
    values.Draws = &draws;
    values.Compute = identity;
    values.Owned.resize(draws.size());
    values.Ready.resize(draws.size(), false);
    return values;
}

template <typename Identity>
VisualIdentityValues<Identity> ResolveVisualIdentities(const std::vector<Oot3dPicaVulkanDrawPlan>& draws,
                                                       Identity identity) {
    static const std::vector<uint64_t> empty;
    return ResolveVisualIdentities(empty, draws, identity);
}

template <typename PreviousIdentities, typename CurrentIdentities>
void MatchEqualCardinalityGroups(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current,
    const std::vector<bool>& previousAlreadyMatched,
    std::vector<bool>& previousMatched,
    std::vector<bool>& currentMatched,
    std::vector<std::optional<VisualDrawMatch>>& previousByCurrent,
                                 const PreviousIdentities& previousIdentities,
                                 const CurrentIdentities& currentIdentities, VisualDrawMatchKind uniqueKind,
    VisualDrawMatchKind repeatedKind, size_t& uniqueMatches,
    size_t& repeatedMatches, size_t* ambiguousPrevious,
    size_t* ambiguousCurrent) {
    std::unordered_map<uint64_t, std::vector<size_t>> previousGroups;
    std::unordered_map<uint64_t, std::vector<size_t>> currentGroups;
    for (size_t index = 0; index < previous.Draws.size(); ++index) {
        if (!previousAlreadyMatched[index] && !previousMatched[index]) {
            previousGroups[previousIdentities[index]].push_back(index);
        }
    }
    for (size_t index = 0; index < current.Draws.size(); ++index) {
        if (!currentMatched[index]) {
            currentGroups[currentIdentities[index]].push_back(index);
        }
    }
    for (const auto& [key, currentIndices] : currentGroups) {
        const auto found = previousGroups.find(key);
        if (found == previousGroups.end()) {
            continue;
        }
        const auto& previousIndices = found->second;
        if (previousIndices.size() != currentIndices.size()) {
            if (ambiguousPrevious != nullptr && previousIndices.size() > 1U) {
                *ambiguousPrevious += previousIndices.size();
            }
            if (ambiguousCurrent != nullptr && currentIndices.size() > 1U) {
                *ambiguousCurrent += currentIndices.size();
            }
            continue;
        }
        const bool unique = currentIndices.size() == 1U;
        for (size_t ordinal = 0; ordinal < currentIndices.size(); ++ordinal) {
            const size_t previousIndex = previousIndices[ordinal];
            const size_t currentIndex = currentIndices[ordinal];
            if (!HaveMatchingStructuralLayout(previous.Draws[previousIndex],
                                              current.Draws[currentIndex])) {
                continue;
            }
            const auto kind = unique ? uniqueKind : repeatedKind;
            previousByCurrent[currentIndex] = {previousIndex, kind};
            previousMatched[previousIndex] = true;
            currentMatched[currentIndex] = true;
            if (unique) {
                ++uniqueMatches;
            } else {
                ++repeatedMatches;
            }
        }
    }
}

struct VisualAlignmentCell {
    size_t Matches = 0;
    double Cost = 0.0;
    uint8_t Action = 0;
};

bool IsBetterAlignment(const VisualAlignmentCell& candidate,
                       const VisualAlignmentCell& current) {
    if (candidate.Matches != current.Matches) {
        return candidate.Matches > current.Matches;
    }
    if (candidate.Cost != current.Cost) {
        return candidate.Cost < current.Cost;
    }
    return candidate.Action > current.Action;
}

template <typename PreviousIdentities, typename CurrentIdentities, typename Compatible>
void MatchMonotonicGroups(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current,
    std::vector<bool>& previousMatched,
    std::vector<bool>& currentMatched,
    std::vector<std::optional<VisualDrawMatch>>& previousByCurrent,
                          const PreviousIdentities& previousIdentities, const CurrentIdentities& currentIdentities, Compatible compatible, VisualDrawMatchKind kind,
    size_t& matchCount) {
    std::unordered_map<uint64_t, std::vector<size_t>> previousGroups;
    std::unordered_map<uint64_t, std::vector<size_t>> currentGroups;
    for (size_t index = 0; index < previous.Draws.size(); ++index) {
        if (!previousMatched[index]) {
            previousGroups[previousIdentities[index]].push_back(index);
        }
    }
    for (size_t index = 0; index < current.Draws.size(); ++index) {
        if (!currentMatched[index]) {
            currentGroups[currentIdentities[index]].push_back(index);
        }
    }
    for (const auto& [key, currentIndices] : currentGroups) {
        const auto found = previousGroups.find(key);
        if (found == previousGroups.end()) {
            continue;
        }
        const auto& previousIndices = found->second;
        const size_t rowWidth = currentIndices.size() + 1U;
        std::vector<VisualAlignmentCell> cells(
            (previousIndices.size() + 1U) * rowWidth);
        const auto cell = [&](size_t previousCount,
                              size_t currentCount) -> VisualAlignmentCell& {
            return cells[previousCount * rowWidth + currentCount];
        };
        for (size_t previousCount = 1; previousCount <= previousIndices.size();
             ++previousCount) {
            cell(previousCount, 0).Action = 1U;
        }
        for (size_t currentCount = 1; currentCount <= currentIndices.size();
             ++currentCount) {
            cell(0, currentCount).Action = 2U;
        }
        for (size_t previousCount = 1; previousCount <= previousIndices.size();
             ++previousCount) {
            for (size_t currentCount = 1;
                 currentCount <= currentIndices.size(); ++currentCount) {
                VisualAlignmentCell best =
                    cell(previousCount - 1U, currentCount);
                best.Action = 1U;
                auto skipCurrent =
                    cell(previousCount, currentCount - 1U);
                skipCurrent.Action = 2U;
                if (IsBetterAlignment(skipCurrent, best)) {
                    best = skipCurrent;
                }
                const size_t previousIndex =
                    previousIndices[previousCount - 1U];
                const size_t currentIndex =
                    currentIndices[currentCount - 1U];
                if (compatible(previous.Draws[previousIndex],
                               current.Draws[currentIndex])) {
                    auto match =
                        cell(previousCount - 1U, currentCount - 1U);
                    ++match.Matches;
                    const double previousOrder =
                        (static_cast<double>(previousIndex) + 0.5) /
                        std::max<size_t>(previous.Draws.size(), 1U);
                    const double currentOrder =
                        (static_cast<double>(currentIndex) + 0.5) /
                        std::max<size_t>(current.Draws.size(), 1U);
                    match.Cost += NormalizedVertexUniformDelta(
                                      previous.Draws[previousIndex],
                                      current.Draws[currentIndex]) +
                                  std::abs(previousOrder - currentOrder);
                    match.Action = 3U;
                    if (IsBetterAlignment(match, best)) {
                        best = match;
                    }
                }
                cell(previousCount, currentCount) = best;
            }
        }

        size_t previousCount = previousIndices.size();
        size_t currentCount = currentIndices.size();
        while (previousCount != 0U || currentCount != 0U) {
            const uint8_t action = cell(previousCount, currentCount).Action;
            if (action == 3U) {
                const size_t previousIndex =
                    previousIndices[previousCount - 1U];
                const size_t currentIndex =
                    currentIndices[currentCount - 1U];
                previousByCurrent[currentIndex] = {previousIndex, kind};
                previousMatched[previousIndex] = true;
                currentMatched[currentIndex] = true;
                ++matchCount;
                --previousCount;
                --currentCount;
            } else if (action == 2U) {
                --currentCount;
            } else if (action == 1U) {
                --previousCount;
            } else {
                break;
            }
        }
    }
}


using Affine34 = std::array<std::array<float, 4>, 3>;

Affine34 AffineAt(const Oot3dPicaVertexUniformState& uniforms, size_t first) {
    return {uniforms.Floats[first], uniforms.Floats[first + 1U], uniforms.Floats[first + 2U]};
}

Affine34 MultiplyAffine(const Affine34& left, const Affine34& right) {
    Affine34 result{};
    for (size_t row = 0; row < 3U; ++row) {
        for (size_t column = 0; column < 4U; ++column) {
            float value = column == 3U ? left[row][3] : 0.0F;
            for (size_t k = 0; k < 3U; ++k) value += left[row][k] * right[k][column];
            result[row][column] = value;
        }
    }
    return result;
}

std::optional<Affine34> InvertAffine(const Affine34& m) {
    const float a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2],
                g = m[2][0], h = m[2][1], i = m[2][2];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(det) || std::abs(det) < 1.0e-12F) return std::nullopt;
    const float inv = 1.0F / det;
    Affine34 r{};
    r[0][0] = (e * i - f * h) * inv; r[0][1] = (c * h - b * i) * inv; r[0][2] = (b * f - c * e) * inv;
    r[1][0] = (f * g - d * i) * inv; r[1][1] = (a * i - c * g) * inv; r[1][2] = (c * d - a * f) * inv;
    r[2][0] = (d * h - e * g) * inv; r[2][1] = (b * g - a * h) * inv; r[2][2] = (a * e - b * d) * inv;
    for (size_t row = 0; row < 3U; ++row)
        r[row][3] = -(r[row][0] * m[0][3] + r[row][1] * m[1][3] + r[row][2] * m[2][3]);
    return r;
}

bool UsesModelViewLayout(const Oot3dPicaVertexUniformState& uniforms) {
    // World draws: a perspective projection (f[3], the w row, takes -z: no
    // constant w, which 2D/UI orthographic draws have), f[4..6] identity and
    // model-view matrices in f[20..]. UI draws must never move with the camera.
    const auto& w = uniforms.Floats[3];
    if (!(std::abs(w[3]) < 1.0e-3F) || !(std::abs(w[0]) + std::abs(w[1]) + std::abs(w[2]) > 0.5F))
        return false;
    for (size_t row = 0; row < 3U; ++row)
        for (size_t column = 0; column < 4U; ++column)
            if (std::abs(uniforms.Floats[4U + row][column] - (row == column ? 1.0F : 0.0F)) > 1.0e-5F)
                return false;
    return true;
}

bool InterfaceDrawSnaps(const Oot3dPicaVulkanDrawPlan& current) {
    return gInterfaceSnaps && !UsesModelViewLayout(current.VertexShader.Uniforms);
}

// 2D/UI draws: an orthographic projection (f[3], the w row, is 0 0 0 1).
bool UsesOrthographicProjection(const Oot3dPicaVertexUniformState& uniforms) {
    const auto& w = uniforms.Floats[3];
    return std::abs(w[3] - 1.0F) < 1.0e-3F &&
           std::abs(w[0]) + std::abs(w[1]) + std::abs(w[2]) < 1.0e-3F;
}

bool SimilarAffine(const Affine34& left, const Affine34& right) {
    for (size_t row = 0; row < 3U; ++row) {
        for (size_t column = 0; column < 3U; ++column)
            if (std::abs(left[row][column] - right[row][column]) > 0.02F) return false;
        // Precision of far scenery (thousands of units away) scales the tolerance.
        if (std::abs(left[row][3] - right[row][3]) > 2.0F + 0.01F * std::abs(left[row][3])) return false;
    }
    return true;
}

// Minimal-cost assignment (Hungarian, rows <= columns): column per row.
std::vector<size_t> MinimalCostAssignment(const std::vector<std::vector<double>>& cost) {
    const size_t rows = cost.size(), columns = rows ? cost[0].size() : 0U;
    constexpr double kInfinity = std::numeric_limits<double>::infinity();
    std::vector<double> u(rows + 1U, 0.0), v(columns + 1U, 0.0);
    std::vector<size_t> owner(columns + 1U, 0U), way(columns + 1U, 0U);
    for (size_t row = 1; row <= rows; ++row) {
        owner[0] = row;
        size_t column0 = 0;
        std::vector<double> minimum(columns + 1U, kInfinity);
        std::vector<bool> used(columns + 1U, false);
        do {
            used[column0] = true;
            const size_t row0 = owner[column0];
            double delta = kInfinity;
            size_t column1 = 0;
            for (size_t column = 1; column <= columns; ++column) {
                if (used[column]) continue;
                const double reduced = cost[row0 - 1U][column - 1U] - u[row0] - v[column];
                if (reduced < minimum[column]) { minimum[column] = reduced; way[column] = column0; }
                if (minimum[column] < delta) { delta = minimum[column]; column1 = column; }
            }
            for (size_t column = 0; column <= columns; ++column) {
                if (used[column]) { u[owner[column]] += delta; v[column] -= delta; }
                else minimum[column] -= delta;
            }
            column0 = column1;
        } while (owner[column0] != 0U);
        do {
            const size_t column1 = way[column0];
            owner[column0] = owner[column1];
            column0 = column1;
        } while (column0 != 0U);
    }
    std::vector<size_t> result(rows, 0U);
    for (size_t column = 1; column <= columns; ++column)
        if (owner[column] != 0U) result[owner[column] - 1U] = column - 1U;
    return result;
}

// Pairs draws by the model instance that submitted them: the k-th draw of
// (model, position in its submission) with the previous frame's k-th, when
// it is still the same mesh. Draw order does not matter.
void MatchModelInstances(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                         std::vector<bool>& previousMatched, std::vector<bool>& currentMatched,
                         VisualDrawMatching& matching) {
    const auto keys = [](const std::vector<Oot3dPicaVulkanDrawPlan>& draws) {
        std::vector<std::optional<uint64_t>> result(draws.size());
        std::unordered_map<uint64_t, uint32_t> occurrences;
        for (size_t index = 0; index < draws.size(); ++index) {
            const auto& composition = draws[index].Composition;
            if (composition.Provenance != Oot3dPicaCompositionProvenance::NativeModelInstance) continue;
            // The mesh (its index data) rather than its place in the model's
            // list: a model that adds or drops a mesh (Link drawing or putting
            // away the sword, switching hands) shifted every later place.
            const uint64_t mesh = draws[index].IndexPhysicalAddress != 0U
                                      ? static_cast<uint64_t>(draws[index].IndexPhysicalAddress)
                                      : (1ULL << 32U) | composition.Ordinal;
            uint64_t base = static_cast<uint64_t>(composition.NativeValue) * 0x9E3779B97F4A7C15ULL;
            base ^= mesh + 0x632BE59BD9B4E019ULL + (base << 6U) + (base >> 2U);
            result[index] = base ^ (static_cast<uint64_t>(occurrences[base]++) * 0xD6E8FEB86659FD93ULL);
        }
        return result;
    };
    const auto previousKeys = keys(previous.Draws);
    const auto currentKeys = keys(current.Draws);
    std::unordered_map<uint64_t, size_t> previousByKey;
    for (size_t index = 0; index < previousKeys.size(); ++index)
        if (previousKeys[index].has_value()) previousByKey.emplace(*previousKeys[index], index);
    for (size_t currentIndex = 0; currentIndex < currentKeys.size(); ++currentIndex) {
        if (!currentKeys[currentIndex].has_value()) continue;
        const auto found = previousByKey.find(*currentKeys[currentIndex]);
        if (found == previousByKey.end() || previousMatched[found->second] ||
            !HaveMatchingStructuralLayout(previous.Draws[found->second], current.Draws[currentIndex]))
            continue;
        previousMatched[found->second] = true;
        currentMatched[currentIndex] = true;
        matching.PreviousByCurrent[currentIndex] = VisualDrawMatch{found->second, VisualDrawMatchKind::ModelInstance};
        ++matching.ModelInstanceMatches;
    }
}

void ComputeModelMotions(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                         VisualDrawMatching& matching);
void ComputePoseTransitions(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                            VisualDrawMatching& matching);

VisualDrawMatching BuildVisualDrawMatching(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current) {
    VisualDrawMatching matching;
    matching.PreviousByCurrent.resize(current.Draws.size());
    std::vector<bool> noPreviousMatches(previous.Draws.size(), false);
    std::vector<bool> previousMatched(previous.Draws.size(), false);
    std::vector<bool> currentMatched(current.Draws.size(), false);
    const auto previousStrict =
        ResolveVisualIdentities(previous.StrictDrawIdentities, previous.Draws, ComputeOot3dPicaVisualDrawIdentity);
    const auto currentStrict =
        ResolveVisualIdentities(current.StrictDrawIdentities, current.Draws, ComputeOot3dPicaVisualDrawIdentity);
    const auto previousStructural = ResolveVisualIdentities(previous.Draws, ComputeStructuralDrawIdentity);
    const auto currentStructural = ResolveVisualIdentities(current.Draws, ComputeStructuralDrawIdentity);
    const auto previousPipeline = ResolveVisualIdentities(previous.Draws, ComputePipelineDrawIdentity);
    const auto currentPipeline = ResolveVisualIdentities(current.Draws, ComputePipelineDrawIdentity);

    static const bool modelPairs = std::getenv("TRIAEVUM_NO_MODEL_PAIRS") == nullptr;
    if (modelPairs && !gModelPairsSuspended)
        MatchModelInstances(previous, current, previousMatched, currentMatched, matching);

    MatchEqualCardinalityGroups(
        previous, current, previousMatched, previousMatched, currentMatched,
        matching.PreviousByCurrent, previousStrict, currentStrict,
        VisualDrawMatchKind::StrictUnique,
        VisualDrawMatchKind::StrictOrdinal, matching.StrictUniqueMatches,
        matching.StrictOrdinalMatches, &matching.AmbiguousPrevious,
        &matching.AmbiguousCurrent);

    size_t unusedStructuralUniqueMatches = 0;
    MatchEqualCardinalityGroups(
        previous, current, previousMatched, previousMatched, currentMatched,
        matching.PreviousByCurrent, previousStructural, currentStructural,
        VisualDrawMatchKind::StructuralOrdinal,
        VisualDrawMatchKind::StructuralOrdinal,
        unusedStructuralUniqueMatches, matching.StructuralOrdinalMatches,
        nullptr, nullptr);
    matching.StructuralOrdinalMatches += unusedStructuralUniqueMatches;

    MatchMonotonicGroups(
        previous, current, previousMatched, currentMatched,
        matching.PreviousByCurrent,
                         previousStructural, currentStructural,
        [](const Oot3dPicaVulkanDrawPlan& before, const Oot3dPicaVulkanDrawPlan& after) {
            return HaveMatchingStructuralLayout(before, after);
        },
        VisualDrawMatchKind::StructuralOrdinal,
        matching.StructuralOrdinalMatches);
    MatchMonotonicGroups(
        previous, current, previousMatched, currentMatched,
        matching.PreviousByCurrent,
                         previousPipeline, currentPipeline,
        HaveMatchingPipelineLayout, VisualDrawMatchKind::PipelineOrdinal,
        matching.PipelineOrdinalMatches);
    // World draws whose fragment state changed (lights): same geometry.
    static const bool geometryPairs = std::getenv("TRIAEVUM_NO_GEOMETRY_PAIRS") == nullptr;
    const auto previousGeometry = ResolveVisualIdentities(previous.Draws, ComputeGeometryDrawIdentity);
    const auto currentGeometry = ResolveVisualIdentities(current.Draws, ComputeGeometryDrawIdentity);
    if (geometryPairs) {
        MatchMonotonicGroups(
            previous, current, previousMatched, currentMatched, matching.PreviousByCurrent, previousGeometry,
            currentGeometry,
            [](const Oot3dPicaVulkanDrawPlan& before, const Oot3dPicaVulkanDrawPlan& after) {
                return UsesModelViewLayout(before.VertexShader.Uniforms) &&
                       UsesModelViewLayout(after.VertexShader.Uniforms) &&
                       HaveMatchingStructuralLayout(before, after, true);
            },
            VisualDrawMatchKind::GeometryOrdinal, matching.GeometryOrdinalMatches);
    }

    // UI draws are paired only by strict identity (same textures and vertex
    // data). A structural or pipeline match pairs two different elements of
    // the same shape (icons, digits, prompts): when one appears or goes, its
    // neighbour flew in from the other's place for the in-between frames.
    static const bool looseUiPairs = std::getenv("TRIAEVUM_UI_LOOSE_PAIRS") != nullptr;
    if (!looseUiPairs) {
        for (size_t currentIndex = 0; currentIndex < matching.PreviousByCurrent.size(); ++currentIndex) {
            auto& match = matching.PreviousByCurrent[currentIndex];
            if (!match.has_value() || match->Kind == VisualDrawMatchKind::StrictUnique ||
                match->Kind == VisualDrawMatchKind::StrictOrdinal ||
                !UsesOrthographicProjection(current.Draws[currentIndex].VertexShader.Uniforms) ||
                !UsesOrthographicProjection(previous.Draws[match->PreviousIndex].VertexShader.Uniforms))
                continue;
            (match->Kind == VisualDrawMatchKind::StructuralOrdinal ? matching.StructuralOrdinalMatches
                                                                    : matching.PipelineOrdinalMatches)--;
            static const bool uiTrace = std::getenv("TRIAEVUM_JUMP_TRACE") != nullptr;
            if (uiTrace) {
                // How far the dropped pairing would have blended (largest
                // change of a continuous vertex uniform).
                float worst = 0.0F;
                const auto& a = previous.Draws[match->PreviousIndex].VertexShader.Uniforms.Floats;
                const auto& b = current.Draws[currentIndex].VertexShader.Uniforms.Floats;
                for (size_t row = 0; row < 0x59U; ++row)
                    for (size_t k = 0; k < 4U; ++k)
                        if (std::isfinite(a[row][k]) && std::isfinite(b[row][k]))
                            worst = std::max(worst, std::abs(a[row][k] - b[row][k]));
                std::fprintf(stderr, "TRIAEVUM_UI_PAIR_DROPPED kind=%d change=%.2f\n",
                             static_cast<int>(match->Kind), worst);
            }
            previousMatched[match->PreviousIndex] = false;
            currentMatched[currentIndex] = false;
            match.reset();
        }
    }

    // An ordinal match can pair two different objects (similar NPCs or props
    // whose draw order changed); blending between them flings the object across
    // the screen in the in-between frames. Grezzo's shaders place a model with
    // f[20..22] (model/first bone to world; f[4..6] is the camera), so its world
    // position is that matrix's translation: a jump no object makes in one game
    // frame drops the pairing and the draw is shown as it is.
    if (FILE* trace = FlingTraceFile()) {
        // Every pairing whose model or any bone matrix moves > 30 units.
        for (size_t currentIndex = 0; currentIndex < matching.PreviousByCurrent.size(); ++currentIndex) {
            const auto& match = matching.PreviousByCurrent[currentIndex];
            if (!match.has_value()) continue;
            const auto& before = previous.Draws[match->PreviousIndex].VertexShader.Uniforms.Floats;
            const auto& after = current.Draws[currentIndex].VertexShader.Uniforms.Floats;
            float worst = 0.0F; size_t worstRow = 0;
            for (size_t row = 20U; row + 2U < 0x59U; row += 3U) {
                float squared = 0.0F;
                for (size_t k = 0; k < 3U; ++k) {
                    const float delta = after[row + k][3] - before[row + k][3];
                    squared += delta * delta;
                }
                if (std::isfinite(squared) && squared > worst) { worst = squared; worstRow = row; }
            }
            if (worst > 30.0F * 30.0F)
                std::fprintf(trace, "PAIR t=%.1f cur=%zu prev=%zu kind=%d jump=%.1f row=%zu draws=%zu/%zu\n",
                             FlingTraceMs(), currentIndex, match->PreviousIndex, static_cast<int>(match->Kind),
                             std::sqrt(worst), worstRow, previous.Draws.size(), current.Draws.size());
        }
    }
    if (std::getenv("TRIAEVUM_UNIFORM_DUMP")) {
        static int dumps = 0;
        if (dumps++ == 200) {
            for (size_t i = 0; i < current.Draws.size() && i < 40U; ++i) {
                const auto& f = current.Draws[i].VertexShader.Uniforms.Floats;
                std::fprintf(stderr, "DUMP %zu v4=[%.3f %.3f %.3f %.1f] v5=[%.3f %.3f %.3f %.1f] v6=[%.3f %.3f %.3f %.1f] m20=[%.3f %.3f %.3f %.1f] m21w=%.1f m22w=%.1f\n", i,
                    f[4][0], f[4][1], f[4][2], f[4][3], f[5][0], f[5][1], f[5][2], f[5][3], f[6][0], f[6][1], f[6][2], f[6][3],
                    f[20][0], f[20][1], f[20][2], f[20][3], f[21][3], f[22][3]);
            }
        }
    }
    // Camera change from strictly paired world draws: static geometry agrees on
    // one D (previous = D * current); the value most pairs agree on wins.
    {
        std::vector<Affine34> candidates;
        std::vector<size_t> pairs;
        for (size_t currentIndex = 0; currentIndex < matching.PreviousByCurrent.size(); ++currentIndex) {
            const auto& match = matching.PreviousByCurrent[currentIndex];
            if (!match.has_value()) continue;
            if (!UsesModelViewLayout(previous.Draws[match->PreviousIndex].VertexShader.Uniforms) ||
                !UsesModelViewLayout(current.Draws[currentIndex].VertexShader.Uniforms))
                continue;
            pairs.push_back(currentIndex);
        }
        // Spread the sample over the whole frame: the first draws are Link's
        // parts and camera-facing sprites, which outvoted the static scenery.
        const size_t stride = std::max<size_t>(1U, pairs.size() / 96U);
        for (size_t k = 0; k < pairs.size(); k += stride) {
            const size_t currentIndex = pairs[k];
            const auto& match = matching.PreviousByCurrent[currentIndex];
            const auto inverse = InvertAffine(AffineAt(current.Draws[currentIndex].VertexShader.Uniforms, 20U));
            if (!inverse) continue;
            candidates.push_back(MultiplyAffine(
                AffineAt(previous.Draws[match->PreviousIndex].VertexShader.Uniforms, 20U), *inverse));
        }
        size_t bestVotes = 0;
        for (const auto& candidate : candidates) {
            const size_t votes = static_cast<size_t>(std::count_if(
                candidates.begin(), candidates.end(),
                [&](const Affine34& other) { return SimilarAffine(candidate, other); }));
            if (votes > bestVotes) { bestVotes = votes; matching.CameraDelta = candidate; }
        }
        // The camera is the average of the agreeing samples, not one of them: a
        // single sample carried its own error, so unpaired scenery (placed with
        // it) wobbled slightly against the paired draws.
        // The loose agreement (0.02 per entry, ~1 degree) also took in Link's
        // parts while the camera followed him: the average leaned 0.2-0.8
        // degrees toward them and far scenery (the field, a mountain 2000+
        // units away) was 5-60 units off in between frames. Static scenery
        // agrees on the camera to float precision: the average is over the
        // largest tight group inside the winner's agreement, when it has 4+.
        static const bool looseAverage = std::getenv("TRIAEVUM_LOOSE_CAMERA_AVERAGE") != nullptr;
        const auto tight = [](const Affine34& left, const Affine34& right) {
            for (size_t row = 0; row < 3U; ++row) {
                for (size_t column = 0; column < 3U; ++column)
                    if (std::abs(left[row][column] - right[row][column]) > 0.0015F) return false;
                if (std::abs(left[row][3] - right[row][3]) > 0.25F + 0.001F * std::abs(left[row][3])) return false;
            }
            return true;
        };
        bool tightAverage = false;
        if (bestVotes > 0U && !looseAverage) {
            const Affine34 winner = matching.CameraDelta;
            size_t bestTight = 0;
            const Affine34* center = nullptr;
            for (const auto& candidate : candidates) {
                if (!SimilarAffine(winner, candidate)) continue;
                const size_t votes = static_cast<size_t>(std::count_if(
                    candidates.begin(), candidates.end(), [&](const Affine34& other) { return tight(candidate, other); }));
                if (votes > bestTight) { bestTight = votes; center = &candidate; }
            }
            if (center != nullptr && bestTight >= 4U) {
                const Affine34 middle = *center;
                Affine34 sum{};
                size_t count = 0;
                for (const auto& candidate : candidates) {
                    if (!tight(middle, candidate)) continue;
                    for (size_t row = 0; row < 3U; ++row)
                        for (size_t column = 0; column < 4U; ++column) sum[row][column] += candidate[row][column];
                    ++count;
                }
                for (size_t row = 0; row < 3U; ++row)
                    for (size_t column = 0; column < 4U; ++column)
                        matching.CameraDelta[row][column] = sum[row][column] / static_cast<float>(count);
                tightAverage = true;
            }
        }
        if (bestVotes > 0U && !tightAverage) {
            const Affine34 winner = matching.CameraDelta;
            Affine34 sum{};
            size_t count = 0;
            for (const auto& candidate : candidates) {
                if (!SimilarAffine(winner, candidate)) continue;
                for (size_t row = 0; row < 3U; ++row)
                    for (size_t column = 0; column < 4U; ++column) sum[row][column] += candidate[row][column];
                ++count;
            }
            if (count > 0U)
                for (size_t row = 0; row < 3U; ++row)
                    for (size_t column = 0; column < 4U; ++column)
                        matching.CameraDelta[row][column] = sum[row][column] / static_cast<float>(count);
        }
        // Only a real majority of the scenery defines the camera; otherwise the
        // frame keeps plain blending (no guard, no correction).
        matching.CameraDeltaValid = bestVotes >= 4U && bestVotes * 10U >= candidates.size() * 3U;
        matching.CameraVotes = bestVotes;
        matching.CameraCandidates = candidates.size();
        // A camera cut (the view turns > 60 degrees or jumps > 400 units in
        // one game frame; no camera moves like that): blending the two views
        // flattened and flung the whole scene for the in-between frames. The
        // new frame is shown as it is.
        static const bool cutsDisabled = std::getenv("TRIAEVUM_NO_CAMERA_CUTS") != nullptr;
        if (matching.CameraDeltaValid && !cutsDisabled) {
            const auto& d = matching.CameraDelta;
            const float angle = std::acos(std::clamp((d[0][0] + d[1][1] + d[2][2] - 1.0F) * 0.5F, -1.0F, 1.0F));
            const float move = std::sqrt(d[0][3] * d[0][3] + d[1][3] * d[1][3] + d[2][3] * d[2][3]);
            if (angle > 60.0F * 0.0174533F || move > 400.0F) {
                matching.CameraCut = true;
                matching.CameraDeltaValid = false;
                for (auto& match : matching.PreviousByCurrent) match.reset();
                std::fill(previousMatched.begin(), previousMatched.end(), false);
                std::fill(currentMatched.begin(), currentMatched.end(), false);
                static const bool cutTrace = std::getenv("TRIAEVUM_JUMP_TRACE") != nullptr;
                if (cutTrace)
                    std::fprintf(stderr, "TRIAEVUM_CAMERA_CUT angle=%.1f move=%.1f\n", angle * 57.2958F, move);
            }
        }
    }
    // Identical draws (the two foot shadows of every character, NPCs of one
    // kind, repeated props) are told apart only by draw order, which changes
    // from frame to frame: the in-between frames slid a foot shadow onto the
    // other foot or an NPC toward its neighbour. Within every group of
    // identical draws paired by order, the pairing is redone as the one with
    // the least motion in the world (camera change removed), as things really
    // move little in one game frame.
    // A fast camera turn (LT recentring, quick swings: 20-50 degrees in one
    // game frame) blended linearly pulled the whole scene toward the camera
    // in the in-between frames (6-10% at 30-50 degrees): those frames move
    // the views as a rigid camera motion instead.
    // Slow turns too. Below the threshold a skinned room (Clock Town's ground
    // and walls are one posed model) was blended bone by bone in the view:
    // its bone turned about its own origin, far from the ground, so the
    // near ground was 0.5 (p95 1.6) game frames off its path in between
    // frames and covered 25-35% of each step there, the rest in the next:
    // it shook while the camera was nudged (1-5 degrees per game frame).
    // With the camera taken out first every still bone stays put.
    // TRIAEVUM_RIGID_CAMERA_MIN_DEGREES=5 restores the old threshold.
    if (matching.CameraDeltaValid) {
        static const bool forced = std::getenv("TRIAEVUM_RIGID_CAMERA") != nullptr;
        static const bool linearOnly = std::getenv("TRIAEVUM_LINEAR_CAMERA") != nullptr;
        static const float minimumDegrees = [] {
            const char* value = std::getenv("TRIAEVUM_RIGID_CAMERA_MIN_DEGREES");
            return value != nullptr ? std::strtof(value, nullptr) : 0.0F;
        }();
        const auto& d = matching.CameraDelta;
        const float angle = std::acos(std::clamp((d[0][0] + d[1][1] + d[2][2] - 1.0F) * 0.5F, -1.0F, 1.0F));
        matching.RigidCamera = !linearOnly && (forced || angle >= minimumDegrees * 0.0174533F);
    }
    static const bool orderPairs = std::getenv("TRIAEVUM_ORDER_PAIRS") != nullptr;
    // Left unpaired on purpose (not re-paired below).
    std::vector<bool> keepUnpaired(current.Draws.size(), false);
    // Without a camera estimate (no static scenery: the Song of Time's fall,
    // 128 identical clocks drawn in a new order every frame) the pairing of
    // identical draws is still corrected and checked, with the camera taken
    // as still: a pair that moved far is shown unblended, not as a clock
    // blended halfway into another one.
    const bool assumedStillCamera = !matching.CameraDeltaValid && !matching.CameraCut;
    std::array<std::array<float, 4>, 3> pairingCamera = matching.CameraDelta;
    if (assumedStillCamera) {
        pairingCamera = {{{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
        // ... unless the largest group of identical world draws (8 or more,
        // the same count in both frames) moves as one: that motion is the
        // camera's (it falls with Link among the clocks, up to 60 units a
        // game frame), taken as a translation.
        std::map<uint64_t, std::pair<std::vector<size_t>, std::vector<size_t>>> identical;
        for (size_t index = 0; index < current.Draws.size(); ++index)
            if (UsesModelViewLayout(current.Draws[index].VertexShader.Uniforms))
                identical[currentStrict[index]].first.push_back(index);
        for (size_t index = 0; index < previous.Draws.size(); ++index)
            if (UsesModelViewLayout(previous.Draws[index].VertexShader.Uniforms))
                if (const auto found = identical.find(previousStrict[index]); found != identical.end())
                    found->second.second.push_back(index);
        const std::pair<std::vector<size_t>, std::vector<size_t>>* largest = nullptr;
        for (const auto& [identity, group] : identical)
            if (group.first.size() >= 8U && group.first.size() == group.second.size() &&
                (largest == nullptr || group.first.size() > largest->first.size()))
                largest = &group;
        static const bool groupTrace = std::getenv("TRIAEVUM_JUMP_TRACE") != nullptr;
        if (groupTrace) {
            size_t best = 0, bestPrevious = 0;
            for (const auto& [identity, group] : identical)
                if (group.first.size() > best) { best = group.first.size(); bestPrevious = group.second.size(); }
            std::fprintf(stderr, "TRIAEVUM_GROUPS identities=%zu largest=%zu/%zu\n", identical.size(), best, bestPrevious);
        }
        if (largest != nullptr && largest->first.size() <= 512U) {
            const size_t count = largest->first.size();
            const auto at = [](const Oot3dPicaVulkanDrawPlan& draw) {
                const auto& f = draw.VertexShader.Uniforms.Floats;
                return std::array<double, 3>{f[20][3], f[21][3], f[22][3]};
            };
            std::vector<std::array<double, 3>> here(count), before(count);
            for (size_t index = 0; index < count; ++index) {
                here[index] = at(current.Draws[largest->first[index]]);
                before[index] = at(previous.Draws[largest->second[index]]);
            }
            // previous = R * current + t, refined over two pairings.
            Affine34 estimate{{{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
            const auto apply = [&](const std::array<double, 3>& point) {
                std::array<double, 3> out{};
                for (size_t row = 0; row < 3U; ++row)
                    out[row] = estimate[row][0] * point[0] + estimate[row][1] * point[1] +
                               estimate[row][2] * point[2] + estimate[row][3];
                return out;
            };
            std::vector<size_t> assignment;
            size_t coherent = 0;
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<std::vector<double>> cost(count, std::vector<double>(count, 0.0));
                for (size_t row = 0; row < count; ++row) {
                    const auto moved = apply(here[row]);
                    for (size_t column = 0; column < count; ++column) {
                        double squared = 0.0;
                        for (size_t axis = 0; axis < 3U; ++axis)
                            squared += (before[column][axis] - moved[axis]) * (before[column][axis] - moved[axis]);
                        cost[row][column] = std::isfinite(squared) ? std::sqrt(squared) : 1.0e12;
                    }
                }
                assignment = MinimalCostAssignment(cost);
                // Rigid fit of the pairs (Horn's quaternion method).
                std::array<double, 3> centerHere{}, centerBefore{};
                for (size_t row = 0; row < count; ++row)
                    for (size_t axis = 0; axis < 3U; ++axis) {
                        centerHere[axis] += here[row][axis] / static_cast<double>(count);
                        centerBefore[axis] += before[assignment[row]][axis] / static_cast<double>(count);
                    }
                double m[3][3] = {};
                for (size_t row = 0; row < count; ++row)
                    for (size_t i = 0; i < 3U; ++i)
                        for (size_t j = 0; j < 3U; ++j)
                            m[i][j] += (here[row][i] - centerHere[i]) * (before[assignment[row]][j] - centerBefore[j]);
                const double n[4][4] = {
                    {m[0][0] + m[1][1] + m[2][2], m[1][2] - m[2][1], m[2][0] - m[0][2], m[0][1] - m[1][0]},
                    {m[1][2] - m[2][1], m[0][0] - m[1][1] - m[2][2], m[0][1] + m[1][0], m[2][0] + m[0][2]},
                    {m[2][0] - m[0][2], m[0][1] + m[1][0], -m[0][0] + m[1][1] - m[2][2], m[1][2] + m[2][1]},
                    {m[0][1] - m[1][0], m[2][0] + m[0][2], m[1][2] + m[2][1], -m[0][0] - m[1][1] + m[2][2]}};
                double shift = 0.0;
                for (const auto& line : n)
                    for (const double value : line) shift += std::abs(value);
                std::array<double, 4> q{1.0, 0.0, 0.0, 0.0};
                for (int iteration = 0; iteration < 100; ++iteration) {
                    std::array<double, 4> next{};
                    for (size_t i = 0; i < 4U; ++i) {
                        next[i] = shift * q[i];
                        for (size_t j = 0; j < 4U; ++j) next[i] += n[i][j] * q[j];
                    }
                    const double length = std::sqrt(next[0] * next[0] + next[1] * next[1] + next[2] * next[2] + next[3] * next[3]);
                    if (!(length > 0.0)) break;
                    for (size_t i = 0; i < 4U; ++i) q[i] = next[i] / length;
                }
                const double w = q[0], x = q[1], y = q[2], z = q[3];
                const double r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                                        {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                                        {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
                for (size_t row = 0; row < 3U; ++row) {
                    double translation = centerBefore[row];
                    for (size_t column = 0; column < 3U; ++column) {
                        estimate[row][column] = static_cast<float>(r[row][column]);
                        translation -= r[row][column] * centerHere[column];
                    }
                    estimate[row][3] = static_cast<float>(translation);
                }
                coherent = 0;
                for (size_t row = 0; row < count; ++row) {
                    const auto moved = apply(here[row]);
                    double squared = 0.0;
                    for (size_t axis = 0; axis < 3U; ++axis)
                        squared += (before[assignment[row]][axis] - moved[axis]) * (before[assignment[row]][axis] - moved[axis]);
                    coherent += squared < 9.0 ? 1U : 0U;
                }
            }
            static const bool estimateTrace = std::getenv("TRIAEVUM_JUMP_TRACE") != nullptr;
            if (estimateTrace)
                std::fprintf(stderr, "TRIAEVUM_GROUP_CAMERA count=%zu coherent=%zu move=%.1f,%.1f,%.1f\n", count,
                             coherent, estimate[0][3], estimate[1][3], estimate[2][3]);
            if (coherent * 10U >= count * 8U) {
                pairingCamera = estimate;
            }
        }
    }
    if (!orderPairs && (matching.CameraDeltaValid || assumedStillCamera)) {
        if (const auto inverseCamera = InvertAffine(pairingCamera)) {
            // Groups: pairing kind and that pass's identity. Draws of the
            // identity left unpaired join their group (one appearing or going
            // shifted the order of the rest).
            struct Group {
                std::vector<size_t> Current, Previous;
            };
            std::map<std::pair<int, uint64_t>, Group> groups;
            const auto currentKey = [&](VisualDrawMatchKind kind, size_t index) {
                return kind == VisualDrawMatchKind::StrictOrdinal       ? currentStrict[index]
                       : kind == VisualDrawMatchKind::StructuralOrdinal ? currentStructural[index]
                       : kind == VisualDrawMatchKind::GeometryOrdinal   ? currentGeometry[index]
                                                                        : currentPipeline[index];
            };
            const auto previousKey = [&](VisualDrawMatchKind kind, size_t index) {
                return kind == VisualDrawMatchKind::StrictOrdinal       ? previousStrict[index]
                       : kind == VisualDrawMatchKind::StructuralOrdinal ? previousStructural[index]
                       : kind == VisualDrawMatchKind::GeometryOrdinal   ? previousGeometry[index]
                                                                        : previousPipeline[index];
            };
            for (size_t currentIndex = 0; currentIndex < matching.PreviousByCurrent.size(); ++currentIndex) {
                const auto& match = matching.PreviousByCurrent[currentIndex];
                if (!match.has_value() || match->Kind == VisualDrawMatchKind::StrictUnique ||
                    match->Kind == VisualDrawMatchKind::ModelInstance)
                    continue;
                if (!UsesModelViewLayout(current.Draws[currentIndex].VertexShader.Uniforms) ||
                    !UsesModelViewLayout(previous.Draws[match->PreviousIndex].VertexShader.Uniforms))
                    continue;
                auto& group = groups[{static_cast<int>(match->Kind), currentKey(match->Kind, currentIndex)}];
                group.Current.push_back(currentIndex);
                group.Previous.push_back(match->PreviousIndex);
            }
            std::vector<bool> currentJoined(current.Draws.size(), false), previousJoined(previous.Draws.size(), false);
            for (auto& [key, group] : groups) {
                const auto kind = static_cast<VisualDrawMatchKind>(key.first);
                for (size_t index = 0; index < current.Draws.size(); ++index)
                    if (!currentMatched[index] && !currentJoined[index] &&
                        UsesModelViewLayout(current.Draws[index].VertexShader.Uniforms) &&
                        currentKey(kind, index) == key.second) {
                        group.Current.push_back(index);
                        currentJoined[index] = true;
                    }
                for (size_t index = 0; index < previous.Draws.size(); ++index)
                    if (!previousMatched[index] && !previousJoined[index] &&
                        UsesModelViewLayout(previous.Draws[index].VertexShader.Uniforms) &&
                        previousKey(kind, index) == key.second) {
                        group.Previous.push_back(index);
                        previousJoined[index] = true;
                    }
            }
            const auto position = [](const Oot3dPicaVertexUniformState& uniforms) {
                return std::array<float, 3>{uniforms.Floats[20][3], uniforms.Floats[21][3], uniforms.Floats[22][3]};
            };
            // A draw farther than this from every previous one of its group is
            // new (and one that far from every current one went away). When the
            // group's size changed, something appeared or went (a foot shadow
            // as the foot lifts): the one left is often the other one, so only
            // a small move keeps the pairing; a draw shown without blending for
            // a game frame is not seen, one sliding across is.
            constexpr double kIncompatible = 1.0e12;
            for (const auto& [key, group] : groups) {
                if (group.Current.size() + group.Previous.size() < 3U) continue;
                // Transparent ones (foot shadows, effects) move at most ~12 units
                // in a game frame; a shadow 15-20 away is the other foot's.
                const bool transparent = current.Draws[group.Current.front()].Composition.Layer ==
                                         Oot3dPicaCompositionLayer::TransparentWorld;
                const double kUnpairedCost = group.Current.size() != group.Previous.size() ? 8.0
                                             : transparent                                  ? 13.0
                                                                                            : 30.0;
                const auto kind = static_cast<VisualDrawMatchKind>(key.first);
                // Square matrix: real columns, then one "unpaired" column per row.
                const size_t rows = group.Current.size(), columns = group.Previous.size();
                std::vector<std::vector<double>> cost(rows, std::vector<double>(columns + rows, kUnpairedCost));
                for (size_t row = 0; row < rows; ++row) {
                    const auto& currentDraw = current.Draws[group.Current[row]];
                    const auto here = position(currentDraw.VertexShader.Uniforms);
                    for (size_t column = 0; column < columns; ++column) {
                        const auto& previousDraw = previous.Draws[group.Previous[column]];
                        if (!HaveMatchingStructuralLayout(previousDraw, currentDraw,
                                                          kind == VisualDrawMatchKind::GeometryOrdinal)) {
                            cost[row][column] = kIncompatible;
                            continue;
                        }
                        // The previous position seen from the current view (D^-1 * p).
                        const auto before = MultiplyAffine(*inverseCamera, AffineAt(previousDraw.VertexShader.Uniforms, 20U));
                        double squared = 0.0;
                        for (size_t axis = 0; axis < 3U; ++axis) {
                            const double delta = static_cast<double>(before[axis][3]) - here[axis];
                            squared += delta * delta;
                        }
                        cost[row][column] = std::isfinite(squared) ? std::sqrt(squared) : kIncompatible;
                    }
                }
                const auto assignment = MinimalCostAssignment(cost);
                for (const size_t index : group.Current) {
                    if (const auto& match = matching.PreviousByCurrent[index]) {
                        previousMatched[match->PreviousIndex] = false;
                    }
                }
                for (size_t row = 0; row < rows; ++row) {
                    const size_t currentIndex = group.Current[row];
                    const auto old = matching.PreviousByCurrent[currentIndex];
                    std::optional<VisualDrawMatch> chosen;
                    if (assignment[row] < columns && cost[row][assignment[row]] < kUnpairedCost)
                        chosen = VisualDrawMatch{group.Previous[assignment[row]], kind};
                    if (!old.has_value() && !chosen.has_value()) continue;
                    if (!old.has_value() || !chosen.has_value() || old->PreviousIndex != chosen->PreviousIndex)
                        ++matching.ReorderedPairs;
                    matching.PreviousByCurrent[currentIndex] = chosen;
                    currentMatched[currentIndex] = chosen.has_value();
                    keepUnpaired[currentIndex] = !chosen.has_value();
                    if (chosen.has_value()) previousMatched[chosen->PreviousIndex] = true;
                }
            }
        }
    }
    static const bool jumpGuard = std::getenv("TRIAEVUM_NO_JUMP_GUARD") == nullptr;
    if (jumpGuard && (matching.CameraDeltaValid || assumedStillCamera)) {
        // World motion of every model/bone matrix with the camera change removed.
        // Only the first model matrix: later bone registers keep leftovers from
        // earlier draws when a model doesn't use them (PICA uniforms persist),
        // and those never follow the camera, so checking them rejected nearly
        // every pairing while the camera turned (the global stutter).
        const auto worldJump = [&](const Oot3dPicaVertexUniformState& before,
                                   const Oot3dPicaVertexUniformState& after) {
            float worst = 0.0F;
            for (size_t first = 20U; first < 21U; first += 3U) {
                const auto predicted = MultiplyAffine(pairingCamera, AffineAt(after, first));
                float squared = 0.0F;
                for (size_t row = 0; row < 3U; ++row) {
                    const float delta = before.Floats[first + row][3] - predicted[row][3];
                    squared += delta * delta;
                }
                if (std::isfinite(squared)) worst = std::max(worst, squared);
            }
            return std::sqrt(worst);
        };
        // A unique pairing is trusted up to 100 units; one made by draw order
        // (identical NPCs such as the carpenters, repeated props) up to 30:
        // swapping two of them blended an NPC halfway to its neighbour.
        constexpr float kMaximumUniqueJump = 100.0F;
        constexpr float kMaximumOrdinalJump = 30.0F;
        std::vector<size_t> rejected;
        for (size_t currentIndex = 0; currentIndex < matching.PreviousByCurrent.size(); ++currentIndex) {
            auto& match = matching.PreviousByCurrent[currentIndex];
            if (!match.has_value()) continue;
            const auto& before = previous.Draws[match->PreviousIndex].VertexShader.Uniforms;
            const auto& after = current.Draws[currentIndex].VertexShader.Uniforms;
            if (!UsesModelViewLayout(before) || !UsesModelViewLayout(after)) continue;
            // (A unique pair is trusted when the camera is unknown: it may
            // have moved.)
            const bool exact = match->Kind == VisualDrawMatchKind::StrictUnique ||
                               match->Kind == VisualDrawMatchKind::ModelInstance;
            if (assumedStillCamera && exact) continue;
            const float limit = exact ? kMaximumUniqueJump : kMaximumOrdinalJump;
            if (worldJump(before, after) > limit) {
                previousMatched[match->PreviousIndex] = false;
                currentMatched[currentIndex] = false;
                match.reset();
                rejected.push_back(currentIndex);
                ++matching.RejectedJumps;
            }
        }
        // Re-pair each unpaired world draw (rejected, or never paired because
        // the draw set changed) with the nearest compatible free previous draw:
        // for static scenery such as doors that is itself.
        std::vector<size_t> unpairedWorld;
        for (size_t currentIndex = 0; currentIndex < matching.PreviousByCurrent.size(); ++currentIndex)
            if (!matching.PreviousByCurrent[currentIndex].has_value() && !keepUnpaired[currentIndex] &&
                UsesModelViewLayout(current.Draws[currentIndex].VertexShader.Uniforms))
                unpairedWorld.push_back(currentIndex);
        for (const size_t currentIndex : unpairedWorld) {
            const auto& currentDraw = current.Draws[currentIndex];
            float best = kMaximumOrdinalJump;
            std::optional<size_t> bestPrevious;
            for (size_t previousIndex = 0; previousIndex < previous.Draws.size(); ++previousIndex) {
                if (previousMatched[previousIndex]) continue;
                const auto& previousDraw = previous.Draws[previousIndex];
                if (!UsesModelViewLayout(previousDraw.VertexShader.Uniforms) ||
                    !HaveMatchingStructuralLayout(previousDraw, currentDraw))
                    continue;
                const float jump = worldJump(previousDraw.VertexShader.Uniforms, currentDraw.VertexShader.Uniforms);
                if (jump < best) { best = jump; bestPrevious = previousIndex; }
            }
            if (bestPrevious.has_value()) {
                previousMatched[*bestPrevious] = true;
                currentMatched[currentIndex] = true;
                matching.PreviousByCurrent[currentIndex] =
                    VisualDrawMatch{*bestPrevious, VisualDrawMatchKind::StructuralOrdinal};
                ++matching.RepairedPairs;
            }
        }
    }
    // TRIAEVUM_PAIR_DEBUG=<file>: every transparent world draw with its
    // pairing (kind, textures, vertex buffers, model position) per frame.
    static FILE* pairDebug = [] {
        const char* path = std::getenv("TRIAEVUM_PAIR_DEBUG");
        return path != nullptr ? std::fopen(path, "w") : nullptr;
    }();
    if (pairDebug != nullptr) {
        static uint64_t frame = 0;
        ++frame;
        const auto describe = [&](const Oot3dPicaVulkanDrawPlan& draw, char* out, size_t size) {
            const auto& f = draw.VertexShader.Uniforms.Floats;
            const uint32_t texture = draw.Textures.empty() ? 0U : draw.Textures.front().State.PhysicalAddress;
            std::snprintf(out, size, "tex=%08x vb=%08x n=%u m=%.1f,%.1f,%.1f bl=%d dw=%d",
                          texture, draw.State.VertexInput.PhysicalBaseAddress, draw.VertexCount,
                          f[20][3], f[21][3], f[22][3], draw.State.OutputMerger.Blend.Enabled ? 1 : 0,
                          draw.State.OutputMerger.Depth.WriteEnabled ? 1 : 0);
        };
        for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
            const auto& draw = current.Draws[currentIndex];
            if (draw.Composition.Layer != Oot3dPicaCompositionLayer::TransparentWorld) continue;
            char now[192], before[192] = "-";
            describe(draw, now, sizeof(now));
            const auto& match = matching.PreviousByCurrent[currentIndex];
            if (match.has_value()) describe(previous.Draws[match->PreviousIndex], before, sizeof(before));
            std::fprintf(pairDebug, "F%llu cur=%zu kind=%d mv=%d | %s | prev %s\n",
                         static_cast<unsigned long long>(frame), currentIndex,
                         match.has_value() ? static_cast<int>(match->Kind) : -1,
                         UsesModelViewLayout(draw.VertexShader.Uniforms) ? 1 : 0, now, before);
        }
    }
    // TRIAEVUM_MATCH_AUDIT=<file>: the final pairing of every frame (camera
    // change D, and per draw its identity, pairing and model position) for
    // offline analysis (work/interp/audit.py).
    static FILE* matchAudit = [] {
        const char* path = std::getenv("TRIAEVUM_MATCH_AUDIT");
        return path != nullptr ? std::fopen(path, "w") : nullptr;
    }();
    if (matchAudit != nullptr) {
        static uint64_t auditFrame = 0;
        ++auditFrame;
        const auto& d = matching.CameraDelta;
        std::fprintf(matchAudit, "S %llu %llu\n", static_cast<unsigned long long>(previous.Sequence),
                     static_cast<unsigned long long>(current.Sequence));
        std::fprintf(matchAudit, "F %llu %zu %zu %d %d", static_cast<unsigned long long>(auditFrame),
                     previous.Draws.size(), current.Draws.size(), matching.CameraDeltaValid ? 1 : 0,
                     matching.CameraCut ? 1 : 0);
        for (size_t row = 0; row < 3U; ++row)
            for (size_t column = 0; column < 4U; ++column) std::fprintf(matchAudit, " %.5g", d[row][column]);
        std::fprintf(matchAudit, "\n");
        for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
            const auto& draw = current.Draws[currentIndex];
            const auto& f = draw.VertexShader.Uniforms.Floats;
            const auto& match = matching.PreviousByCurrent[currentIndex];
            const uint32_t texture = draw.Textures.empty() ? 0U : draw.Textures.front().State.PhysicalAddress;
            uint64_t vertexKey = kFnvOffset, fragmentKey = kFnvOffset, indexKey = kFnvOffset;
            HashValue(vertexKey, draw.VertexShader.StateKey);
            HashValue(fragmentKey, draw.FragmentShader.StateKey);
            for (const uint8_t byte : draw.ResolvedIndexBytes()) HashValue(indexKey, byte);
            std::fprintf(matchAudit, "X %zu %016llx %016llx %016llx %u %u %u %u %u %u %u %u\n", currentIndex,
                         static_cast<unsigned long long>(vertexKey), static_cast<unsigned long long>(fragmentKey),
                         static_cast<unsigned long long>(indexKey), draw.State.Framebuffer.Width,
                         draw.State.Framebuffer.Height, static_cast<unsigned>(draw.State.Framebuffer.ColorFormat),
                         draw.State.Framebuffer.Flipped ? 1U : 0U, draw.BaseVertex,
                         static_cast<unsigned>(draw.Composition.Provenance), draw.Composition.SourcePc,
                         draw.State.VertexInput.IndexPhysicalAddress);
            std::fprintf(matchAudit, "D %zu %d %d %d %016llx %016llx %08x %08x %u %d %.3f %.3f %.3f %08x %u\n", currentIndex,
                         match.has_value() ? static_cast<int>(match->PreviousIndex) : -1,
                         match.has_value() ? static_cast<int>(match->Kind) : -1,
                         UsesModelViewLayout(draw.VertexShader.Uniforms) ? 1 : 0,
                         static_cast<unsigned long long>(currentStrict[currentIndex]),
                         static_cast<unsigned long long>(currentStructural[currentIndex]), texture,
                         draw.State.VertexInput.PhysicalBaseAddress, draw.VertexCount,
                         static_cast<int>(draw.Composition.Layer), f[20][3], f[21][3], f[22][3],
                         draw.Composition.NativeValue, draw.Composition.Ordinal);
        }
    }
    if (matchAudit != nullptr) {
        // Paired 2D (orthographic) draws whose data changed: which vertex
        // uniform rows, and whether vertex bytes differ.
        for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
            const auto& match = matching.PreviousByCurrent[currentIndex];
            const auto& b = current.Draws[currentIndex];
            if (!match || UsesModelViewLayout(b.VertexShader.Uniforms)) continue;
            const auto& a = previous.Draws[match->PreviousIndex];
            std::string rows;
            for (size_t row = 0; row < 0x60U; ++row)
                if (a.VertexShader.Uniforms.Floats[row] != b.VertexShader.Uniforms.Floats[row]) {
                    rows += std::to_string(row);
                    if (rows.size() < 160U || row % 4U == 3U) {
                        char values[96];
                        const auto& x = a.VertexShader.Uniforms.Floats[row];
                        const auto& y = b.VertexShader.Uniforms.Floats[row];
                        std::snprintf(values, sizeof(values), "[%.3g %.3g %.3g %.3g>%.3g %.3g %.3g %.3g]", x[0], x[1], x[2],
                                      x[3], y[0], y[1], y[2], y[3]);
                        rows += values;
                    }
                    rows += ",";
                }
            bool vertexBytes = false;
            for (const auto& binding : b.VertexBindings)
                if (const auto* other = FindVertexBinding(a, binding.Binding))
                    vertexBytes = vertexBytes || !EqualBindingBytes(*other, binding);
            const bool fragment = ContinuousFragmentUniformValuesDiffer(a, b);
            if (rows.empty() && !vertexBytes && !fragment && std::getenv("TRIAEVUM_AUDIT_ALL_UI") == nullptr) continue;
            const auto& f3 = b.VertexShader.Uniforms.Floats[3];
            std::fprintf(matchAudit, "U %zu %08x %u k=%d ortho=%d vb=%d frag=%d f3=%.2f,%.2f,%.2f,%.2f rows=%s\n",
                         currentIndex, b.Textures.empty() ? 0U : b.Textures.front().State.PhysicalAddress,
                         b.VertexCount, static_cast<int>(match->Kind),
                         UsesOrthographicProjection(b.VertexShader.Uniforms) ? 1 : 0, vertexBytes ? 1 : 0,
                         fragment ? 1 : 0, f3[0], f3[1], f3[2], f3[3], rows.empty() ? "-" : rows.c_str());
        }
        // Unpaired current world draws with an unpaired previous twin (same strict identity).
        std::vector<bool> previousUsed(previous.Draws.size(), false);
        for (const auto& match : matching.PreviousByCurrent)
            if (match) previousUsed[match->PreviousIndex] = true;
        for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
            if (matching.PreviousByCurrent[currentIndex] ||
                !UsesModelViewLayout(current.Draws[currentIndex].VertexShader.Uniforms))
                continue;
            for (size_t previousIndex = 0; previousIndex < previous.Draws.size(); ++previousIndex) {
                if (previousUsed[previousIndex] || previousStrict[previousIndex] != currentStrict[currentIndex]) continue;
                const auto& a = previous.Draws[previousIndex];
                const auto& b = current.Draws[currentIndex];
                std::fprintf(matchAudit, "W %zu %zu structural=%d composition=%d/%d layer=%d/%d dom=%d/%d matchedP=%d matchedC=%d keep=%d cut=%d\n",
                             currentIndex, previousIndex, HaveMatchingStructuralLayout(a, b) ? 1 : 0,
                             CompatibleComposition(a, b) ? 1 : 0, a.Composition == b.Composition ? 1 : 0,
                             static_cast<int>(a.Composition.Layer), static_cast<int>(b.Composition.Layer),
                             static_cast<int>(a.CompositionDomain), static_cast<int>(b.CompositionDomain),
                             previousMatched[previousIndex] ? 1 : 0, currentMatched[currentIndex] ? 1 : 0,
                             keepUnpaired[currentIndex] ? 1 : 0, matching.CameraCut ? 1 : 0);
                break;
            }
        }
    }
    matching.UnmatchedPrevious = static_cast<size_t>(std::count(
        previousMatched.begin(), previousMatched.end(), false));
    matching.UnmatchedCurrent = static_cast<size_t>(std::count(
        currentMatched.begin(), currentMatched.end(), false));
    if (FILE* trace = FlingTraceFile()) {
        float angle = 0.0F;
        if (matching.CameraDeltaValid) {
            const auto& d = matching.CameraDelta;
            angle = std::acos(std::clamp((d[0][0] + d[1][1] + d[2][2] - 1.0F) * 0.5F, -1.0F, 1.0F)) * 57.2958F;
        }
        size_t unmatched = 0;
        for (const auto& m : matching.PreviousByCurrent) unmatched += m.has_value() ? 0U : 1U;
        std::fprintf(trace, "FRAME t=%.1f draws=%zu/%zu camera=%d angle=%.2f rejected=%zu unmatched=%zu votes=%zu/%zu\n",
                     FlingTraceMs(), previous.Draws.size(), current.Draws.size(), matching.CameraDeltaValid ? 1 : 0,
                     angle, matching.RejectedJumps, unmatched, matching.CameraVotes, matching.CameraCandidates);
    }
    if (FILE* trace = FlingTraceFile(); trace != nullptr && matching.CameraDeltaValid) {
        // Unpaired world draws with their position mapped to the previous view
        // (static scenery should repeat the same numbers frame after frame).
        for (size_t currentIndex = 0; currentIndex < matching.PreviousByCurrent.size(); ++currentIndex) {
            if (matching.PreviousByCurrent[currentIndex].has_value()) continue;
            const auto& uniforms = current.Draws[currentIndex].VertexShader.Uniforms;
            if (!UsesModelViewLayout(uniforms)) continue;
            std::fprintf(trace, "UNPAIRED t=%.1f index=%zu identity=%016llx view=%.1f,%.1f,%.1f\n", FlingTraceMs(),
                         currentIndex, static_cast<unsigned long long>(
                             current.StrictDrawIdentities.size() > currentIndex ? current.StrictDrawIdentities[currentIndex] : 0ULL),
                         uniforms.Floats[20][3], uniforms.Floats[21][3], uniforms.Floats[22][3]);
        }
    }
    static const bool jumpTrace = std::getenv("TRIAEVUM_JUMP_TRACE") != nullptr;
    if (jumpTrace) {
        static size_t calls = 0, valid = 0;
        ++calls; valid += matching.CameraDeltaValid ? 1U : 0U;
        if (calls % 100U == 0U)
            std::fprintf(stderr, "TRIAEVUM_CAMERA_DELTA valid=%zu/%zu\n", valid, calls);
    }
    if (jumpTrace && matching.RejectedJumps != 0)
        std::fprintf(stderr, "TRIAEVUM_JUMP_REJECTED draws=%zu repaired=%zu of=%zu\n", matching.RejectedJumps,
                     matching.RepairedPairs, current.Draws.size());
    // TRIAEVUM_MODEL_PAIR_AUDIT=<file>: the draws whose identity pairing
    // differs from the pairing without identities, with both jumps.
    static FILE* modelAudit = [] {
        const char* path = std::getenv("TRIAEVUM_MODEL_PAIR_AUDIT");
        return path != nullptr ? std::fopen(path, "w") : nullptr;
    }();
    if (modelAudit != nullptr && !gModelPairsSuspended) {
        gModelPairsSuspended = true;
        const auto without = BuildVisualDrawMatching(previous, current);
        gModelPairsSuspended = false;
        const auto position = [](const Oot3dPicaVulkanDrawPlan& draw) {
            const auto& f = draw.VertexShader.Uniforms.Floats;
            return std::array<float, 3>{f[20][3], f[21][3], f[22][3]};
        };
        const auto jump = [&](size_t previousIndex, size_t currentIndex) {
            const auto a = position(previous.Draws[previousIndex]);
            const auto b = position(current.Draws[currentIndex]);
            const auto& d = matching.CameraDelta;
            float squared = 0.0F;
            for (size_t row = 0; row < 3U; ++row) {
                const float moved = matching.CameraDeltaValid
                                        ? d[row][0] * b[0] + d[row][1] * b[1] + d[row][2] * b[2] + d[row][3]
                                        : b[row];
                squared += (moved - a[row]) * (moved - a[row]);
            }
            return std::sqrt(squared);
        };
        size_t model = 0, differ = 0;
        for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
            const auto& with = matching.PreviousByCurrent[currentIndex];
            if (!with.has_value() || with->Kind != VisualDrawMatchKind::ModelInstance) continue;
            ++model;
            const auto& other = without.PreviousByCurrent[currentIndex];
            if (other.has_value() && other->PreviousIndex == with->PreviousIndex) continue;
            ++differ;
            const bool world = UsesModelViewLayout(current.Draws[currentIndex].VertexShader.Uniforms);
            std::fprintf(modelAudit, "M %zu world=%d verts=%u model=%08x ord=%u id_jump=%.2f other=%d other_jump=%.2f other_kind=%d\n",
                         currentIndex, world ? 1 : 0, current.Draws[currentIndex].VertexCount,
                         current.Draws[currentIndex].Composition.NativeValue,
                         current.Draws[currentIndex].Composition.Ordinal,
                         world ? jump(with->PreviousIndex, currentIndex) : -1.0F,
                         other.has_value() ? static_cast<int>(other->PreviousIndex) : -1,
                         other.has_value() && world ? jump(other->PreviousIndex, currentIndex) : -1.0F,
                         other.has_value() ? static_cast<int>(other->Kind) : -1);
        }
        std::fprintf(modelAudit, "F model=%zu differ=%zu draws=%zu camera=%d\n", model, differ,
                     current.Draws.size(), matching.CameraDeltaValid ? 1 : 0);
    }
    ComputeModelMotions(previous, current, matching);
    {
        // TRIAEVUM_POSE_TIMING: average cost of the skeleton preparation.
        static const bool timing = std::getenv("TRIAEVUM_POSE_TIMING") != nullptr;
        const auto start = std::chrono::steady_clock::now();
        ComputePoseTransitions(previous, current, matching);
        if (timing) {
            static double total = 0.0;
            static uint64_t calls = 0;
            total += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            if (++calls % 300U == 0U)
                std::fprintf(stderr, "TRIAEVUM_POSE_TIMING prepare=%.1fus poses=%zu\n", total / static_cast<double>(calls),
                             matching.Poses.size());
        }
    }
    return matching;
}

Oot3dPicaVisualTransitionStats
AnalyzeVisualTransitionWithMatching(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                    const VisualDrawMatching& matching,
                                    std::vector<VisualDrawDeltaFlags>* drawDeltas = nullptr) {
    Oot3dPicaVisualTransitionStats stats;
    stats.PreviousDraws = previous.Draws.size();
    stats.CurrentDraws = current.Draws.size();
    stats.StrictUniqueMatchedDraws = matching.StrictUniqueMatches;
    stats.StrictOrdinalMatchedDraws = matching.StrictOrdinalMatches;
    stats.StructuralOrdinalMatchedDraws = matching.StructuralOrdinalMatches;
    stats.PipelineOrdinalMatchedDraws = matching.PipelineOrdinalMatches;
    // The final pairing (later passes re-pair and drop pairs).
    stats.MatchedDraws = static_cast<size_t>(std::count_if(
        matching.PreviousByCurrent.begin(), matching.PreviousByCurrent.end(),
        [](const auto& match) { return match.has_value(); }));
    stats.AmbiguousPreviousDraws = matching.AmbiguousPrevious;
    stats.AmbiguousCurrentDraws = matching.AmbiguousCurrent;
    stats.UnmatchedPreviousDraws = matching.UnmatchedPrevious;
    stats.UnmatchedCurrentDraws = matching.UnmatchedCurrent;
    const size_t maximumDrawCount = std::max(stats.PreviousDraws, stats.CurrentDraws);
    if (maximumDrawCount != 0U) {
        stats.MatchedDrawCoverage = static_cast<double>(stats.MatchedDraws) / static_cast<double>(maximumDrawCount);
    }
    std::vector<double> topTargetDeltas;
    if (drawDeltas != nullptr) {
        drawDeltas->assign(current.Draws.size(), {});
    }
    for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
        if (!matching.PreviousByCurrent[currentIndex].has_value()) {
            continue;
        }
        const auto& previousDraw = previous.Draws[matching.PreviousByCurrent[currentIndex]->PreviousIndex];
        const auto& currentDraw = current.Draws[currentIndex];
        VisualDrawDeltaFlags delta;
        delta.PerInstanceVertex =
            InterpolatableVertexValuesDiffer(previousDraw, currentDraw, Oot3dPicaVertexInputRate::PerInstance);
        delta.PerVertex =
            previousDraw.BaseVertex == currentDraw.BaseVertex &&
            EqualIndexBytes(previousDraw, currentDraw) &&
            InterpolatableVertexValuesDiffer(
                previousDraw, currentDraw, Oot3dPicaVertexInputRate::PerVertex);
        static const bool vertexBlendTrace = std::getenv("TRIAEVUM_VERTEX_BLEND_TRACE") != nullptr;
        if (vertexBlendTrace && (delta.PerVertex || delta.PerInstanceVertex)) {
            const auto& state = currentDraw.State;
            const auto& texture = state.Textures[0];
            std::fprintf(stderr,
                         "VBLEND dom=%d layer=%d verts=%u depthW=%d blend=%d src=%d dst=%d tex=%08X %ux%u fmt=%u "
                         "fb=%08X prevIdx=%zu\n",
                         static_cast<int>(currentDraw.CompositionDomain),
                         static_cast<int>(currentDraw.Composition.Layer), currentDraw.VertexCount,
                         state.OutputMerger.Depth.WriteEnabled ? 1 : 0, state.OutputMerger.Blend.Enabled ? 1 : 0,
                         static_cast<int>(state.OutputMerger.Blend.SourceColor),
                         static_cast<int>(state.OutputMerger.Blend.DestinationColor),
                         texture.Enabled ? texture.PhysicalAddress : 0U, texture.Width, texture.Height,
                         static_cast<unsigned>(texture.Format), state.Framebuffer.ColorPhysicalAddress,
                         matching.PreviousByCurrent[currentIndex]->PreviousIndex);
        }
        delta.VertexUniforms =
            ContinuousVertexUniformValuesDiffer(previousDraw, currentDraw);
        delta.FragmentUniforms =
            matching.PreviousByCurrent[currentIndex]->Kind != VisualDrawMatchKind::GeometryOrdinal &&
            ContinuousFragmentUniformValuesDiffer(previousDraw, currentDraw);
        delta.Viewport =
            ContinuousViewportValuesDiffer(previousDraw, currentDraw);
        delta.BlendConstantColor =
            previousDraw.State.OutputMerger.Blend.ConstantColor !=
            currentDraw.State.OutputMerger.Blend.ConstantColor;
        delta.Continuous = delta.PerInstanceVertex || delta.PerVertex ||
                           delta.VertexUniforms || delta.FragmentUniforms ||
                           delta.Viewport || delta.BlendConstantColor;
        if (delta.Continuous) {
            ++stats.ChangedContinuousDraws;
        }
        if (delta.PerInstanceVertex) {
            ++stats.ChangedPerInstanceVertexDraws;
        }
        if (delta.PerVertex) {
            ++stats.ChangedPerVertexDraws;
        }
        if (drawDeltas != nullptr) {
            (*drawDeltas)[currentIndex] = delta;
        }
        if (currentDraw.State.Framebuffer.ColorPhysicalAddress == current.TopTransfer.InputPhysicalAddress) {
            ++stats.TopTargetMatchedDraws;
            topTargetDeltas.push_back(NormalizedVertexUniformDelta(previousDraw, currentDraw));
            const auto& currentProgram = currentDraw.VertexShader.TemporalProgram;
            const auto& previousProgram = previousDraw.VertexShader.TemporalProgram;
            const auto skinned = [](const auto& draw) {
                const auto& skeleton = draw.VertexShader.TemporalProgram->Hooks.Skeleton;
                return skeleton.Available() &&
                    (draw.VertexShader.Uniforms.BooleanMask & (1U << skeleton.EnableBooleanUniform)) != 0;
            };
            if (currentProgram && previousProgram && !skinned(currentDraw) && !skinned(previousDraw) &&
                currentDraw.Composition.Layer == Oot3dPicaCompositionLayer::OpaqueWorld &&
                previousDraw.Composition.Layer == Oot3dPicaCompositionLayer::OpaqueWorld &&
                currentProgram->Hooks.Transform == previousProgram->Hooks.Transform) {
                const auto camera = Fast::Renderer3ds::MeasurePicaRigidViewTemporalDelta(
                    currentProgram->Hooks.Transform, previousDraw.VertexShader.Uniforms.Floats,
                    currentDraw.VertexShader.Uniforms.Floats);
                if (camera) {
                    ++stats.CameraMatchedDraws;
                    if (camera->Discontinuous()) ++stats.CameraDiscontinuousDraws;
                }
            }
        }
    }
    // Model-view scenes (MM3D: the camera is inside f[20..], f[4..6] is
    // identity): the view-matrix measure above reads object and camera motion
    // together and called LT swings (20-50 degrees a frame) cuts, so those
    // frames were shown without in-betweens (a hitch). The matching's own
    // camera estimate decides there: a cut only past 60 degrees / 400 units.
    static const bool viewCuts = std::getenv("TRIAEVUM_VIEW_CAMERA_CUTS") != nullptr;
    if (!viewCuts && (matching.CameraDeltaValid || matching.CameraCut)) {
        stats.ModelViewCamera = true;
        if (matching.CameraCut) {
            stats.CameraMatchedDraws = std::max<size_t>(stats.CameraMatchedDraws, 1U);
            stats.CameraDiscontinuousDraws = stats.CameraMatchedDraws;
        } else {
            stats.CameraDiscontinuousDraws = 0U;
        }
    }
    if (!topTargetDeltas.empty()) {
        std::sort(topTargetDeltas.begin(), topTargetDeltas.end());
        stats.MedianNormalizedContinuousDelta = topTargetDeltas[topTargetDeltas.size() / 2U];
        const size_t p90Index = ((topTargetDeltas.size() - 1U) * 9U) / 10U;
        stats.P90NormalizedContinuousDelta = topTargetDeltas[p90Index];
    }
    static const bool trace = std::getenv("OOT3D_VISUAL_TRANSITION_DIAGNOSTICS") != nullptr;
    if (trace && stats.CameraDiscontinuousDraws != 0) {
        std::fprintf(stderr, "[visual-camera-cut] previous=%llu current=%llu camera_draws=%zu cut_draws=%zu coverage=%.4f rejected=%u\n",
            static_cast<unsigned long long>(previous.Sequence), static_cast<unsigned long long>(current.Sequence),
            stats.CameraMatchedDraws, stats.CameraDiscontinuousDraws, stats.MatchedDrawCoverage,
            stats.CameraDiscontinuousDraws * 2U >= stats.CameraMatchedDraws ? 1U : 0U);
    }
    return stats;
}


// The in-between camera: D (previous = D * current, view space) applied for a
// fraction w = 1 - alpha, as a real rotation around the point the camera
// orbits (blending matrices linearly cut across fast turns: the scene shrank
// and moved unevenly while LT swung the camera).
using Quaternion = std::array<float, 4>;  // x y z w

float Determinant3(const std::array<std::array<float, 4>, 3>& m) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

// M (3x3 of a model/bone matrix) = R * diag(scale): the columns' lengths are
// the scales, R their orthonormalized directions (Gram-Schmidt).
bool DecomposeRotationScale(const std::array<std::array<float, 4>, 3>& m, Quaternion& rotation,
                            std::array<float, 3>& scale) {
    std::array<std::array<float, 3>, 3> axis{};  // axis[c] = column c
    for (size_t c = 0; c < 3U; ++c) {
        for (size_t r = 0; r < 3U; ++r) axis[c][r] = m[r][c];
        scale[c] = std::sqrt(axis[c][0] * axis[c][0] + axis[c][1] * axis[c][1] + axis[c][2] * axis[c][2]);
        if (!(scale[c] > 1.0e-6F) || !std::isfinite(scale[c])) return false;
    }
    const auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const auto normalize = [](std::array<float, 3>& v) {
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!(length > 1.0e-6F)) return false;
        for (float& value : v) value /= length;
        return true;
    };
    if (!normalize(axis[0])) return false;
    const float d01 = dot(axis[1], axis[0]);
    for (size_t r = 0; r < 3U; ++r) axis[1][r] -= d01 * axis[0][r];
    if (!normalize(axis[1])) return false;
    axis[2] = {axis[0][1] * axis[1][2] - axis[0][2] * axis[1][1],
               axis[0][2] * axis[1][0] - axis[0][0] * axis[1][2],
               axis[0][0] * axis[1][1] - axis[0][1] * axis[1][0]};
    if (dot(axis[2], {m[0][2], m[1][2], m[2][2]}) < 0.0F) return false;  // mirrored
    // R[r][c] = axis[c][r]
    const float r00 = axis[0][0], r11 = axis[1][1], r22 = axis[2][2];
    const float trace = r00 + r11 + r22;
    if (trace > 0.0F) {
        const float t = std::sqrt(trace + 1.0F) * 2.0F;
        rotation = {(axis[1][2] - axis[2][1]) / t, (axis[2][0] - axis[0][2]) / t,
                    (axis[0][1] - axis[1][0]) / t, 0.25F * t};
    } else if (r00 > r11 && r00 > r22) {
        const float t = std::sqrt(1.0F + r00 - r11 - r22) * 2.0F;
        rotation = {0.25F * t, (axis[1][0] + axis[0][1]) / t, (axis[2][0] + axis[0][2]) / t,
                    (axis[1][2] - axis[2][1]) / t};
    } else if (r11 > r22) {
        const float t = std::sqrt(1.0F + r11 - r00 - r22) * 2.0F;
        rotation = {(axis[1][0] + axis[0][1]) / t, 0.25F * t, (axis[2][1] + axis[1][2]) / t,
                    (axis[2][0] - axis[0][2]) / t};
    } else {
        const float t = std::sqrt(1.0F + r22 - r00 - r11) * 2.0F;
        rotation = {(axis[2][0] + axis[0][2]) / t, (axis[2][1] + axis[1][2]) / t, 0.25F * t,
                    (axis[0][1] - axis[1][0]) / t};
    }
    return std::all_of(rotation.begin(), rotation.end(), [](float v) { return std::isfinite(v); });
}

// Linear blending of two rotations far apart shrinks the matrix (180 degrees:
// to nothing): a limb, or the whole model, flattened for a frame. Those
// model/bone matrices are blended as rotation (slerp) and scale instead.
void InterpolateRotatedMatrices(const Oot3dPicaVertexUniformState& previous,
                                const Oot3dPicaVertexUniformState& current, float alpha,
                                Oot3dPicaVertexUniformState& output) {
    static const bool disabled = std::getenv("TRIAEVUM_LINEAR_BONES") != nullptr;
    if (disabled) return;
    for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
        std::array<std::array<float, 4>, 3> a{}, b{}, blended{};
        for (size_t r = 0; r < 3U; ++r) {
            a[r] = previous.Floats[first + r];
            b[r] = current.Floats[first + r];
            blended[r] = output.Floats[first + r];
        }
        const float da = Determinant3(a), db = Determinant3(b), dm = Determinant3(blended);
        if (!std::isfinite(da) || !std::isfinite(db) || !std::isfinite(dm) || da == db) continue;
        const float smaller = std::min(std::abs(da), std::abs(db));
        // Within 2% of the endpoints' volume: a small rotation (< ~16 degrees),
        // keep linear. (10% let a roll's ~30 degrees a frame shrink Link ~3%.)
        if (!(smaller > 1.0e-9F) || (dm * da > 0.0F && dm * db > 0.0F && std::abs(dm) >= 0.98F * smaller))
            continue;
        static const bool trace = std::getenv("TRIAEVUM_SQUASH_TRACE") != nullptr;
        if (trace)
            std::fprintf(stderr, "TRIAEVUM_SQUASH row=%zu det=%.3g/%.3g blended=%.3g alpha=%.2f\n", first, da, db,
                         dm, alpha);
        Quaternion qa{}, qb{};
        std::array<float, 3> sa{}, sb{};
        if (!DecomposeRotationScale(a, qa, sa) || !DecomposeRotationScale(b, qb, sb)) {
            // Mirrored or degenerate: show the nearer frame's matrix.
            for (size_t r = 0; r < 3U; ++r)
                for (size_t c = 0; c < 3U; ++c) output.Floats[first + r][c] = alpha < 0.5F ? a[r][c] : b[r][c];
            continue;
        }
        float cosine = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
        if (cosine < 0.0F) {
            for (float& v : qb) v = -v;
            cosine = -cosine;
        }
        Quaternion q{};
        if (cosine > 0.9995F) {
            for (size_t i = 0; i < 4U; ++i) q[i] = qa[i] + (qb[i] - qa[i]) * alpha;
        } else {
            const float angle = std::acos(std::clamp(cosine, -1.0F, 1.0F));
            const float sine = std::sin(angle);
            const float wa = std::sin((1.0F - alpha) * angle) / sine, wb = std::sin(alpha * angle) / sine;
            for (size_t i = 0; i < 4U; ++i) q[i] = qa[i] * wa + qb[i] * wb;
        }
        const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (!(length > 1.0e-6F)) continue;
        for (float& v : q) v /= length;
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const std::array<std::array<float, 3>, 3> rotation{{
            {1.0F - 2.0F * (y * y + z * z), 2.0F * (x * y - z * w), 2.0F * (x * z + y * w)},
            {2.0F * (x * y + z * w), 1.0F - 2.0F * (x * x + z * z), 2.0F * (y * z - x * w)},
            {2.0F * (x * z - y * w), 2.0F * (y * z + x * w), 1.0F - 2.0F * (x * x + y * y)},
        }};
        for (size_t c = 0; c < 3U; ++c) {
            const float scale = sa[c] + (sb[c] - sa[c]) * alpha;
            for (size_t r = 0; r < 3U; ++r) output.Floats[first + r][c] = rotation[r][c] * scale;
        }
    }
}

Affine34 PartialCameraDeltaPivot(const Affine34& d, float w);

// The camera change D applied for a fraction w, as a screw motion: rotation
// by w of D's angle about D's own rotation axis line, plus w of the slide
// along it. Orbiting Link the axis passes through him (the exact arc);
// walking while the camera turns a little it is the far centre of that
// turn (a wide arc, nearly a line). The older pivot, forced onto the view
// axis, exploded for small turns with motion (pivot thousands of units
// out: the scenery jumped back and forth in between frames while walking).
// TRIAEVUM_PIVOT_CAMERA=1 restores it.
Affine34 PartialCameraDelta(const Affine34& d, float w) {
    static const bool pivot = std::getenv("TRIAEVUM_PIVOT_CAMERA") != nullptr;
    if (pivot) return PartialCameraDeltaPivot(d, w);
    Affine34 result{};
    for (size_t row = 0; row < 3U; ++row) result[row][row] = 1.0F;
    if (w <= 0.0F) return result;
    std::array<std::array<double, 3>, 3> r{};
    std::array<double, 3> t{};
    for (size_t row = 0; row < 3U; ++row) {
        for (size_t column = 0; column < 3U; ++column) r[row][column] = d[row][column];
        t[row] = d[row][3];
    }
    std::array<double, 3> axis{r[2][1] - r[1][2], r[0][2] - r[2][0], r[1][0] - r[0][1]};
    const double twiceSine = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    const double angle = std::atan2(0.5 * twiceSine, 0.5 * (r[0][0] + r[1][1] + r[2][2] - 1.0));
    if (!(twiceSine > 1.0e-9) || !(angle > 1.0e-7)) {
        // No turn: the blend of the matrices.
        for (size_t row = 0; row < 3U; ++row) {
            for (size_t column = 0; column < 3U; ++column)
                result[row][column] = static_cast<float>((row == column ? 1.0 : 0.0) +
                                                         w * (r[row][column] - (row == column ? 1.0 : 0.0)));
            result[row][3] = static_cast<float>(w * t[row]);
        }
        return result;
    }
    for (double& v : axis) v /= twiceSine;
    const double x = axis[0], y = axis[1], z = axis[2];
    const double turn = angle * w, c = std::cos(turn), sn = std::sin(turn), k = 1.0 - c;
    const std::array<std::array<double, 3>, 3> rotation{{{c + x * x * k, x * y * k - z * sn, x * z * k + y * sn},
                                                         {y * x * k + z * sn, c + y * y * k, y * z * k - x * sn},
                                                         {z * x * k - y * sn, z * y * k + x * sn, c + z * z * k}}};
    // The slide along the axis, and a point a of the axis line:
    // (I - R + n n^T) a = t - (t.n) n (a has no part along n).
    const double slide = t[0] * x + t[1] * y + t[2] * z;
    std::array<double, 3> across{};
    std::array<std::array<double, 3>, 3> m{};
    for (size_t row = 0; row < 3U; ++row) {
        across[row] = t[row] - slide * axis[row];
        for (size_t column = 0; column < 3U; ++column)
            m[row][column] = (row == column ? 1.0 : 0.0) - r[row][column] + axis[row] * axis[column];
    }
    const auto det3 = [](const std::array<std::array<double, 3>, 3>& a) {
        return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
               a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    };
    const double det = det3(m);
    std::array<double, 3> point{};
    if (std::abs(det) > 1.0e-30) {
        for (size_t column = 0; column < 3U; ++column) {
            auto replaced = m;
            for (size_t row = 0; row < 3U; ++row) replaced[row][column] = across[row];
            point[column] = det3(replaced) / det;
        }
    }
    // C x = R_w (x - a) + a + w * slide * n
    for (size_t row = 0; row < 3U; ++row) {
        double rotatedPoint = 0.0;
        for (size_t column = 0; column < 3U; ++column) {
            result[row][column] = static_cast<float>(rotation[row][column]);
            rotatedPoint += rotation[row][column] * point[column];
        }
        result[row][3] = static_cast<float>(point[row] - rotatedPoint + w * slide * axis[row]);
    }
    return result;
}

Affine34 PartialCameraDeltaPivot(const Affine34& d, float w) {
    Affine34 result{};
    for (size_t row = 0; row < 3U; ++row) result[row][row] = 1.0F;
    if (w <= 0.0F) return result;
    // Rotation part as axis/angle.
    const float trace = d[0][0] + d[1][1] + d[2][2];
    const float angle = std::acos(std::clamp((trace - 1.0F) * 0.5F, -1.0F, 1.0F));
    std::array<std::array<float, 3>, 3> rotation{};
    if (angle < 1.0e-4F) {
        for (size_t r = 0; r < 3U; ++r)
            for (size_t c = 0; c < 3U; ++c) rotation[r][c] = (r == c ? 1.0F : 0.0F) + w * (d[r][c] - (r == c ? 1.0F : 0.0F));
    } else {
        std::array<float, 3> axis{d[2][1] - d[1][2], d[0][2] - d[2][0], d[1][0] - d[0][1]};
        const float length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        if (length < 1.0e-6F) return result;
        for (float& v : axis) v /= length;
        const float t = angle * w, c = std::cos(t), sn = std::sin(t), k = 1.0F - c;
        const float x = axis[0], y = axis[1], z = axis[2];
        rotation = {{{c + x * x * k, x * y * k - z * sn, x * z * k + y * sn},
                     {y * x * k + z * sn, c + y * y * k, y * z * k - x * sn},
                     {z * x * k - y * sn, z * y * k + x * sn, c + z * z * k}}};
    }
    // Pivot: the point on the view axis D moves least (the orbit target).
    // p = (0,0,-z): minimise |D p - p|^2 over z (closed form).
    std::array<float, 3> u{}, v{};  // D p - p = u * z + v
    for (size_t r = 0; r < 3U; ++r) {
        u[r] = -d[r][2] + (r == 2U ? 1.0F : 0.0F);
        v[r] = d[r][3];
    }
    const float uu = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
    const float z = uu > 1.0e-8F ? -(u[0] * v[0] + u[1] * v[1] + u[2] * v[2]) / uu : 0.0F;
    const std::array<float, 3> pivot{0.0F, 0.0F, -z};
    // C x = R_w (x - p) + p + w * (D p - p)
    for (size_t r = 0; r < 3U; ++r) {
        float rotatedPivot = 0.0F, movedPivot = d[r][3];
        for (size_t c = 0; c < 3U; ++c) {
            result[r][c] = rotation[r][c];
            rotatedPivot += rotation[r][c] * pivot[c];
            movedPivot += d[r][c] * pivot[c];
        }
        result[r][3] = -rotatedPivot + pivot[r] + w * (movedPivot - pivot[r]);
    }
    return result;
}

bool CameraInterpolationEnabled() {
    static const bool disabled = std::getenv("TRIAEVUM_NO_UNMATCHED_CAMERA") != nullptr;
    return !disabled;
}

Quaternion MultiplyQuaternion(const Quaternion& a, const Quaternion& b) {
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
            a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
            a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}

// The rotation q applied for a fraction w of its angle.
Quaternion PartialQuaternion(Quaternion q, float w) {
    if (q[3] < 0.0F)
        for (float& v : q) v = -v;
    const float angle = std::acos(std::clamp(q[3], -1.0F, 1.0F));
    const float sine = std::sin(angle);
    if (!(sine > 1.0e-6F)) return {0.0F, 0.0F, 0.0F, 1.0F};
    const float scale = std::sin(angle * w) / sine;
    return {q[0] * scale, q[1] * scale, q[2] * scale, std::cos(angle * w)};
}

std::array<std::array<float, 3>, 3> QuaternionMatrix(const Quaternion& q) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    return {{
        {1.0F - 2.0F * (y * y + z * z), 2.0F * (x * y - z * w), 2.0F * (x * z + y * w)},
        {2.0F * (x * y + z * w), 1.0F - 2.0F * (x * x + z * z), 2.0F * (y * z - x * w)},
        {2.0F * (x * z - y * w), 2.0F * (y * z + x * w), 1.0F - 2.0F * (x * x + y * y)},
    }};
}

// Rows 20.. hold the bone palette, and after the bones the model's other
// constants (lights, material): those do not change between the frames, or
// are no transform at all (a singular 3x3).
bool RowsMoved(const Oot3dPicaVertexUniformState& previous, const Oot3dPicaVertexUniformState& current,
               size_t first) {
    bool moved = false;
    for (size_t row = 0; row < 3U; ++row)
        if (previous.Floats[first + row] != current.Floats[first + row]) moved = true;
    if (!moved) return false;
    const float a = Determinant3(AffineAt(previous, first)), b = Determinant3(AffineAt(current, first));
    return std::isfinite(a) && std::isfinite(b) && std::abs(a) > 1.0e-15F && std::abs(b) > 1.0e-15F;
}

// x -> rotation * (x - from) + to, applied to the model/bone matrices.
void MoveMatrices(const std::array<std::array<float, 3>, 3>& rotation, const std::array<float, 3>& from,
                  const std::array<float, 3>& to, Oot3dPicaVertexUniformState& uniforms,
                  const Oot3dPicaVertexUniformState& rawPrevious, const Oot3dPicaVertexUniformState& rawCurrent) {
    for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
        if (!RowsMoved(rawPrevious, rawCurrent, first)) continue;
        const auto m = AffineAt(uniforms, first);
        for (size_t r = 0; r < 3U; ++r) {
            float translation = to[r];
            for (size_t k = 0; k < 3U; ++k) translation += rotation[r][k] * (m[k][3] - from[k]);
            for (size_t c = 0; c < 3U; ++c) {
                float value = 0.0F;
                for (size_t k = 0; k < 3U; ++k) value += rotation[r][k] * m[k][c];
                uniforms.Floats[first + r][c] = value;
            }
            uniforms.Floats[first + r][3] = translation;
        }
    }
}

bool RigidCameraApplies(const VisualDrawMatching& matching, const Oot3dPicaVertexUniformState& previous,
                        const Oot3dPicaVertexUniformState& current) {
    return matching.RigidCamera && CameraInterpolationEnabled() && matching.CameraDeltaValid &&
           UsesModelViewLayout(previous) && UsesModelViewLayout(current);
}

// Per model instance (its draws share the model pointer; a draw paired some
// other way is its own model): the rotation most of its bones made, found by
// consensus (rows the shader does not use hold leftovers), with where those
// bones went. Small turns stay linear, turns the game snaps snap.
void ComputeModelMotions(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                         VisualDrawMatching& matching) {
    static const bool disabled = std::getenv("TRIAEVUM_NO_MODEL_MOTION") != nullptr;
    static const bool trace = std::getenv("TRIAEVUM_MODEL_MOTION_TRACE") != nullptr;
    matching.Motions.clear();
    matching.MotionByCurrent.assign(current.Draws.size(), std::numeric_limits<uint32_t>::max());
    if (disabled) return;
    std::optional<Affine34> inverseCamera;
    if (matching.RigidCamera && matching.CameraDeltaValid) inverseCamera = InvertAffine(matching.CameraDelta);
    struct Row {
        Quaternion Relative;
        std::array<float, 3> From, To;
    };
    struct Group {
        std::vector<size_t> Draws;
        std::vector<Row> Rows;
        std::vector<Row> ReloadedRows;  // same as the draw before: maybe its own, maybe not
        bool RigidSpace = false;
    };
    std::unordered_map<uint64_t, Group> groups;
    for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
        const auto& match = matching.PreviousByCurrent[currentIndex];
        if (!match.has_value()) continue;
        const auto& currentUniforms = current.Draws[currentIndex].VertexShader.Uniforms;
        const auto& previousUniforms = previous.Draws[match->PreviousIndex].VertexShader.Uniforms;
        if (!UsesModelViewLayout(previousUniforms) || !UsesModelViewLayout(currentUniforms)) continue;
        const bool rigid = RigidCameraApplies(matching, previousUniforms, currentUniforms) && inverseCamera;
        const auto& composition = current.Draws[currentIndex].Composition;
        const uint64_t key =
            (composition.Provenance == Oot3dPicaCompositionProvenance::NativeModelInstance
                 ? static_cast<uint64_t>(composition.NativeValue)
                 : (1ULL << 40U) | currentIndex) |
            (rigid ? 1ULL << 48U : 0ULL);
        auto& group = groups[key];
        group.RigidSpace = rigid;
        group.Draws.push_back(currentIndex);
        // The uniform file keeps what the draw before set: rows this draw
        // did not load are someone else's (a wall drawn after Link holds
        // Link's bones in the rows it does not use).
        const Oot3dPicaVertexUniformState* before =
            currentIndex > 0U ? &current.Draws[currentIndex - 1U].VertexShader.Uniforms : nullptr;
        // A draw of no model and no skeleton (particles, a fire's glow) loads
        // only its first matrix: its other rows were Link's bones, so a
        // particle drawn after Deku Link spinning took his 55-65 degree turn
        // about him and was flung round him in between frames.
        static const bool allRows = std::getenv("TRIAEVUM_PARTICLE_ALL_ROWS") != nullptr;
        const bool ownMatrixOnly = !allRows &&
                                   composition.Provenance != Oot3dPicaCompositionProvenance::NativeModelInstance &&
                                   current.Draws[currentIndex].Skinning == nullptr;
        for (size_t first = 20U; first + 2U < (ownMatrixOnly ? 23U : 0x59U); first += 3U) {
            if (!RowsMoved(previousUniforms, currentUniforms, first)) continue;
            const bool reloaded =
                !ownMatrixOnly && before != nullptr && !RowsMoved(*before, currentUniforms, first);
            auto a = AffineAt(previousUniforms, first);
            if (rigid) a = MultiplyAffine(*inverseCamera, a);
            const auto b = AffineAt(currentUniforms, first);
            Quaternion qa{}, qb{};
            std::array<float, 3> sa{}, sb{};
            if (!DecomposeRotationScale(a, qa, sa) || !DecomposeRotationScale(b, qb, sb)) continue;
            Row row;
            row.Relative = MultiplyQuaternion(qb, {-qa[0], -qa[1], -qa[2], qa[3]});
            for (size_t r = 0; r < 3U; ++r) {
                row.From[r] = a[r][3];
                row.To[r] = b[r][3];
            }
            if (!std::all_of(row.Relative.begin(), row.Relative.end(), [](float v) { return std::isfinite(v); }))
                continue;
            (reloaded ? group.ReloadedRows : group.Rows).push_back(row);
        }
    }
    // A model drawn right after the one holding it can load the same bone
    // (Link's hand after his arm): then those rows are all it has.
    for (auto& [key, group] : groups)
        if (group.Rows.empty()) group.Rows = std::move(group.ReloadedRows);
    if (trace) {
        std::vector<bool> previousUsed(previous.Draws.size(), false);
        for (size_t i = 0; i < current.Draws.size(); ++i) {
            const auto& match = matching.PreviousByCurrent[i];
            if (match.has_value()) { previousUsed[match->PreviousIndex] = true; continue; }
            const auto& u = current.Draws[i].VertexShader.Uniforms;
            std::fprintf(stderr, "TRIAEVUM_UNPAIRED cur draw=%zu model=%08x ord=%u verts=%u idx=%08x t=%.0f,%.0f,%.0f\n", i,
                         current.Draws[i].Composition.NativeValue, current.Draws[i].Composition.Ordinal,
                         current.Draws[i].VertexCount, current.Draws[i].IndexPhysicalAddress, u.Floats[20][3], u.Floats[21][3], u.Floats[22][3]);
        }
        for (size_t i = 0; i < previous.Draws.size(); ++i) {
            if (previousUsed[i]) continue;
            const auto& u = previous.Draws[i].VertexShader.Uniforms;
            std::fprintf(stderr, "TRIAEVUM_UNPAIRED prev draw=%zu model=%08x ord=%u verts=%u idx=%08x t=%.0f,%.0f,%.0f\n", i,
                         previous.Draws[i].Composition.NativeValue, previous.Draws[i].Composition.Ordinal,
                         previous.Draws[i].VertexCount, previous.Draws[i].IndexPhysicalAddress, u.Floats[20][3], u.Floats[21][3], u.Floats[22][3]);
        }
    }
    if (trace)
        for (auto& [key, group] : groups)
            if (group.Rows.empty())
                std::fprintf(stderr, "TRIAEVUM_MODEL_MOTION key=%zx model=%08x draws=%zu no rows\n", group.Draws.front(),
                             current.Draws[group.Draws.front()].Composition.NativeValue, group.Draws.size());
    constexpr float kAgree = 0.9848F;      // within 20 degrees of each other
    constexpr float kLinearAngle = 0.21F;  // radians (12 degrees): linear is fine
    constexpr float kSnapAngle = 1.75F;    // radians (100 degrees)
    // Largest models first: what a smaller one carries along (Link's sword,
    // shield, mask: models of their own) takes the carrier's motion when that
    // motion explains its own, so it stays where it is held.
    std::vector<Group*> ordered;
    for (auto& [key, group] : groups) ordered.push_back(&group);
    std::sort(ordered.begin(), ordered.end(), [](const Group* a, const Group* b) {
        return a->Rows.size() != b->Rows.size() ? a->Rows.size() > b->Rows.size() : a->Draws < b->Draws;
    });
    for (Group* groupPointer : ordered) {
        auto& group = *groupPointer;
        const uint64_t key = group.Draws.front();
        if (group.Rows.empty()) continue;
        const auto dot = [](const Quaternion& a, const Quaternion& b) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        };
        const size_t stride = std::max<size_t>(1U, group.Rows.size() / 48U);
        size_t best = 0, bestVotes = 0;
        for (size_t i = 0; i < group.Rows.size(); i += stride) {
            size_t votes = 0;
            for (const auto& other : group.Rows)
                if (std::abs(dot(group.Rows[i].Relative, other.Relative)) > kAgree) ++votes;
            if (votes > bestVotes) { bestVotes = votes; best = i; }
        }
        const Quaternion reference = group.Rows[best].Relative;
        Quaternion sum{};
        ModelMotion motion;
        motion.RigidCameraSpace = group.RigidSpace;
        size_t count = 0;
        for (const auto& row : group.Rows) {
            const float d = dot(reference, row.Relative);
            if (std::abs(d) <= kAgree) continue;
            const float sign = d < 0.0F ? -1.0F : 1.0F;
            for (size_t i = 0; i < 4U; ++i) sum[i] += sign * row.Relative[i];
            for (size_t r = 0; r < 3U; ++r) {
                motion.PreviousPivot[r] += row.From[r];
                motion.CurrentPivot[r] += row.To[r];
            }
            ++count;
        }
        const float length = std::sqrt(dot(sum, sum));
        if (count == 0U || !(length > 1.0e-6F)) continue;
        for (size_t i = 0; i < 4U; ++i) motion.Rotation[i] = sum[i] / length;
        if (motion.Rotation[3] < 0.0F)
            for (float& v : motion.Rotation) v = -v;
        for (size_t r = 0; r < 3U; ++r) {
            motion.PreviousPivot[r] /= static_cast<float>(count);
            motion.CurrentPivot[r] /= static_cast<float>(count);
        }
        for (const auto& row : group.Rows) {
            float squared = 0.0F;
            for (size_t r = 0; r < 3U; ++r)
                squared += (row.To[r] - motion.CurrentPivot[r]) * (row.To[r] - motion.CurrentPivot[r]);
            motion.Radius = std::max(motion.Radius, std::sqrt(squared));
        }
        const float angle = 2.0F * std::acos(std::clamp(motion.Rotation[3], -1.0F, 1.0F));
        if (trace)
            std::fprintf(stderr, "TRIAEVUM_MODEL_MOTION seq=%llu layer=%d verts=%u key=%llx model=%08x kind=%d draws=%zu rows=%zu agree=%zu angle=%.1f pivot=%.0f,%.0f,%.0f%s\n",
                         static_cast<unsigned long long>(current.Sequence), static_cast<int>(current.Draws[key].Composition.Layer),
                         current.Draws[key].VertexCount,
                         static_cast<unsigned long long>(key), current.Draws[key].Composition.NativeValue,
                         static_cast<int>(matching.PreviousByCurrent[key]->Kind), group.Draws.size(), group.Rows.size(), count,
                         angle * 57.2958F, motion.CurrentPivot[0], motion.CurrentPivot[1], motion.CurrentPivot[2],
                         angle < kLinearAngle ? " linear" : "");
        if (angle < kLinearAngle) continue;
        motion.Snap = angle > kSnapAngle;
        auto index = static_cast<uint32_t>(matching.Motions.size());
        for (uint32_t other = 0; other < matching.Motions.size(); ++other) {
            const auto& carrier = matching.Motions[other];
            if (carrier.RigidCameraSpace != motion.RigidCameraSpace || carrier.Snap != motion.Snap ||
                std::abs(dot(carrier.Rotation, motion.Rotation)) <= kAgree)
                continue;
            const auto rotation = QuaternionMatrix(carrier.Rotation);
            float error = 0.0F, reach = 0.0F;
            for (size_t r = 0; r < 3U; ++r) {
                float moved = carrier.CurrentPivot[r];
                for (size_t k = 0; k < 3U; ++k)
                    moved += rotation[r][k] * (motion.PreviousPivot[k] - carrier.PreviousPivot[k]);
                error += (moved - motion.CurrentPivot[r]) * (moved - motion.CurrentPivot[r]);
                reach += (motion.PreviousPivot[r] - carrier.PreviousPivot[r]) *
                         (motion.PreviousPivot[r] - carrier.PreviousPivot[r]);
            }
            // Held: within the carrier's reach, and moved as part of it (an
            // object of its own that only turns the same way, a clock next to
            // a clock, is not).
            if (std::sqrt(reach) < 1.5F * carrier.Radius + 5.0F && std::sqrt(error) < 0.1F * std::sqrt(reach) + 1.5F) {
                index = other;
                break;
            }
        }
        if (trace && index != matching.Motions.size())
            std::fprintf(stderr, "TRIAEVUM_MODEL_MOTION carried by %u\n", index);
        if (index == matching.Motions.size()) matching.Motions.push_back(motion);
        for (const size_t draw : group.Draws) matching.MotionByCurrent[draw] = index;
    }
}

// previous -> current blended with the model's motion taken out: the
// previous pose moved onto the current one, the (small) rest blended, then
// the motion undone for the part of it not reached yet. One transform for
// every bone of the model, so the joints stay together.
void BlendWithModelMotion(const Oot3dPicaVertexUniformState& previous, const Oot3dPicaVertexUniformState& current,
                          float alpha, const ModelMotion& motion, Oot3dPicaVertexUniformState& output,
                          const Oot3dPicaVertexUniformState& rawPrevious) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    if (motion.Snap) {
        const auto& shown = alpha < 0.5F ? previous : current;
        for (size_t first = 20U; first + 2U < 0x59U; first += 3U)
            if (RowsMoved(rawPrevious, current, first))
                for (size_t row = 0; row < 3U; ++row) output.Floats[first + row] = shown.Floats[first + row];
        return;
    }
    auto aligned = previous;
    MoveMatrices(QuaternionMatrix(motion.Rotation), motion.PreviousPivot, motion.CurrentPivot, aligned,
                 rawPrevious, current);
    for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
        if (!RowsMoved(rawPrevious, current, first)) continue;
        for (size_t row = 0; row < 3U; ++row)
            for (size_t column = 0; column < 4U; ++column)
                output.Floats[first + row][column] = InterpolateFinite(
                    aligned.Floats[first + row][column], current.Floats[first + row][column], alpha);
    }
    InterpolateRotatedMatrices(aligned, current, alpha, output);
    // Undo (1 - alpha) of the motion, as a screw: the rotation around its
    // own axis and the slide along it. Which bones measured the motion then
    // does not matter (around their centroid it did: the in-between pose
    // shifted from one frame to the next, the head most).
    const float w = 1.0F - alpha;
    const auto rotation = QuaternionMatrix(motion.Rotation);
    std::array<float, 3> t{};  // current = rotation * previous + t
    for (size_t r = 0; r < 3U; ++r) {
        t[r] = motion.CurrentPivot[r];
        for (size_t k = 0; k < 3U; ++k) t[r] -= rotation[r][k] * motion.PreviousPivot[k];
    }
    const float half = std::acos(std::clamp(motion.Rotation[3], -1.0F, 1.0F));
    const float sine = std::sin(half);
    std::array<float, 3> point{}, to{};
    if (sine > 1.0e-4F) {
        const std::array<float, 3> u{motion.Rotation[0] / sine, motion.Rotation[1] / sine, motion.Rotation[2] / sine};
        const float slide = u[0] * t[0] + u[1] * t[1] + u[2] * t[2];
        std::array<float, 3> across{};
        for (size_t r = 0; r < 3U; ++r) across[r] = t[r] - slide * u[r];
        const std::array<float, 3> cross{u[1] * across[2] - u[2] * across[1], u[2] * across[0] - u[0] * across[2],
                                         u[0] * across[1] - u[1] * across[0]};
        const float cotangent = std::cos(half) / sine;
        for (size_t r = 0; r < 3U; ++r) {
            point[r] = 0.5F * (across[r] + cotangent * cross[r]);
            to[r] = point[r] - w * slide * u[r];
        }
    } else {
        for (size_t r = 0; r < 3U; ++r) to[r] = -w * t[r];
    }
    const Quaternion back = PartialQuaternion(
        {-motion.Rotation[0], -motion.Rotation[1], -motion.Rotation[2], motion.Rotation[3]}, w);
    MoveMatrices(QuaternionMatrix(back), point, to, output, rawPrevious, current);
}

// --- Model skeletons (MM3D) ---
//
// Every bone matrix of a skinned model blended on its own (even as a
// rotation) tore it at the joints in the in-between frames: a forearm
// turning fast slid off the elbow, a hand off the wrist. The game's own
// skeleton is known for MM3D models (the bones each draw loads, their
// parents): each bone is blended in its parent's space and placed on the
// parent's in-between pose, as the animation itself is built, so the
// joints stay together however fast the limbs turn.

struct AffineParts {
    Quaternion Rotation{};
    std::array<std::array<float, 3>, 3> Shape{};  // R^T * M: scale and shear
    std::array<float, 3> Translation{};
};

// M = R * Shape (Gram-Schmidt on the columns), R a proper rotation.
std::optional<AffineParts> DecomposeAffine(const Affine34& m) {
    std::array<std::array<float, 3>, 3> column{};
    const auto length = [](const std::array<float, 3>& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
    const auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    float largest = 0.0F;
    for (size_t c = 0; c < 3U; ++c) {
        for (size_t r = 0; r < 3U; ++r) column[c][r] = m[r][c];
        largest = std::max(largest, length(column[c]));
    }
    if (!std::isfinite(largest) || !(largest > 1.0e-20F)) return std::nullopt;
    std::array<std::array<float, 3>, 3> axis{};  // axis[c] = column c of R
    axis[0] = column[0];
    const float l0 = length(axis[0]);
    if (!(l0 > 1.0e-5F * largest)) return std::nullopt;
    for (float& v : axis[0]) v /= l0;
    const float d = dot(column[1], axis[0]);
    for (size_t r = 0; r < 3U; ++r) axis[1][r] = column[1][r] - d * axis[0][r];
    const float l1 = length(axis[1]);
    if (!(l1 > 1.0e-5F * largest)) return std::nullopt;
    for (float& v : axis[1]) v /= l1;
    axis[2] = {axis[0][1] * axis[1][2] - axis[0][2] * axis[1][1], axis[0][2] * axis[1][0] - axis[0][0] * axis[1][2],
               axis[0][0] * axis[1][1] - axis[0][1] * axis[1][0]};
    AffineParts parts;
    for (size_t i = 0; i < 3U; ++i)
        for (size_t j = 0; j < 3U; ++j) parts.Shape[i][j] = dot(axis[i], column[j]);
    const float r00 = axis[0][0], r11 = axis[1][1], r22 = axis[2][2];
    const float trace = r00 + r11 + r22;
    auto& q = parts.Rotation;
    if (trace > 0.0F) {
        const float t = std::sqrt(trace + 1.0F) * 2.0F;
        q = {(axis[1][2] - axis[2][1]) / t, (axis[2][0] - axis[0][2]) / t, (axis[0][1] - axis[1][0]) / t, 0.25F * t};
    } else if (r00 > r11 && r00 > r22) {
        const float t = std::sqrt(1.0F + r00 - r11 - r22) * 2.0F;
        q = {0.25F * t, (axis[1][0] + axis[0][1]) / t, (axis[2][0] + axis[0][2]) / t, (axis[1][2] - axis[2][1]) / t};
    } else if (r11 > r22) {
        const float t = std::sqrt(1.0F + r11 - r00 - r22) * 2.0F;
        q = {(axis[1][0] + axis[0][1]) / t, 0.25F * t, (axis[2][1] + axis[1][2]) / t, (axis[2][0] - axis[0][2]) / t};
    } else {
        const float t = std::sqrt(1.0F + r22 - r00 - r11) * 2.0F;
        q = {(axis[2][0] + axis[0][2]) / t, (axis[2][1] + axis[1][2]) / t, 0.25F * t, (axis[0][1] - axis[1][0]) / t};
    }
    const float norm = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!std::isfinite(norm) || !(norm > 1.0e-6F)) return std::nullopt;
    for (float& v : q) v /= norm;
    parts.Translation = {m[0][3], m[1][3], m[2][3]};
    return parts;
}

Quaternion SlerpQuaternion(const Quaternion& a, const Quaternion& b, float t) {
    const float cosine = std::clamp(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], -1.0F, 1.0F);
    Quaternion q{};
    if (cosine > 0.9995F) {
        for (size_t i = 0; i < 4U; ++i) q[i] = a[i] + (b[i] - a[i]) * t;
    } else {
        const float angle = std::acos(cosine), sine = std::sin(angle);
        const float wa = std::sin((1.0F - t) * angle) / sine, wb = std::sin(t * angle) / sine;
        for (size_t i = 0; i < 4U; ++i) q[i] = a[i] * wa + b[i] * wb;
    }
    const float norm = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(norm > 1.0e-6F)) return b;
    for (float& v : q) v /= norm;
    return q;
}

// A draw's palette entry: the bone, its inverse bind (smooth skinning) and
// the shape's position offset, as the game builds it.
Affine34 SkinnedPaletteMatrix(const Oot3dPicaDrawSkinning& skinning, size_t entry, const Affine34& bone) {
    const auto index = static_cast<size_t>(skinning.Bones[entry]);
    Affine34 m = skinning.InverseBind ? MultiplyAffine(bone, skinning.Pose->InverseBinds[index]) : bone;
    const auto& o = skinning.Offset;
    if (o[0] != 0.0F || o[1] != 0.0F || o[2] != 0.0F)
        for (size_t r = 0; r < 3U; ++r) m[r][3] += m[r][0] * o[0] + m[r][1] * o[1] + m[r][2] * o[2];
    return m;
}

// The draw's bone rows are what its pose says (else its bones are not the
// recorded ones: left to the per-draw blending).
bool SkinningMatchesUniforms(const Oot3dPicaDrawSkinning& skinning, const Oot3dPicaVertexUniformState& uniforms) {
    if (skinning.Pose == nullptr || skinning.Bones.empty() || 20U + 3U * skinning.Bones.size() > 0x59U) return false;
    for (size_t entry = 0; entry < skinning.Bones.size(); ++entry) {
        const auto index = static_cast<size_t>(skinning.Bones[entry]);
        if (index >= skinning.Pose->Bones.size() || index >= skinning.Pose->InverseBinds.size()) return false;
        const auto expected = SkinnedPaletteMatrix(skinning, entry, skinning.Pose->Bones[index]);
        for (size_t r = 0; r < 3U; ++r)
            for (size_t c = 0; c < 4U; ++c) {
                const float actual = uniforms.Floats[20U + 3U * entry + r][c];
                if (!(std::abs(actual - expected[r][c]) <= 1.0e-3F + 1.0e-4F * std::abs(actual))) return false;
            }
    }
    return true;
}

// TRIAEVUM_POSE_TRACE: the frame pair being built (0: not traced).
thread_local uint64_t gPoseTraceSequence = 0;

bool BuildPoseTransition(const Oot3dPicaModelPose& previous, const Oot3dPicaModelPose& current,
                         const Affine34* inverseCamera, PoseTransition& transition,
                         const Affine34* cameraDelta = nullptr) {
    constexpr float kSnapAngle = 1.75F;   // radians (100 degrees): the game snaps, so do we
    constexpr float kJump = 300.0F;       // a root moving further in one game frame was placed anew
    const size_t count = current.Bones.size();
    if (count == 0U || previous.Bones.size() != count || current.Parents.size() != count ||
        previous.Parents != current.Parents)
        return false;
    transition.Previous = &previous;
    transition.Current = &current;
    transition.RigidCameraSpace = inverseCamera != nullptr;
    transition.Moving = transition.RigidCameraSpace;
    transition.PreviousBones.resize(count);
    for (size_t i = 0; i < count; ++i) {
        transition.PreviousBones[i] =
            inverseCamera != nullptr ? MultiplyAffine(*inverseCamera, previous.Bones[i]) : previous.Bones[i];
        if (previous.Bones[i] != current.Bones[i]) transition.Moving = true;
    }
    // Parents first: by depth (a parent chain longer than the bones is a loop).
    std::vector<uint16_t> depth(count, 0U);
    for (size_t i = 0; i < count; ++i) {
        size_t steps = 0;
        for (int parent = current.Parents[i]; parent >= 0; parent = current.Parents[static_cast<size_t>(parent)])
            if (static_cast<size_t>(parent) >= count || ++steps > count) return false;
        depth[i] = static_cast<uint16_t>(steps);
    }
    transition.Order.resize(count);
    for (size_t i = 0; i < count; ++i) transition.Order[i] = static_cast<uint16_t>(i);
    std::stable_sort(transition.Order.begin(), transition.Order.end(),
                     [&](uint16_t a, uint16_t b) { return depth[a] < depth[b]; });
    transition.Bones.resize(count);
    for (size_t i = 0; i < count; ++i) {
        auto& bone = transition.Bones[i];
        Affine34 local0 = transition.PreviousBones[i], local1 = current.Bones[i];
        const int parent = current.Parents[i];
        if (parent >= 0) {
            const auto inverse0 = InvertAffine(transition.PreviousBones[static_cast<size_t>(parent)]);
            const auto inverse1 = InvertAffine(current.Bones[static_cast<size_t>(parent)]);
            // A parent scaled to nothing (a hidden part) has no space: the
            // bone is blended on its own.
            if (inverse0 && inverse1) {
                local0 = MultiplyAffine(*inverse0, transition.PreviousBones[i]);
                local1 = MultiplyAffine(*inverse1, current.Bones[i]);
                bone.Parent = static_cast<int16_t>(parent);
            }
        }
        bone.Local0 = local0;
        bone.Local1 = local1;
        const auto parts0 = DecomposeAffine(local0), parts1 = DecomposeAffine(local1);
        // A mirrored side on one frame only cannot turn into the other.
        if (parts0 && parts1 && (parts0->Shape[2][2] < 0.0F) == (parts1->Shape[2][2] < 0.0F)) {
            bone.Decomposed = true;
            bone.Rotation0 = parts0->Rotation;
            bone.Rotation1 = parts1->Rotation;
            const float cosine = bone.Rotation0[0] * bone.Rotation1[0] + bone.Rotation0[1] * bone.Rotation1[1] +
                                 bone.Rotation0[2] * bone.Rotation1[2] + bone.Rotation0[3] * bone.Rotation1[3];
            if (cosine < 0.0F)
                for (float& v : bone.Rotation1) v = -v;
            bone.Shape0 = parts0->Shape;
            bone.Shape1 = parts1->Shape;
            bone.Translation0 = parts0->Translation;
            bone.Translation1 = parts1->Translation;
            const float turned = 2.0F * std::acos(std::min(std::abs(cosine), 1.0F));
            static const float sharpAngle = [] {
                const char* value = std::getenv("TRIAEVUM_POSE_SHARP_DEGREES");
                return (value != nullptr ? std::strtof(value, nullptr) : 60.0F) * 0.0174533F;
            }();
            bone.SharpTurn = turned > sharpAngle;
            if (gPoseTraceSequence != 0U && bone.Parent >= 0 && turned > 1.6F)
                std::fprintf(stderr, "TRIAEVUM_POSE_LOCAL seq=%llu model=%08x bones=%zu bone=%zu parent=%d angle=%.1f\n",
                             static_cast<unsigned long long>(gPoseTraceSequence), current.Model, count, i, bone.Parent,
                             turned * 57.2958F);
        }
        if (bone.Parent >= 0) {
            // A sharp turn against the parent: blended against the ancestor
            // it turned least against (the forearm and hand turning far
            // opposite ways: the arm's shoulder; Link rolling: the parent),
            // where a halfway turn is least ambiguous.
            std::optional<AffineParts> world0, world1;
            float least = 4.0F;
            for (int ancestor = bone.Parent; ancestor >= 0; ancestor = transition.Bones[static_cast<size_t>(ancestor)].Parent) {
                const auto a = static_cast<size_t>(ancestor);
                const auto inverse0 = InvertAffine(transition.PreviousBones[a]);
                const auto inverse1 = InvertAffine(current.Bones[a]);
                if (!inverse0 || !inverse1) break;
                auto relative0 = DecomposeAffine(MultiplyAffine(*inverse0, transition.PreviousBones[i]));
                auto relative1 = DecomposeAffine(MultiplyAffine(*inverse1, current.Bones[i]));
                if (!relative0 || !relative1) break;
                const auto& q0 = relative0->Rotation;
                const auto& q1 = relative1->Rotation;
                const float angle = 2.0F * std::acos(std::min(std::abs(q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3]), 1.0F));
                if (angle < least - 1.0e-3F) {
                    least = angle;
                    bone.Root = static_cast<uint16_t>(a);
                    world0 = std::move(relative0);
                    world1 = std::move(relative1);
                }
                if (transition.Bones[a].Parent < 0) break;
            }
            if (world0 && world1 && (world0->Shape[2][2] < 0.0F) == (world1->Shape[2][2] < 0.0F)) {
                bone.WorldDecomposed = true;
                bone.WorldRotation0 = world0->Rotation;
                bone.WorldRotation1 = world1->Rotation;
                const float c = bone.WorldRotation0[0] * bone.WorldRotation1[0] + bone.WorldRotation0[1] * bone.WorldRotation1[1] +
                                bone.WorldRotation0[2] * bone.WorldRotation1[2] + bone.WorldRotation0[3] * bone.WorldRotation1[3];
                if (c < 0.0F)
                    for (float& v : bone.WorldRotation1) v = -v;
                bone.WorldShape0 = world0->Shape;
                bone.WorldShape1 = world1->Shape;
            }
        }
        if (current.Parents[i] >= 0 || !transition.Moving) continue;
        // A root that turned past what any animation does in one game frame
        // (Link reversing) or jumped: the whole model snaps, as in the game.
        float angle = 0.0F;
        float squared = 0.0F;
        // Camera space roots also move with the camera: a far model's root
        // "moved" 400-700 units when the camera turned 3-5 degrees and the
        // whole model (a mountain, the field) snapped in between frames. The
        // test compares the roots with the camera's change taken out (the
        // rigid camera mode already did, for turns over 5 degrees).
        static const bool cameraBlind = std::getenv("TRIAEVUM_NO_POSE_SNAP_CAMERA") != nullptr;
        const auto still0 = DecomposeAffine(previous.Bones[i]);
        const auto still1 = !cameraBlind && inverseCamera == nullptr && cameraDelta != nullptr
                                ? DecomposeAffine(MultiplyAffine(*cameraDelta, current.Bones[i]))
                                : std::optional<AffineParts>{};
        if (still0 && still1) {
            const auto& q0 = still0->Rotation;
            const auto& q1 = still1->Rotation;
            angle = 2.0F * std::acos(std::min(std::abs(q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3]), 1.0F));
            for (size_t r = 0; r < 3U; ++r)
                squared += (still1->Translation[r] - still0->Translation[r]) * (still1->Translation[r] - still0->Translation[r]);
        } else {
            if (bone.Decomposed) {
                const float cosine = std::abs(bone.Rotation0[0] * bone.Rotation1[0] + bone.Rotation0[1] * bone.Rotation1[1] +
                                              bone.Rotation0[2] * bone.Rotation1[2] + bone.Rotation0[3] * bone.Rotation1[3]);
                angle = 2.0F * std::acos(std::min(cosine, 1.0F));
            }
            for (size_t r = 0; r < 3U; ++r)
                squared += (local1[r][3] - local0[r][3]) * (local1[r][3] - local0[r][3]);
        }
        if (angle > kSnapAngle) transition.Snap = true;
        if (!(squared <= kJump * kJump)) transition.Snap = true;
        if (gPoseTraceSequence != 0U && (angle > 1.0F || squared > 30.0F * 30.0F))
            std::fprintf(stderr, "TRIAEVUM_POSE_ROOT seq=%llu model=%08x bones=%zu angle=%.1f move=%.1f%s\n",
                         static_cast<unsigned long long>(gPoseTraceSequence), current.Model, count, angle * 57.2958F,
                         std::sqrt(squared), transition.Snap ? " snap" : "");
    }
    return true;
}

// A frame's poses by (model, n-th draw of that model in the frame).
std::unordered_map<uint64_t, const Oot3dPicaModelPose*> CollectFramePoses(const Oot3dPicaVisualFrame& frame) {
    std::unordered_map<uint64_t, const Oot3dPicaModelPose*> poses;
    std::unordered_map<uint32_t, uint32_t> occurrences;
    std::unordered_set<const Oot3dPicaModelPose*> seen;
    for (const auto& draw : frame.Draws) {
        if (draw.Skinning == nullptr || draw.Skinning->Pose == nullptr) continue;
        const auto* pose = draw.Skinning->Pose.get();
        if (!seen.insert(pose).second) continue;
        const uint32_t occurrence = occurrences[pose->Model]++;
        poses.emplace((static_cast<uint64_t>(pose->Model) << 32U) | occurrence, pose);
    }
    return poses;
}

void ComputePoseTransitions(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                            VisualDrawMatching& matching) {
    static const bool disabled = std::getenv("TRIAEVUM_NO_POSE_INTERPOLATION") != nullptr;
    static const bool trace = std::getenv("TRIAEVUM_POSE_TRACE") != nullptr;
    matching.Poses.clear();
    matching.StillPoses.clear();
    matching.PoseByCurrent.assign(current.Draws.size(), std::numeric_limits<uint32_t>::max());
    if (disabled || matching.CameraCut) return;
    const auto previousPoses = CollectFramePoses(previous);
    const auto currentPoses = CollectFramePoses(current);
    if (currentPoses.empty() || previousPoses.empty()) return;
    std::optional<Affine34> inverseCamera;
    if (matching.RigidCamera && matching.CameraDeltaValid && CameraInterpolationEnabled())
        inverseCamera = InvertAffine(matching.CameraDelta);
    gPoseTraceSequence = trace && current.Sequence == previous.Sequence + 1U ? current.Sequence : 0U;
    std::unordered_map<const Oot3dPicaModelPose*, uint32_t> indexByPose;
    // Pooled models: MM3D's field bushes are a few model instances the game
    // hands to whichever bushes are near the camera, so one instance is
    // another bush when the view swings (a 150-unit jump against its
    // neighbours' 40-70): paired by instance they slid between bushes or
    // snapped in between frames, flickering on every fast turn. A previous
    // pose more than 10 units from where the camera puts it (root, camera
    // change removed) gives way to the nearest unused previous pose of the
    // same skeleton within 25 units; with none, the current pose is shown.
    static const bool noRepair = std::getenv("TRIAEVUM_NO_POSE_REPAIR") != nullptr;
    const bool repair = !noRepair && matching.CameraDeltaValid && CameraInterpolationEnabled();
    const auto rootPlace = [](const Oot3dPicaModelPose& pose) -> std::optional<std::array<float, 3>> {
        for (size_t i = 0; i < pose.Bones.size() && i < pose.Parents.size(); ++i)
            if (pose.Parents[i] < 0) return std::array<float, 3>{pose.Bones[i][0][3], pose.Bones[i][1][3], pose.Bones[i][2][3]};
        return std::nullopt;
    };
    const auto distance = [&](const Oot3dPicaModelPose& before, const Oot3dPicaModelPose& now) {
        const auto a = rootPlace(before), b = rootPlace(now);
        if (!a || !b) return std::numeric_limits<float>::infinity();
        const auto& d = matching.CameraDelta;
        float squared = 0.0F;
        for (size_t r = 0; r < 3U; ++r) {
            const float moved = d[r][0] * (*b)[0] + d[r][1] * (*b)[1] + d[r][2] * (*b)[2] + d[r][3];
            squared += (moved - (*a)[r]) * (moved - (*a)[r]);
        }
        return std::sqrt(squared);
    };
    const auto sameSkeleton = [](const Oot3dPicaModelPose& a, const Oot3dPicaModelPose& b) {
        return a.Skeleton == b.Skeleton && a.Bones.size() == b.Bones.size() && a.Parents == b.Parents;
    };
    std::unordered_map<const Oot3dPicaModelPose*, const Oot3dPicaModelPose*> chosen;
    if (repair) {
        std::unordered_set<const Oot3dPicaModelPose*> used;
        std::vector<const Oot3dPicaModelPose*> unsettled;
        std::unordered_map<const Oot3dPicaModelPose*, const Oot3dPicaModelPose*> own;
        for (const auto& [key, currentPose] : currentPoses) {
            const auto found = previousPoses.find(key);
            const bool same = found != previousPoses.end() && sameSkeleton(*found->second, *currentPose);
            if (same) own[currentPose] = found->second;
            if (same && distance(*found->second, *currentPose) <= 10.0F) {
                chosen[currentPose] = found->second;
                used.insert(found->second);
            } else {
                unsettled.push_back(currentPose);
            }
        }
        for (const auto* currentPose : unsettled) {
            const Oot3dPicaModelPose* best = nullptr;
            float bestDistance = 25.0F;
            size_t sharing = 0;
            for (const auto& [key, previousPose] : previousPoses) {
                if (!sameSkeleton(*previousPose, *currentPose)) continue;
                ++sharing;
                if (used.contains(previousPose)) continue;
                const float d = distance(*previousPose, *currentPose);
                if (d < bestDistance) { bestDistance = d; best = previousPose; }
            }
            // A model alone with its skeleton (Link rolling, Epona: more
            // than 10 units a frame) keeps its own previous pose however far
            // (the jump test still snaps); pooled ones (shared skeleton, an
            // instance handed to another bush 30-150 units away) do not.
            const auto mine = own.find(currentPose);
            if (best == nullptr && sharing < 2U && mine != own.end() && !used.contains(mine->second))
                best = mine->second;
            if (best != nullptr) used.insert(best);
            if (best == nullptr) {
                // Nothing of it near before (a bush handed a pooled
                // instance): still in the world, so before it was where the
                // camera's change puts it.
                auto& still = matching.StillPoses.emplace_back(*currentPose);
                for (auto& bone : still.Bones) bone = MultiplyAffine(matching.CameraDelta, bone);
                best = &still;
            }
            chosen[currentPose] = best;
            if (gPoseTraceSequence != 0U)
                std::fprintf(stderr, "TRIAEVUM_POSE_REPAIR seq=%llu model=%08x from=%08x distance=%.1f\n",
                             static_cast<unsigned long long>(gPoseTraceSequence), currentPose->Model,
                             best != nullptr ? best->Model : 0U, best != nullptr ? bestDistance : -1.0F);
        }
    }
    for (const auto& [key, currentPose] : currentPoses) {
        const Oot3dPicaModelPose* before = nullptr;
        if (repair) {
            before = chosen[currentPose];
        } else {
            const auto found = previousPoses.find(key);
            if (found == previousPoses.end() || found->second->Skeleton != currentPose->Skeleton) continue;
            before = found->second;
        }
        PoseTransition transition;
        if (!BuildPoseTransition(*before, *currentPose, inverseCamera ? &*inverseCamera : nullptr, transition,
                                 matching.CameraDeltaValid && CameraInterpolationEnabled() ? &matching.CameraDelta
                                                                                           : nullptr))
            continue;
        indexByPose.emplace(currentPose, static_cast<uint32_t>(matching.Poses.size()));
        matching.Poses.push_back(std::move(transition));
    }
    gPoseTraceSequence = 0U;
    // Carriers before what they may hold: more bones first.
    auto& poses = matching.Poses;
    matching.PoseOrder.resize(poses.size());
    for (uint32_t i = 0; i < poses.size(); ++i) matching.PoseOrder[i] = i;
    std::stable_sort(matching.PoseOrder.begin(), matching.PoseOrder.end(), [&](uint32_t a, uint32_t b) {
        return poses[a].Bones.size() > poses[b].Bones.size();
    });
    std::vector<uint32_t> rank(poses.size(), 0U);
    for (uint32_t k = 0; k < matching.PoseOrder.size(); ++k) rank[matching.PoseOrder[k]] = k;
    // A model whose roots kept their place on another model's (moving) bone
    // from one frame to the next is held by it.
    static const bool noCarriers = std::getenv("TRIAEVUM_NO_POSE_CARRIERS") != nullptr;
    if (!noCarriers) {
        const auto moved = [](const Affine34& a, const Affine34& b) {
            for (size_t r = 0; r < 3U; ++r) {
                for (size_t c = 0; c < 3U; ++c)
                    if (std::abs(a[r][c] - b[r][c]) > 1.0e-3F * (std::abs(b[r][c]) + 1.0e-3F)) return true;
                if (std::abs(a[r][3] - b[r][3]) > 0.05F) return true;
            }
            return false;
        };
        // Inverses of every moving bone, once.
        std::vector<std::vector<std::optional<std::pair<Affine34, Affine34>>>> inverses(poses.size());
        for (size_t u = 0; u < poses.size(); ++u) {
            const auto& carrier = poses[u];
            inverses[u].resize(carrier.Bones.size());
            if (!carrier.Moving || carrier.Snap) continue;
            for (size_t b = 0; b < carrier.Bones.size(); ++b) {
                if (!moved(carrier.PreviousBones[b], carrier.Current->Bones[b])) continue;
                const auto i0 = InvertAffine(carrier.PreviousBones[b]);
                const auto i1 = InvertAffine(carrier.Current->Bones[b]);
                if (i0 && i1) inverses[u][b] = std::make_pair(*i0, *i1);
            }
        }
        for (size_t t = 0; t < poses.size(); ++t) {
            auto& held = poses[t];
            if (!held.Moving) continue;
            std::vector<uint16_t> roots;
            for (size_t r = 0; r < held.Bones.size(); ++r)
                if (held.Current->Parents[r] < 0) roots.push_back(static_cast<uint16_t>(r));
            if (roots.empty() || !moved(held.PreviousBones[roots[0]], held.Current->Bones[roots[0]])) continue;
            float bestDistance = 100.0F;  // within reach of the holding bone
            for (size_t u = 0; u < poses.size(); ++u) {
                if (u == t || rank[u] >= rank[t]) continue;
                const auto& carrier = poses[u];
                for (size_t b = 0; b < carrier.Bones.size(); ++b) {
                    if (!inverses[u][b]) continue;
                    const auto& bone = carrier.Current->Bones[b];
                    const auto& root = held.Current->Bones[roots[0]];
                    const float distance = std::sqrt((bone[0][3] - root[0][3]) * (bone[0][3] - root[0][3]) +
                                                     (bone[1][3] - root[1][3]) * (bone[1][3] - root[1][3]) +
                                                     (bone[2][3] - root[2][3]) * (bone[2][3] - root[2][3]));
                    if (!(distance < bestDistance)) continue;
                    bool kept = true;
                    std::vector<std::pair<uint16_t, std::array<Affine34, 2>>> places;
                    for (const uint16_t r : roots) {
                        const auto place0 = MultiplyAffine(inverses[u][b]->first, held.PreviousBones[r]);
                        const auto place1 = MultiplyAffine(inverses[u][b]->second, held.Current->Bones[r]);
                        float scale = 0.0F;
                        for (size_t row = 0; row < 3U; ++row)
                            for (size_t c = 0; c < 3U; ++c) scale = std::max(scale, std::abs(place1[row][c]));
                        for (size_t row = 0; row < 3U && kept; ++row)
                            for (size_t c = 0; c < 4U && kept; ++c)
                                kept = std::abs(place0[row][c] - place1[row][c]) <=
                                       2.0e-3F * (c == 3U ? std::abs(place1[row][c]) + 1.0F : scale);
                        if (!kept) break;
                        places.emplace_back(r, std::array<Affine34, 2>{place0, place1});
                    }
                    if (!kept) continue;
                    bestDistance = distance;
                    held.Carrier = static_cast<uint32_t>(u);
                    held.CarrierBone = static_cast<uint16_t>(b);
                    held.HeldRoots = std::move(places);
                }
            }
            if (trace && held.Carrier < poses.size())
                std::fprintf(stderr, "TRIAEVUM_POSE_CARRIED seq=%llu model=%08x bones=%zu by=%08x bone=%u distance=%.1f\n",
                             static_cast<unsigned long long>(current.Sequence), held.Current->Model, held.Bones.size(),
                             poses[held.Carrier].Current->Model, held.CarrierBone, bestDistance);
        }
    }
    size_t mismatched = 0;
    for (size_t index = 0; index < current.Draws.size(); ++index) {
        const auto& draw = current.Draws[index];
        if (draw.Skinning == nullptr || draw.Skinning->Pose == nullptr) continue;
        const auto found = indexByPose.find(draw.Skinning->Pose.get());
        if (found == indexByPose.end() || !UsesModelViewLayout(draw.VertexShader.Uniforms)) continue;
        if (!SkinningMatchesUniforms(*draw.Skinning, draw.VertexShader.Uniforms)) {
            ++mismatched;
            continue;
        }
        matching.PoseByCurrent[index] = found->second;
    }
    // Parts by geometry and bones; one in a frame only was switched by the
    // game (Link's shield from his back into his hand: the same mesh on
    // another bone, or the open hand for the fist).
    static const bool noPartSwitch = std::getenv("TRIAEVUM_NO_PART_SWITCH") != nullptr;
    matching.PoseAppeared.assign(current.Draws.size(), false);
    matching.PreviousInPose.assign(previous.Draws.size(), false);
    matching.PoseVanished.clear();
    if (!noPartSwitch) {
        const auto partKey = [](const Oot3dPicaVulkanDrawPlan& draw) {
            // The mesh by where its data is (cheap: no content hashing).
            uint64_t hash = kFnvOffset;
            HashValue(hash, draw.IndexPhysicalAddress);
            HashValue(hash, draw.Indexed);
            HashValue(hash, draw.BaseVertex);
            HashValue(hash, draw.VertexCount);
            HashValue(hash, draw.VertexShader.StateKey);
            for (const auto& binding : draw.VertexBindings) {
                HashValue(hash, binding.Binding);
                HashValue(hash, binding.ByteStride);
                HashValue(hash, binding.ResolvedBytes().size());
            }
            for (const auto& texture : draw.Textures) HashValue(hash, texture.State.PhysicalAddress);
            HashValue(hash, draw.Skinning->InverseBind);
            for (const int16_t bone : draw.Skinning->Bones) HashValue(hash, bone);
            for (const float offset : draw.Skinning->Offset) HashValue(hash, offset);
            return hash;
        };
        std::unordered_map<const Oot3dPicaModelPose*, uint32_t> transitionByPrevious;
        for (uint32_t t = 0; t < matching.Poses.size(); ++t) transitionByPrevious.emplace(matching.Poses[t].Previous, t);
        std::vector<std::unordered_map<uint64_t, std::vector<size_t>>> previousParts(matching.Poses.size());
        for (size_t index = 0; index < previous.Draws.size(); ++index) {
            const auto& draw = previous.Draws[index];
            if (draw.Skinning == nullptr || draw.Skinning->Pose == nullptr) continue;
            const auto found = transitionByPrevious.find(draw.Skinning->Pose.get());
            if (found == transitionByPrevious.end() || !UsesModelViewLayout(draw.VertexShader.Uniforms) ||
                !SkinningMatchesUniforms(*draw.Skinning, draw.VertexShader.Uniforms))
                continue;
            matching.PreviousInPose[index] = true;
            previousParts[found->second][partKey(draw)].push_back(index);
        }
        constexpr size_t kNone = std::numeric_limits<size_t>::max();
        std::vector<size_t> lastOfPose(matching.Poses.size(), kNone);
        for (size_t index = 0; index < current.Draws.size(); ++index) {
            const uint32_t pose = matching.PoseByCurrent[index];
            if (pose >= matching.Poses.size()) continue;
            lastOfPose[pose] = index;
            auto& parts = previousParts[pose];
            const auto found = parts.find(partKey(current.Draws[index]));
            if (found == parts.end() || found->second.empty()) {
                matching.PoseAppeared[index] = true;
            } else {
                found->second.erase(found->second.begin());
            }
        }
        for (uint32_t pose = 0; pose < matching.Poses.size(); ++pose)
            for (const auto& [key, left] : previousParts[pose])
                for (const size_t index : left)
                    if (lastOfPose[pose] != kNone) matching.PoseVanished.push_back({index, pose, lastOfPose[pose]});
        std::sort(matching.PoseVanished.begin(), matching.PoseVanished.end(),
                  [](const auto& a, const auto& b) { return a.PreviousIndex < b.PreviousIndex; });
    }
    if (trace) {
        size_t snapped = 0, moving = 0, bones = 0;
        for (const auto& t : matching.Poses) {
            snapped += t.Snap ? 1U : 0U;
            moving += t.Moving ? 1U : 0U;
            bones += t.Bones.size();
        }
        std::fprintf(stderr, "TRIAEVUM_POSES seq=%llu poses=%zu/%zu paired=%zu moving=%zu snapped=%zu bones=%zu mismatched=%zu\n",
                     static_cast<unsigned long long>(current.Sequence), currentPoses.size(), previousPoses.size(),
                     matching.Poses.size(), moving, snapped, bones, mismatched);
    }
}

// The in-between skeleton, in the current view (before the camera's own
// in-between motion). carrier: the holding model's, already sampled.
void SamplePoseTransition(const PoseTransition& transition, float alpha, const std::vector<Affine34>* carrier,
                          std::vector<Affine34>& bones) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    const size_t count = transition.Bones.size();
    if (transition.Snap && carrier == nullptr) {
        bones = alpha < 0.5F ? transition.PreviousBones : transition.Current->Bones;
    } else {
        bones.resize(count);
        for (const uint16_t index : transition.Order) {
            const auto& bone = transition.Bones[index];
            if (carrier != nullptr && bone.Parent < 0) {
                const auto held = std::find_if(transition.HeldRoots.begin(), transition.HeldRoots.end(),
                                               [&](const auto& root) { return root.first == index; });
                if (held != transition.HeldRoots.end()) {
                    Affine34 place{};
                    for (size_t r = 0; r < 3U; ++r)
                        for (size_t c = 0; c < 4U; ++c)
                            place[r][c] = held->second[0][r][c] + (held->second[1][r][c] - held->second[0][r][c]) * alpha;
                    bones[index] = MultiplyAffine((*carrier)[transition.CarrierBone], place);
                    continue;
                }
            }
            static const bool localRotation = std::getenv("TRIAEVUM_POSE_LOCAL_ROTATION") != nullptr;
            if (!localRotation && bone.Parent >= 0 && bone.WorldDecomposed && bone.Decomposed && bone.SharpTurn) {
                // Orientation blended in the view (a forearm and hand turning
                // far opposite ways in one game frame keep the hand's own
                // turn), the joint where the parent's in-between pose puts it.
                const auto rotation = QuaternionMatrix(SlerpQuaternion(bone.WorldRotation0, bone.WorldRotation1, alpha));
                const auto& parent = bones[static_cast<size_t>(bone.Parent)];
                const auto& root = bones[bone.Root];
                std::array<float, 3> joint{};
                for (size_t r = 0; r < 3U; ++r)
                    joint[r] = bone.Translation0[r] + (bone.Translation1[r] - bone.Translation0[r]) * alpha;
                std::array<std::array<float, 3>, 3> model{};  // in the root's space
                for (size_t r = 0; r < 3U; ++r)
                    for (size_t c = 0; c < 3U; ++c) {
                        float value = 0.0F;
                        for (size_t k = 0; k < 3U; ++k) {
                            const float shape =
                                bone.WorldShape0[k][c] + (bone.WorldShape1[k][c] - bone.WorldShape0[k][c]) * alpha;
                            value += rotation[r][k] * shape;
                        }
                        model[r][c] = value;
                    }
                Affine34 world{};
                for (size_t r = 0; r < 3U; ++r) {
                    for (size_t c = 0; c < 3U; ++c)
                        world[r][c] = root[r][0] * model[0][c] + root[r][1] * model[1][c] + root[r][2] * model[2][c];
                    world[r][3] = parent[r][3] + parent[r][0] * joint[0] + parent[r][1] * joint[1] + parent[r][2] * joint[2];
                }
                bones[index] = world;
                continue;
            }
            Affine34 local{};
            if (bone.Decomposed) {
                const auto rotation = QuaternionMatrix(SlerpQuaternion(bone.Rotation0, bone.Rotation1, alpha));
                std::array<std::array<float, 3>, 3> shape{};
                for (size_t r = 0; r < 3U; ++r)
                    for (size_t c = 0; c < 3U; ++c)
                        shape[r][c] = bone.Shape0[r][c] + (bone.Shape1[r][c] - bone.Shape0[r][c]) * alpha;
                for (size_t r = 0; r < 3U; ++r) {
                    for (size_t c = 0; c < 3U; ++c)
                        local[r][c] = rotation[r][0] * shape[0][c] + rotation[r][1] * shape[1][c] + rotation[r][2] * shape[2][c];
                    local[r][3] = bone.Translation0[r] + (bone.Translation1[r] - bone.Translation0[r]) * alpha;
                }
            } else {
                for (size_t r = 0; r < 3U; ++r)
                    for (size_t c = 0; c < 4U; ++c)
                        local[r][c] = InterpolateFinite(bone.Local0[r][c], bone.Local1[r][c], alpha);
            }
            bones[index] = bone.Parent >= 0 ? MultiplyAffine(bones[static_cast<size_t>(bone.Parent)], local) : local;
        }
    }
}

// A model part's bone rows from its sampled skeleton.
void WriteSkinnedRows(const Oot3dPicaDrawSkinning& skinning, const std::vector<Affine34>& bones,
                      Oot3dPicaVertexUniformState& uniforms) {
    for (size_t entry = 0; entry < skinning.Bones.size(); ++entry) {
        const auto m = SkinnedPaletteMatrix(skinning, entry, bones[static_cast<size_t>(skinning.Bones[entry])]);
        for (size_t r = 0; r < 3U; ++r) uniforms.Floats[20U + 3U * entry + r] = m[r];
    }
}

// Every needed skeleton (and its carriers) at alpha, in the in-between view.
std::vector<std::vector<Affine34>> SampleModelPoses(const VisualDrawMatching& matching, float alpha,
                                                    std::vector<bool> needed) {
    const auto& poses = matching.Poses;
    for (uint32_t index = 0; index < poses.size(); ++index)
        if (needed[index])
            for (uint32_t pose = poses[index].Carrier; pose < poses.size() && !needed[pose]; pose = poses[pose].Carrier)
                needed[pose] = true;
    std::vector<std::vector<Affine34>> sampled(poses.size());
    for (const uint32_t pose : matching.PoseOrder) {
        if (!needed[pose]) continue;
        const uint32_t carrier = poses[pose].Carrier;
        SamplePoseTransition(poses[pose], alpha, carrier < poses.size() ? &sampled[carrier] : nullptr, sampled[pose]);
    }
    if (matching.RigidCamera && matching.CameraDeltaValid && CameraInterpolationEnabled()) {
        const auto camera = PartialCameraDelta(matching.CameraDelta, 1.0F - std::clamp(alpha, 0.0F, 1.0F));
        for (uint32_t pose = 0; pose < poses.size(); ++pose)
            if (needed[pose] && poses[pose].RigidCameraSpace)
                for (auto& bone : sampled[pose]) bone = MultiplyAffine(camera, bone);
    }
    return sampled;
}

// The bone rows of every draw whose model has a pose in both frames, from
// that pose's in-between skeleton. uniformsOf(index) is the draw's sampled
// uniforms (created when the sampler had none).
template <typename UniformsOf>
std::vector<std::vector<Affine34>> ApplySampledPoses(const Oot3dPicaVisualFrame& previous,
                                                     const Oot3dPicaVisualFrame& current,
                                                     const VisualDrawMatching& matching, float alpha,
                                                     UniformsOf uniformsOf) {
    const auto& poses = matching.Poses;
    if (poses.empty()) return {};
    // The skeletons some draw shows, and their carriers.
    std::vector<bool> needed(poses.size(), false);
    for (size_t index = 0; index < current.Draws.size() && index < matching.PoseByCurrent.size(); ++index) {
        const uint32_t pose = matching.PoseByCurrent[index];
        if (pose < poses.size() && poses[pose].Moving) needed[pose] = true;
    }
    for (const auto& part : matching.PoseVanished)
        if (alpha < 0.5F && poses[part.Pose].Moving) needed[part.Pose] = true;
    auto sampled = SampleModelPoses(matching, alpha, needed);
    for (size_t index = 0; index < current.Draws.size() && index < matching.PoseByCurrent.size(); ++index) {
        const uint32_t pose = matching.PoseByCurrent[index];
        if (pose >= poses.size() || !poses[pose].Moving) continue;
        const auto& skinning = *current.Draws[index].Skinning;
        auto& uniforms = uniformsOf(index);
        for (size_t entry = 0; entry < skinning.Bones.size(); ++entry) {
            const auto m = SkinnedPaletteMatrix(skinning, entry, sampled[pose][static_cast<size_t>(skinning.Bones[entry])]);
            for (size_t r = 0; r < 3U; ++r) uniforms.Floats[20U + 3U * entry + r] = m[r];
        }
        // TRIAEVUM_POSE_CHECK: at alpha 0 a paired draw must show its previous rows.
        static const bool check = std::getenv("TRIAEVUM_POSE_CHECK") != nullptr;
        const auto& match = matching.PreviousByCurrent[index];
        if (check && alpha == 0.0F && match.has_value() && !matching.PoseAppeared[index]) {
            const auto& before = previous.Draws[match->PreviousIndex];
            for (size_t entry = 0; entry < skinning.Bones.size(); ++entry) {
                float worst = 0.0F;
                for (size_t r = 0; r < 3U; ++r)
                    for (size_t c = 0; c < 4U; ++c)
                        worst = std::max(worst, std::abs(uniforms.Floats[20U + 3U * entry + r][c] -
                                                         before.VertexShader.Uniforms.Floats[20U + 3U * entry + r][c]));
                if (worst > 0.05F) {
                    const auto* beforeSkinning = before.Skinning.get();
                    std::fprintf(stderr,
                                 "TRIAEVUM_POSE_CHECK seq=%llu draw=%zu prev=%zu kind=%d model=%08x verts=%u/%u entry=%zu bone=%d prevbone=%d snap=%d carried=%d diff=%.2f\n",
                                 static_cast<unsigned long long>(current.Sequence), index, match->PreviousIndex,
                                 static_cast<int>(match->Kind), current.Draws[index].Composition.NativeValue,
                                 current.Draws[index].VertexCount, before.VertexCount, entry, skinning.Bones[entry],
                                 beforeSkinning != nullptr && entry < beforeSkinning->Bones.size() ? beforeSkinning->Bones[entry] : -9,
                                 poses[pose].Snap ? 1 : 0, poses[pose].Carrier < poses.size() ? 1 : 0, worst);
                }
            }
        }
    }
    return sampled;
}

bool ApplyCameraDelta(const VisualDrawMatching& matching, float alpha,
                      Oot3dPicaVertexUniformState& uniforms) {
    if (!CameraInterpolationEnabled() || !matching.CameraDeltaValid || !UsesModelViewLayout(uniforms)) return false;
    const float weight = 1.0F - std::clamp(alpha, 0.0F, 1.0F);
    if (!matching.RigidCamera) {
        // Same blend the paired draws get (previous = D * current).
        const auto currentUniforms = uniforms;
        auto previousUniforms = uniforms;
        for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
            const auto current = AffineAt(currentUniforms, first);
            const auto previous = MultiplyAffine(matching.CameraDelta, current);
            for (size_t row = 0; row < 3U; ++row) {
                previousUniforms.Floats[first + row] = previous[row];
                for (size_t column = 0; column < 4U; ++column)
                    uniforms.Floats[first + row][column] =
                        current[row][column] + weight * (previous[row][column] - current[row][column]);
            }
        }
        InterpolateRotatedMatrices(previousUniforms, currentUniforms, 1.0F - weight, uniforms);
        return true;
    }
    const auto camera = PartialCameraDelta(matching.CameraDelta, weight);
    for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
        const auto moved = MultiplyAffine(camera, AffineAt(uniforms, first));
        for (size_t row = 0; row < 3U; ++row) uniforms.Floats[first + row] = moved[row];
    }
    return true;
}

// A paired world draw: its own motion blended linearly in the current view
// (small), the camera interpolated as above.
void ApplyRigidCameraInterpolation(const VisualDrawMatching& matching, const Oot3dPicaVertexUniformState& previous,
                                   const Oot3dPicaVertexUniformState& current, float alpha,
                                   Oot3dPicaVertexUniformState& output, const ModelMotion* motion = nullptr) {
    if (!RigidCameraApplies(matching, previous, current)) return;
    const auto inverse = InvertAffine(matching.CameraDelta);
    if (!inverse) return;
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    // The previous pose seen from the current view, blended with the current
    // one (its own motion; rotations as rotations), then the camera.
    auto before = previous;
    auto blended = current;
    for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
        const auto moved = MultiplyAffine(*inverse, AffineAt(previous, first));
        for (size_t row = 0; row < 3U; ++row) {
            before.Floats[first + row] = moved[row];
            for (size_t column = 0; column < 4U; ++column)
                blended.Floats[first + row][column] =
                    InterpolateFinite(moved[row][column], current.Floats[first + row][column], alpha);
        }
    }
    if (motion != nullptr && motion->RigidCameraSpace)
        BlendWithModelMotion(before, current, alpha, *motion, blended, previous);
    else
        InterpolateRotatedMatrices(before, current, alpha, blended);
    const auto camera = PartialCameraDelta(matching.CameraDelta, 1.0F - alpha);
    for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
        const auto moved = MultiplyAffine(camera, AffineAt(blended, first));
        for (size_t row = 0; row < 3U; ++row) output.Floats[first + row] = moved[row];
    }
}

void InterpolateVisualVertexUniforms(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaVertexUniformState& vertexUniforms, const ModelMotion* motion = nullptr) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    for (size_t index = 0; index < vertexUniforms.Floats.size(); ++index) {
        if (!IsContinuousVertexUniform(index)) {
            continue;
        }
        for (size_t component = 0; component < 4U; ++component) {
            vertexUniforms.Floats[index][component] =
                InterpolateFinite(previous.VertexShader.Uniforms.Floats[index][component],
                                  current.VertexShader.Uniforms.Floats[index][component], alpha);
        }
    }
    if (UsesModelViewLayout(previous.VertexShader.Uniforms) &&
        UsesModelViewLayout(current.VertexShader.Uniforms)) {
        if (motion != nullptr && !motion->RigidCameraSpace)
            BlendWithModelMotion(previous.VertexShader.Uniforms, current.VertexShader.Uniforms, alpha, *motion,
                                 vertexUniforms, previous.VertexShader.Uniforms);
        else
            InterpolateRotatedMatrices(previous.VertexShader.Uniforms, current.VertexShader.Uniforms, alpha,
                                       vertexUniforms);
    }
}

void InterpolateVisualFragmentUniforms(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaFragmentUniformState& fragmentUniforms) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    for (size_t slot = 0; slot < fragmentUniforms.TevConstants.size(); ++slot) {
        for (size_t component = 0; component < 4U; ++component) {
            fragmentUniforms.TevConstants[slot][component] =
                InterpolateFinite(previous.FragmentShader.Uniforms.TevConstants[slot][component],
                                  current.FragmentShader.Uniforms.TevConstants[slot][component], alpha);
        }
    }
    for (size_t component = 0; component < 4U; ++component) {
        fragmentUniforms.CombinerBufferColor[component] =
            InterpolateFinite(previous.FragmentShader.Uniforms.CombinerBufferColor[component],
                              current.FragmentShader.Uniforms.CombinerBufferColor[component], alpha);
        fragmentUniforms.FogColor[component] =
            InterpolateFinite(previous.FragmentShader.Uniforms.FogColor[component],
                              current.FragmentShader.Uniforms.FogColor[component], alpha);
    }
    for (size_t entry = 0; entry < fragmentUniforms.FogLut.size(); ++entry) {
        for (size_t component = 0; component < 2U; ++component) {
            fragmentUniforms.FogLut[entry][component] =
                InterpolateFinite(previous.FragmentShader.Uniforms.FogLut[entry][component],
                                  current.FragmentShader.Uniforms.FogLut[entry][component], alpha);
        }
    }
    for (size_t component = 0;
         component < fragmentUniforms.TextureLodBias.size(); ++component) {
        fragmentUniforms.TextureLodBias[component] = InterpolateFinite(
            previous.FragmentShader.Uniforms.TextureLodBias[component],
            current.FragmentShader.Uniforms.TextureLodBias[component], alpha);
    }
    const auto interpolateVectors = [&](auto& output,
                                        const auto& previousValues,
                                        const auto& currentValues) {
        for (size_t vector = 0; vector < output.size(); ++vector) {
            for (size_t component = 0; component < output[vector].size();
                 ++component) {
                output[vector][component] = InterpolateFinite(
                    previousValues[vector][component],
                    currentValues[vector][component], alpha);
            }
        }
    };
    interpolateVectors(fragmentUniforms.Lighting.Specular0,
                       previous.FragmentShader.Uniforms.Lighting.Specular0,
                       current.FragmentShader.Uniforms.Lighting.Specular0);
    interpolateVectors(fragmentUniforms.Lighting.Specular1,
                       previous.FragmentShader.Uniforms.Lighting.Specular1,
                       current.FragmentShader.Uniforms.Lighting.Specular1);
    interpolateVectors(fragmentUniforms.Lighting.Diffuse,
                       previous.FragmentShader.Uniforms.Lighting.Diffuse,
                       current.FragmentShader.Uniforms.Lighting.Diffuse);
    interpolateVectors(fragmentUniforms.Lighting.Ambient,
                       previous.FragmentShader.Uniforms.Lighting.Ambient,
                       current.FragmentShader.Uniforms.Lighting.Ambient);
    interpolateVectors(fragmentUniforms.Lighting.Position,
                       previous.FragmentShader.Uniforms.Lighting.Position,
                       current.FragmentShader.Uniforms.Lighting.Position);
    interpolateVectors(
        fragmentUniforms.Lighting.SpotDirection,
        previous.FragmentShader.Uniforms.Lighting.SpotDirection,
        current.FragmentShader.Uniforms.Lighting.SpotDirection);
    interpolateVectors(fragmentUniforms.Lighting.Attenuation,
                       previous.FragmentShader.Uniforms.Lighting.Attenuation,
                       current.FragmentShader.Uniforms.Lighting.Attenuation);
    for (size_t component = 0;
         component < fragmentUniforms.Lighting.GlobalAmbient.size();
         ++component) {
        fragmentUniforms.Lighting.GlobalAmbient[component] = InterpolateFinite(
            previous.FragmentShader.Uniforms.Lighting.GlobalAmbient[component],
            current.FragmentShader.Uniforms.Lighting.GlobalAmbient[component],
            alpha);
    }
    fragmentUniforms.ShadowBiasConstant = InterpolateFinite(
        previous.FragmentShader.Uniforms.ShadowBiasConstant,
        current.FragmentShader.Uniforms.ShadowBiasConstant, alpha);
    fragmentUniforms.ShadowBiasLinear = InterpolateFinite(
        previous.FragmentShader.Uniforms.ShadowBiasLinear,
        current.FragmentShader.Uniforms.ShadowBiasLinear, alpha);
}

void InterpolateVisualViewport(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaViewportState& viewport) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    viewport.HalfWidth = InterpolateFinite(previous.State.Viewport.HalfWidth, current.State.Viewport.HalfWidth, alpha);
    viewport.HalfHeight =
        InterpolateFinite(previous.State.Viewport.HalfHeight, current.State.Viewport.HalfHeight, alpha);
    viewport.DepthRange =
        InterpolateFinite(previous.State.Viewport.DepthRange, current.State.Viewport.DepthRange, alpha);
    viewport.NearPlane = InterpolateFinite(previous.State.Viewport.NearPlane, current.State.Viewport.NearPlane, alpha);
}

void InterpolateVisualBlendConstantColor(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    std::array<float, 4>& blendConstantColor) {
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    for (size_t component = 0; component < 4U; ++component) {
        blendConstantColor[component] = InterpolateFinite(
            previous.State.OutputMerger.Blend.ConstantColor[component],
            current.State.OutputMerger.Blend.ConstantColor[component], alpha);
    }
}

void InterpolateCompatibleVisualRenderState(
    const Oot3dPicaVulkanDrawPlan& previous,
    const Oot3dPicaVulkanDrawPlan& current, float alpha,
    Oot3dPicaVertexUniformState& vertexUniforms,
    Oot3dPicaFragmentUniformState& fragmentUniforms,
    Oot3dPicaViewportState& viewport,
    std::array<float, 4>& blendConstantColor, const ModelMotion* motion = nullptr) {
    InterpolateVisualVertexUniforms(previous, current, alpha,
                                    vertexUniforms, motion);
    InterpolateVisualFragmentUniforms(previous, current, alpha,
                                      fragmentUniforms);
    InterpolateVisualViewport(previous, current, alpha, viewport);
    InterpolateVisualBlendConstantColor(previous, current, alpha,
                                        blendConstantColor);
}

void InterpolateCompatibleVisualDraw(const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanDrawPlan& current,
                                     float alpha, Oot3dPicaVulkanDrawPlan& output,
                                     const ModelMotion* motion = nullptr) {
    output = current;
    InterpolateCompatibleVisualRenderState(previous, current, alpha, output.VertexShader.Uniforms,
                                           output.FragmentShader.Uniforms, output.State.Viewport,
                                           output.State.OutputMerger.Blend.ConstantColor, motion);
    const bool perInstanceInterpolated = InterpolateVertexValues(
        previous, current, alpha, Oot3dPicaVertexInputRate::PerInstance,
        output.VertexBindings);
    bool perVertexInterpolated = false;
    if (previous.BaseVertex == current.BaseVertex &&
        EqualIndexBytes(previous, current)) {
      perVertexInterpolated = InterpolateVertexValues(
          previous, current, alpha, Oot3dPicaVertexInputRate::PerVertex,
          output.VertexBindings);
    }
    if (perInstanceInterpolated || perVertexInterpolated) {
      output.GeometryIdentityAvailable = false;
    }
}

// A model switches meshes from one frame to the next (Link's open hand and
// fist): the new mesh has no previous draw and showed its final pose in
// between, a hand hanging in the air off the turning arm. Its bones are
// the same as the ones its paired neighbours load: those rows take the
// neighbours' in-between pose. outputOf(index) is a paired draw's sampled
// uniforms (nullptr: unchanged).
template <typename OutputOf>
void AttachUnpairedModelDraws(const Oot3dPicaVisualFrame& current, const VisualDrawMatching& matching,
                              OutputOf outputOf,
                              const std::function<Oot3dPicaVertexUniformState&(size_t)>& unpairedUniforms) {
    static const bool disabled = std::getenv("TRIAEVUM_NO_UNPAIRED_ATTACH") != nullptr;
    if (disabled) return;
    const auto rowHash = [](uint32_t model, const Oot3dPicaVertexUniformState& u, size_t first) {
        uint64_t hash = kFnvOffset;
        HashValue(hash, model);
        for (size_t row = 0; row < 3U; ++row) HashValue(hash, u.Floats[first + row]);
        return hash;
    };
    std::vector<size_t> unpaired;
    for (size_t index = 0; index < current.Draws.size(); ++index)
        if (!matching.PreviousByCurrent[index].has_value() &&
            current.Draws[index].Composition.Provenance == Oot3dPicaCompositionProvenance::NativeModelInstance &&
            UsesModelViewLayout(current.Draws[index].VertexShader.Uniforms))
            unpaired.push_back(index);
    if (unpaired.empty()) return;
    std::unordered_map<uint32_t, bool> wanted;
    for (const size_t index : unpaired) wanted[current.Draws[index].Composition.NativeValue] = true;
    std::unordered_map<uint64_t, std::pair<size_t, size_t>> sources;  // row hash -> (draw, first row)
    for (size_t index = 0; index < current.Draws.size(); ++index) {
        const auto& draw = current.Draws[index];
        if (!matching.PreviousByCurrent[index].has_value() ||
            matching.PreviousByCurrent[index]->Kind != VisualDrawMatchKind::ModelInstance ||
            !wanted.contains(draw.Composition.NativeValue) || outputOf(index) == nullptr)
            continue;
        for (size_t first = 20U; first + 2U < 0x59U; first += 3U)
            sources.emplace(rowHash(draw.Composition.NativeValue, draw.VertexShader.Uniforms, first),
                            std::make_pair(index, first));
    }
    if (sources.empty()) return;
    for (const size_t index : unpaired) {
        const auto& draw = current.Draws[index];
        Oot3dPicaVertexUniformState* target = nullptr;
        for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
            const auto found = sources.find(rowHash(draw.Composition.NativeValue, draw.VertexShader.Uniforms, first));
            if (found == sources.end()) continue;
            const auto& [sourceIndex, sourceFirst] = found->second;
            const auto& source = current.Draws[sourceIndex].VertexShader.Uniforms;
            bool same = true;
            for (size_t row = 0; row < 3U; ++row)
                same = same && source.Floats[sourceFirst + row] == draw.VertexShader.Uniforms.Floats[first + row];
            if (!same) continue;
            if (target == nullptr) target = &unpairedUniforms(index);
            const auto* sampled = outputOf(sourceIndex);
            for (size_t row = 0; row < 3U; ++row) target->Floats[first + row] = sampled->Floats[sourceFirst + row];
        }
    }
}

// A world draw with no previous draw (MM3D's field grass: one batch whose
// vertex count changes as tufts come into range, unpaired on 40% of the
// frames) was moved by the estimated camera change, an average that put it
// 20-30 milliradians off the ground in between frames: the grass flickered
// as it loaded. Its matrix is the same view matrix the paired scenery around
// it uses (the field's parts): rows equal to a paired draw's take that
// draw's in-between rows, so it moves exactly with them. Model instances
// keep AttachUnpairedModelDraws (same model only).
template <typename OutputOf>
void AttachUnpairedWorldDraws(const Oot3dPicaVisualFrame& current, const VisualDrawMatching& matching,
                              OutputOf outputOf,
                              const std::function<Oot3dPicaVertexUniformState&(size_t)>& unpairedUniforms) {
    static const bool disabled = std::getenv("TRIAEVUM_NO_UNPAIRED_WORLD_ATTACH") != nullptr;
    if (disabled) return;
    const auto rowHash = [](const Oot3dPicaVertexUniformState& u, size_t first) {
        uint64_t hash = kFnvOffset;
        for (size_t row = 0; row < 3U; ++row) HashValue(hash, u.Floats[first + row]);
        return hash;
    };
    std::vector<size_t> unpaired;
    for (size_t index = 0; index < current.Draws.size(); ++index)
        if (!matching.PreviousByCurrent[index].has_value() &&
            current.Draws[index].Composition.Provenance != Oot3dPicaCompositionProvenance::NativeModelInstance &&
            UsesModelViewLayout(current.Draws[index].VertexShader.Uniforms))
            unpaired.push_back(index);
    if (unpaired.empty()) return;
    std::unordered_map<uint64_t, std::pair<size_t, size_t>> sources;  // row hash -> (draw, first row)
    for (size_t index = 0; index < current.Draws.size(); ++index) {
        if (!matching.PreviousByCurrent[index].has_value() || outputOf(index) == nullptr ||
            !UsesModelViewLayout(current.Draws[index].VertexShader.Uniforms))
            continue;
        for (size_t first = 20U; first + 2U < 0x59U; first += 3U)
            sources.emplace(rowHash(current.Draws[index].VertexShader.Uniforms, first), std::make_pair(index, first));
    }
    if (sources.empty()) return;
    for (const size_t index : unpaired) {
        const auto& draw = current.Draws[index];
        for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
            const auto found = sources.find(rowHash(draw.VertexShader.Uniforms, first));
            if (found == sources.end()) continue;
            const auto& [sourceIndex, sourceFirst] = found->second;
            const auto& source = current.Draws[sourceIndex].VertexShader.Uniforms;
            bool same = true;
            for (size_t row = 0; row < 3U; ++row)
                same = same && source.Floats[sourceFirst + row] == draw.VertexShader.Uniforms.Floats[first + row];
            if (!same) continue;
            const auto* sampled = outputOf(sourceIndex);
            auto& target = unpairedUniforms(index);
            for (size_t row = 0; row < 3U; ++row) target.Floats[first + row] = sampled->Floats[sourceFirst + row];
        }
    }
}

bool SampleVisualFrameWithMatching(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                   float alpha, const VisualDrawMatching& matching, Oot3dPicaVisualFrameSample& output,
                                   const std::vector<VisualDrawDeltaFlags>* drawDeltas = nullptr) {
    output.TopTransfer = current.TopTransfer;
    output.MatchedDraws = matching.StrictUniqueMatches + matching.StrictOrdinalMatches +
                          matching.StructuralOrdinalMatches + matching.PipelineOrdinalMatches;
    output.StrictUniqueMatchedDraws = matching.StrictUniqueMatches;
    output.StrictOrdinalMatchedDraws = matching.StrictOrdinalMatches;
    output.StructuralOrdinalMatchedDraws = matching.StructuralOrdinalMatches;
    output.PipelineOrdinalMatchedDraws = matching.PipelineOrdinalMatches;
    output.InterpolatedDraws = 0;
    output.InterpolatedPerInstanceVertexDraws = 0;
    output.InterpolatedPerVertexDraws = 0;
    output.Draws.resize(current.Draws.size());

    for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
        const auto& currentDraw = current.Draws[currentIndex];
        const auto match = matching.PreviousByCurrent[currentIndex];
        auto& sampled = output.Draws[currentIndex];
        if (match.has_value() && InterfaceDrawSnaps(currentDraw)) {
            sampled = currentDraw;
        } else if (match.has_value()) {
            const auto& previousDraw = previous.Draws[match->PreviousIndex];
            const auto* motion = matching.MotionFor(currentIndex);
            InterpolateCompatibleVisualDraw(previousDraw, currentDraw, alpha, sampled, motion);
            if (match->Kind == VisualDrawMatchKind::GeometryOrdinal)
                sampled.FragmentShader.Uniforms = currentDraw.FragmentShader.Uniforms;
            ApplyRigidCameraInterpolation(matching, previousDraw.VertexShader.Uniforms,
                                          currentDraw.VertexShader.Uniforms, alpha,
                                          sampled.VertexShader.Uniforms, motion);
            VisualDrawDeltaFlags delta;
            if (drawDeltas != nullptr) {
                delta = (*drawDeltas)[currentIndex];
            } else {
                delta.PerInstanceVertex =
                    InterpolatableVertexValuesDiffer(previousDraw, currentDraw, Oot3dPicaVertexInputRate::PerInstance);
                delta.PerVertex =
                    previousDraw.BaseVertex == currentDraw.BaseVertex &&
                    EqualIndexBytes(previousDraw, currentDraw) &&
                    InterpolatableVertexValuesDiffer(
                        previousDraw, currentDraw,
                        Oot3dPicaVertexInputRate::PerVertex);
                delta.VertexUniforms =
                    ContinuousVertexUniformValuesDiffer(previousDraw,
                                                        currentDraw);
                delta.FragmentUniforms =
                    ContinuousFragmentUniformValuesDiffer(previousDraw,
                                                          currentDraw);
                delta.Viewport = ContinuousViewportValuesDiffer(previousDraw,
                                                                currentDraw);
                delta.BlendConstantColor =
                    previousDraw.State.OutputMerger.Blend.ConstantColor !=
                    currentDraw.State.OutputMerger.Blend.ConstantColor;
                delta.Continuous =
                    delta.PerInstanceVertex || delta.PerVertex ||
                    delta.VertexUniforms || delta.FragmentUniforms ||
                    delta.Viewport || delta.BlendConstantColor;
            }
            if (delta.Continuous) {
                ++output.InterpolatedDraws;
            }
            if (delta.PerInstanceVertex) {
                ++output.InterpolatedPerInstanceVertexDraws;
            }
            if (delta.PerVertex) {
                ++output.InterpolatedPerVertexDraws;
            }
        } else {
            sampled = currentDraw;
            ApplyCameraDelta(matching, alpha, sampled.VertexShader.Uniforms);
        }
    }
    AttachUnpairedModelDraws(
        current, matching,
        [&](size_t index) -> const Oot3dPicaVertexUniformState* { return &output.Draws[index].VertexShader.Uniforms; },
        [&](size_t index) -> Oot3dPicaVertexUniformState& { return output.Draws[index].VertexShader.Uniforms; });
    AttachUnpairedWorldDraws(
        current, matching,
        [&](size_t index) -> const Oot3dPicaVertexUniformState* { return &output.Draws[index].VertexShader.Uniforms; },
        [&](size_t index) -> Oot3dPicaVertexUniformState& { return output.Draws[index].VertexShader.Uniforms; });
    const auto sampledPoses = ApplySampledPoses(
        previous, current, matching, alpha,
        [&](size_t index) -> Oot3dPicaVertexUniformState& { return output.Draws[index].VertexShader.Uniforms; });
    // A model's parts in one frame only (mesh switches): the nearer frame's.
    if (alpha < 0.5F && (!matching.PoseVanished.empty() ||
                         std::find(matching.PoseAppeared.begin(), matching.PoseAppeared.end(), true) !=
                             matching.PoseAppeared.end())) {
        std::vector<std::vector<size_t>> after(output.Draws.size());
        for (size_t k = 0; k < matching.PoseVanished.size(); ++k)
            if (matching.PoseVanished[k].After < after.size()) after[matching.PoseVanished[k].After].push_back(k);
        std::vector<Oot3dPicaVulkanDrawPlan> merged;
        merged.reserve(output.Draws.size() + matching.PoseVanished.size());
        for (size_t index = 0; index < output.Draws.size(); ++index) {
            if (index >= matching.PoseAppeared.size() || !matching.PoseAppeared[index])
                merged.push_back(std::move(output.Draws[index]));
            for (const size_t k : after[index]) {
                const auto& part = matching.PoseVanished[k];
                auto draw = previous.Draws[part.PreviousIndex];
                if (part.Pose < sampledPoses.size() && !sampledPoses[part.Pose].empty())
                    WriteSkinnedRows(*draw.Skinning, sampledPoses[part.Pose], draw.VertexShader.Uniforms);
                merged.push_back(std::move(draw));
            }
        }
        output.Draws = std::move(merged);
    }
    if (output.Draws.empty()) {
        return false;
    }

    output.MemoryFills = current.MemoryFills;
    output.DisplayTransfers = current.DisplayTransfers;
    return true;
}

bool SampleVisualFrameViewWithMatching(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                       float alpha, const VisualDrawMatching& matching,
                                       Oot3dPicaVisualFrameViewSample& output,
                                       const std::vector<VisualDrawDeltaFlags>& drawDeltas) {
    output.TopTransfer = current.TopTransfer;
    output.MemoryFills = &current.MemoryFills;
    output.DisplayTransfers = &current.DisplayTransfers;
    output.MatchedDraws = matching.StrictUniqueMatches + matching.StrictOrdinalMatches +
                          matching.StructuralOrdinalMatches + matching.PipelineOrdinalMatches;
    output.StrictUniqueMatchedDraws = matching.StrictUniqueMatches;
    output.StrictOrdinalMatchedDraws = matching.StrictOrdinalMatches;
    output.StructuralOrdinalMatchedDraws = matching.StructuralOrdinalMatches;
    output.PipelineOrdinalMatchedDraws = matching.PipelineOrdinalMatches;
    output.InterpolatedDraws = 0;
    output.InterpolatedPerInstanceVertexDraws = 0;
    output.InterpolatedPerVertexDraws = 0;
    output.ViewAdjustmentValid = false;
    if (CameraInterpolationEnabled() && matching.CameraDeltaValid) {
        // As ApplyCameraDelta moves the world draws: rigid partial rotation, or
        // the linear blend I + w (D - I).
        const float weight = 1.0F - std::clamp(alpha, 0.0F, 1.0F);
        Affine34 adjustment = PartialCameraDelta(matching.CameraDelta, weight);
        if (!matching.RigidCamera) {
            for (size_t row = 0; row < 3U; ++row)
                for (size_t column = 0; column < 4U; ++column) {
                    const float identity = row == column ? 1.0F : 0.0F;
                    adjustment[row][column] = identity + weight * (matching.CameraDelta[row][column] - identity);
                }
        }
        for (size_t row = 0; row < 3U; ++row)
            for (size_t column = 0; column < 4U; ++column)
                output.ViewAdjustment[row * 4U + column] = adjustment[row][column];
        output.ViewAdjustmentValid = true;
    }
    output.Draws.resize(current.Draws.size());

    for (size_t currentIndex = 0; currentIndex < current.Draws.size(); ++currentIndex) {
        const auto& currentDraw = current.Draws[currentIndex];
        auto& sampled = output.Draws[currentIndex];
        sampled.BasePlan = &currentDraw;
        sampled.VertexUniforms.reset();
        sampled.FragmentUniforms.reset();
        sampled.Viewport.reset();
        sampled.BlendConstantColor.reset();
        sampled.HasVertexBindingOverrides = false;

        const auto match = matching.PreviousByCurrent[currentIndex];
        if (!match.has_value()) {
            Oot3dPicaVertexUniformState uniforms = currentDraw.VertexShader.Uniforms;
            if (ApplyCameraDelta(matching, alpha, uniforms))
                sampled.VertexUniforms.emplace(std::move(uniforms));
            continue;
        }
        if (InterfaceDrawSnaps(currentDraw)) continue;  // as the current game frame has it
        const auto& previousDraw = previous.Draws[match->PreviousIndex];
        const auto& delta = drawDeltas[currentIndex];
        if (delta.VertexUniforms) {
            sampled.VertexUniforms.emplace(currentDraw.VertexShader.Uniforms);
            const auto* motion = matching.MotionFor(currentIndex);
            InterpolateVisualVertexUniforms(
                previousDraw, currentDraw, alpha, *sampled.VertexUniforms, motion);
            ApplyRigidCameraInterpolation(matching, previousDraw.VertexShader.Uniforms,
                                          currentDraw.VertexShader.Uniforms, alpha, *sampled.VertexUniforms,
                                          motion);
            // TRIAEVUM_BONE_TRACK=<vertex count>: every sample's bone places of that draw.
            static const long boneTrack = [] {
                const char* value = std::getenv("TRIAEVUM_BONE_TRACK");
                return value != nullptr ? std::strtol(value, nullptr, 10) : -1L;
            }();
            if (boneTrack >= 0 && currentDraw.VertexCount == static_cast<uint32_t>(boneTrack)) {
                const auto& u = sampled.VertexUniforms.has_value() ? *sampled.VertexUniforms : currentDraw.VertexShader.Uniforms;
                std::fprintf(stderr, "TRIAEVUM_BONES %llu %.3f", static_cast<unsigned long long>(current.Sequence), alpha);
                for (size_t first = 20U; first + 2U < 0x59U; first += 3U)
                    std::fprintf(stderr, " %.3f %.3f %.3f", u.Floats[first][3], u.Floats[first + 1][3], u.Floats[first + 2][3]);
                std::fprintf(stderr, "\n");
            }
        }
        // TRIAEVUM_INTERP_PROBE=<file>: for opaque world pairs, the sampled
        // distance from the camera against the endpoints' (1 = no pull).
        static FILE* probe = [] {
            const char* path = std::getenv("TRIAEVUM_INTERP_PROBE");
            return path != nullptr ? std::fopen(path, "w") : nullptr;
        }();
        if (probe != nullptr && sampled.VertexUniforms.has_value() && alpha > 0.3F && alpha < 0.7F &&
            match->Kind == VisualDrawMatchKind::StrictUnique &&
            currentDraw.Composition.Layer == Oot3dPicaCompositionLayer::OpaqueWorld &&
            UsesModelViewLayout(currentDraw.VertexShader.Uniforms)) {
            const auto length = [](const Oot3dPicaVertexUniformState& u) {
                return std::sqrt(u.Floats[20][3] * u.Floats[20][3] + u.Floats[21][3] * u.Floats[21][3] +
                                 u.Floats[22][3] * u.Floats[22][3]);
            };
            const float a = length(previousDraw.VertexShader.Uniforms), b = length(currentDraw.VertexShader.Uniforms);
            const float m = length(*sampled.VertexUniforms);
            const auto& d = matching.CameraDelta;
            const float angle = std::acos(std::clamp((d[0][0] + d[1][1] + d[2][2] - 1.0F) * 0.5F, -1.0F, 1.0F));
            if (a > 50.0F && b > 50.0F)
                std::fprintf(probe, "%.2f %.1f %d %.4f\n", alpha, angle * 57.2958F, matching.RigidCamera ? 1 : 0,
                             m / (a + (b - a) * alpha));
        }
        if (delta.FragmentUniforms) {
            sampled.FragmentUniforms.emplace(currentDraw.FragmentShader.Uniforms);
            InterpolateVisualFragmentUniforms(
                previousDraw, currentDraw, alpha, *sampled.FragmentUniforms);
        }
        if (delta.Viewport) {
            sampled.Viewport.emplace(currentDraw.State.Viewport);
            InterpolateVisualViewport(previousDraw, currentDraw, alpha,
                                      *sampled.Viewport);
        }
        if (delta.BlendConstantColor) {
            sampled.BlendConstantColor.emplace(currentDraw.State.OutputMerger.Blend.ConstantColor);
            InterpolateVisualBlendConstantColor(
                previousDraw, currentDraw, alpha,
                *sampled.BlendConstantColor);
        }
        if (delta.PerInstanceVertex || delta.PerVertex) {
            sampled.VertexBindings = currentDraw.VertexBindings;
            sampled.HasVertexBindingOverrides = true;
            if (delta.PerInstanceVertex) {
                InterpolateVertexValues(previousDraw, currentDraw, alpha, Oot3dPicaVertexInputRate::PerInstance,
                                        sampled.VertexBindings);
            }
            if (delta.PerVertex) {
                InterpolateVertexValues(previousDraw, currentDraw, alpha, Oot3dPicaVertexInputRate::PerVertex,
                                        sampled.VertexBindings);
            }
        }
        if (delta.Continuous) {
            ++output.InterpolatedDraws;
        }
        if (delta.PerInstanceVertex) {
            ++output.InterpolatedPerInstanceVertexDraws;
        }
        if (delta.PerVertex) {
            ++output.InterpolatedPerVertexDraws;
        }
    }
    AttachUnpairedModelDraws(
        current, matching,
        [&](size_t index) -> const Oot3dPicaVertexUniformState* {
            return output.Draws[index].VertexUniforms.has_value() ? &*output.Draws[index].VertexUniforms : nullptr;
        },
        [&](size_t index) -> Oot3dPicaVertexUniformState& {
            auto& sampled = output.Draws[index];
            if (!sampled.VertexUniforms.has_value()) sampled.VertexUniforms.emplace(current.Draws[index].VertexShader.Uniforms);
            return *sampled.VertexUniforms;
        });
    AttachUnpairedWorldDraws(
        current, matching,
        [&](size_t index) -> const Oot3dPicaVertexUniformState* {
            return output.Draws[index].VertexUniforms.has_value() ? &*output.Draws[index].VertexUniforms : nullptr;
        },
        [&](size_t index) -> Oot3dPicaVertexUniformState& {
            auto& sampled = output.Draws[index];
            if (!sampled.VertexUniforms.has_value()) sampled.VertexUniforms.emplace(current.Draws[index].VertexShader.Uniforms);
            return *sampled.VertexUniforms;
        });
    const auto sampledPoses =
        ApplySampledPoses(previous, current, matching, alpha, [&](size_t index) -> Oot3dPicaVertexUniformState& {
            auto& sampled = output.Draws[index];
            if (!sampled.VertexUniforms.has_value())
                sampled.VertexUniforms.emplace(current.Draws[index].VertexShader.Uniforms);
            return *sampled.VertexUniforms;
        });
    // TRIAEVUM_MID_AUDIT=<file>: in-between samples (alpha 0.3..0.7), per
    // world draw: path, then the origin (f20..22 column 3) previous, current,
    // sampled (session_tools/midaudit.py).
    static FILE* midAudit = [] {
        const char* path = std::getenv("TRIAEVUM_MID_AUDIT");
        return path != nullptr ? std::fopen(path, "w") : nullptr;
    }();
    if (midAudit != nullptr && alpha > 0.3F && alpha < 0.7F) {
        const auto& d = matching.CameraDelta;
        std::fprintf(midAudit, "M %llu %.3f %d %d %d", static_cast<unsigned long long>(current.Sequence), alpha,
                     matching.CameraDeltaValid ? 1 : 0, matching.RigidCamera ? 1 : 0, matching.CameraCut ? 1 : 0);
        for (size_t row = 0; row < 3U; ++row)
            for (size_t column = 0; column < 4U; ++column) std::fprintf(midAudit, " %.6g", d[row][column]);
        // steady clock ms: lines up with TRIAEVUM_WARP's (area of a sample)
        std::fprintf(midAudit, " %lld\n",
                     static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count()));
        for (size_t index = 0; index < current.Draws.size(); ++index) {
            const auto& draw = current.Draws[index];
            if (!UsesModelViewLayout(draw.VertexShader.Uniforms)) continue;
            const auto& match = matching.PreviousByCurrent[index];
            const auto& c = draw.VertexShader.Uniforms.Floats;
            const auto& s = output.Draws[index].VertexUniforms.has_value() ? output.Draws[index].VertexUniforms->Floats : c;
            const auto* p = match.has_value() ? &previous.Draws[match->PreviousIndex].VertexShader.Uniforms.Floats : &c;
            const bool posed = index < matching.PoseByCurrent.size() &&
                               matching.PoseByCurrent[index] != std::numeric_limits<uint32_t>::max();
            std::fprintf(midAudit, "P %zu %d %d %d %d %08x %u %08x %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", index,
                         match.has_value() ? static_cast<int>(match->Kind) : -1, matching.MotionFor(index) != nullptr ? 1 : 0,
                         posed ? 1 : 0, static_cast<int>(draw.Composition.Layer),
                         draw.Textures.empty() ? 0U : draw.Textures.front().State.PhysicalAddress, draw.VertexCount,
                         draw.Composition.NativeValue, (*p)[20][3], (*p)[21][3], (*p)[22][3], c[20][3], c[21][3], c[22][3],
                         s[20][3], s[21][3], s[22][3]);
        }
    }
    // TRIAEVUM_MID_MATRICES=<file>: every sample, per world draw the first
    // matrix (f20..22) previous, current and shown (ground_jitter tools).
    static FILE* midMatrices = [] {
        const char* path = std::getenv("TRIAEVUM_MID_MATRICES");
        return path != nullptr ? std::fopen(path, "w") : nullptr;
    }();
    if (midMatrices != nullptr) {
        const auto& d = matching.CameraDelta;
        std::fprintf(midMatrices, "S %llu %.4f %d %d", static_cast<unsigned long long>(current.Sequence), alpha,
                     matching.CameraDeltaValid ? 1 : 0, matching.RigidCamera ? 1 : 0);
        for (size_t row = 0; row < 3U; ++row)
            for (size_t column = 0; column < 4U; ++column) std::fprintf(midMatrices, " %.7g", d[row][column]);
        std::fprintf(midMatrices, " %lld\n",
                     static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count()));
        for (size_t index = 0; index < current.Draws.size(); ++index) {
            const auto& draw = current.Draws[index];
            if (!UsesModelViewLayout(draw.VertexShader.Uniforms)) continue;
            const auto& match = matching.PreviousByCurrent[index];
            const auto& c = draw.VertexShader.Uniforms.Floats;
            const auto& s = output.Draws[index].VertexUniforms.has_value() ? output.Draws[index].VertexUniforms->Floats : c;
            const auto* p = match.has_value() ? &previous.Draws[match->PreviousIndex].VertexShader.Uniforms.Floats : &c;
            const bool posed = index < matching.PoseByCurrent.size() &&
                               matching.PoseByCurrent[index] != std::numeric_limits<uint32_t>::max();
            std::fprintf(midMatrices, "Q %zu %d %d %d %d %08x %u %u %08x", index,
                         match.has_value() ? static_cast<int>(match->Kind) : -1, posed ? 1 : 0,
                         draw.Skinning != nullptr ? 1 : 0, matching.MotionFor(index) != nullptr ? 1 : 0,
                         draw.Composition.NativeValue, draw.Composition.Ordinal, draw.VertexCount,
                         draw.Textures.empty() ? 0U : draw.Textures.front().State.PhysicalAddress);
            for (const auto* m : {p, &c, &s})
                for (size_t row = 20U; row < 23U; ++row)
                    for (size_t column = 0; column < 4U; ++column) std::fprintf(midMatrices, " %.7g", (*m)[row][column]);
            std::fprintf(midMatrices, "\n");
        }
    }
    // Draws added after a current draw, and current draws left out.
    std::vector<std::vector<Oot3dPicaVisualDrawView>> inserted;
    std::vector<bool> hidden;
    size_t insertedCount = 0, hiddenCount = 0;
    // A model's parts in one frame only (mesh switches): the nearer frame's.
    if (alpha < 0.5F) {
        for (size_t index = 0; index < matching.PoseAppeared.size(); ++index) {
            if (!matching.PoseAppeared[index]) continue;
            if (hidden.empty()) hidden.assign(current.Draws.size(), false);
            hidden[index] = true;
            ++hiddenCount;
        }
        for (const auto& part : matching.PoseVanished) {
            const auto& draw = previous.Draws[part.PreviousIndex];
            Oot3dPicaVisualDrawView view;
            view.BasePlan = &draw;
            auto uniforms = draw.VertexShader.Uniforms;
            if (part.Pose < sampledPoses.size() && !sampledPoses[part.Pose].empty())
                WriteSkinnedRows(*draw.Skinning, sampledPoses[part.Pose], uniforms);
            view.VertexUniforms.emplace(std::move(uniforms));
            if (inserted.empty()) inserted.resize(current.Draws.size());
            inserted[part.After].push_back(std::move(view));
            ++insertedCount;
        }
    }
    // Opaque world draws the game culled as they left the view: in the previous
    // frame, gone from the current one, and their place is outside the
    // current view (the game drops what goes off screen, the in-between
    // views still see part of it). Turning fast, a few to 30 such draws per
    // frame vanished early at the screen edges. Drawn where the in-between
    // camera sees them, next to their neighbour in the frame. Draws gone
    // while still in view (destroyed, a model switched) stay gone.
    static const bool keepCulled = std::getenv("TRIAEVUM_NO_CULLED_KEEP") == nullptr;
    const auto& cameraDelta = matching.CameraDelta;
    const float cameraTurn =
        std::acos(std::clamp((cameraDelta[0][0] + cameraDelta[1][1] + cameraDelta[2][2] - 1.0F) * 0.5F, -1.0F, 1.0F));
    const float cameraMove = std::sqrt(cameraDelta[0][3] * cameraDelta[0][3] + cameraDelta[1][3] * cameraDelta[1][3] +
                                       cameraDelta[2][3] * cameraDelta[2][3]);
    if (keepCulled && matching.CameraDeltaValid && !matching.CameraCut && alpha < 1.0F &&
        (cameraTurn > 0.5F * 0.0174533F || cameraMove > 2.0F)) {
        const auto inverse = InvertAffine(matching.CameraDelta);
        if (inverse) {
            // Current world draws by geometry, with their place: a previous draw
            // whose geometry is drawn nearby now was re-identified, not culled.
            std::unordered_map<uint64_t, std::vector<std::array<float, 3>>> currentPlaces;
            for (const auto& draw : current.Draws)
                if (UsesModelViewLayout(draw.VertexShader.Uniforms))
                    currentPlaces[ComputeGeometryDrawIdentity(draw)].push_back(
                        {draw.VertexShader.Uniforms.Floats[20][3], draw.VertexShader.Uniforms.Floats[21][3],
                         draw.VertexShader.Uniforms.Floats[22][3]});
            constexpr size_t kNone = std::numeric_limits<size_t>::max();
            std::vector<size_t> currentOfPrevious(previous.Draws.size(), kNone);
            for (size_t currentIndex = 0; currentIndex < matching.PreviousByCurrent.size(); ++currentIndex)
                if (const auto& match = matching.PreviousByCurrent[currentIndex])
                    currentOfPrevious[match->PreviousIndex] = currentIndex;
            for (size_t previousIndex = 0; previousIndex < previous.Draws.size(); ++previousIndex) {
                if (currentOfPrevious[previousIndex] != kNone) continue;
                // A paired model's parts: its skeleton decides (above).
                if (previousIndex < matching.PreviousInPose.size() && matching.PreviousInPose[previousIndex]) continue;
                const auto& draw = previous.Draws[previousIndex];
                const auto layer = draw.Composition.Layer;
                // Opaque only: transparent ones are mostly effects whose
                // particles really ended (their place is never in view).
                if (layer != Oot3dPicaCompositionLayer::OpaqueWorld || !UsesModelViewLayout(draw.VertexShader.Uniforms))
                    continue;
                // Its place seen from the current view, against the current view's volume.
                const auto& f = draw.VertexShader.Uniforms.Floats;
                const auto place = MultiplyAffine(*inverse, AffineAt(draw.VertexShader.Uniforms, 20U));
                const std::array<float, 4> point{place[0][3], place[1][3], place[2][3], 1.0F};
                const auto clip = [&](size_t row) {
                    return f[row][0] * point[0] + f[row][1] * point[1] + f[row][2] * point[2] + f[row][3] * point[3];
                };
                const float x = clip(0), y = clip(1), w = clip(3);
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) ||
                    (w > 0.0F && std::abs(x) <= w && std::abs(y) <= w))
                    continue;  // in view (or unknown): really gone
                if (!(w > 0.0F) || std::max(std::abs(x), std::abs(y)) > 4.0F * w)
                    continue;  // behind or far off to the side: not seen in between either
                if (const auto found = currentPlaces.find(ComputeGeometryDrawIdentity(draw));
                    found != currentPlaces.end() &&
                    std::any_of(found->second.begin(), found->second.end(), [&](const std::array<float, 3>& at) {
                        const float dx = at[0] - point[0], dy = at[1] - point[1], dz = at[2] - point[2];
                        return dx * dx + dy * dy + dz * dz < 50.0F * 50.0F;
                    }))
                    continue;  // drawn now under another identity
                // Next to the draw before it in the previous frame, in the same target.
                size_t neighbour = kNone;
                for (size_t before = previousIndex; before-- > 0U;)
                    if (currentOfPrevious[before] != kNone) { neighbour = currentOfPrevious[before]; break; }
                if (neighbour == kNone) continue;
                const auto& target = current.Draws[neighbour].State.Framebuffer;
                if (target.ColorPhysicalAddress != draw.State.Framebuffer.ColorPhysicalAddress ||
                    target.DepthPhysicalAddress != draw.State.Framebuffer.DepthPhysicalAddress)
                    continue;
                Oot3dPicaVisualDrawView view;
                view.BasePlan = &draw;
                auto uniforms = draw.VertexShader.Uniforms;
                for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
                    const auto moved = MultiplyAffine(*inverse, AffineAt(draw.VertexShader.Uniforms, first));
                    for (size_t row = 0; row < 3U; ++row) uniforms.Floats[first + row] = moved[row];
                }
                ApplyCameraDelta(matching, alpha, uniforms);
                view.VertexUniforms.emplace(std::move(uniforms));
                if (inserted.empty()) inserted.resize(current.Draws.size());
                inserted[neighbour].push_back(std::move(view));
                ++insertedCount;
                static const bool keptTrace = std::getenv("TRIAEVUM_CULLED_TRACE") != nullptr;
                if (keptTrace)
                    std::fprintf(stderr, "TRIAEVUM_CULLED_DRAW tex=%08x n=%u layer=%d fb=%08x at=%.0f,%.0f,%.0f clip=%.2f,%.2f\n",
                                 draw.Textures.empty() ? 0U : draw.Textures.front().State.PhysicalAddress, draw.VertexCount,
                                 static_cast<int>(layer), draw.State.Framebuffer.ColorPhysicalAddress, point[0], point[1],
                                 point[2], w != 0.0F ? x / w : 0.0F, w != 0.0F ? y / w : 0.0F);
            }
            static const bool trace = std::getenv("TRIAEVUM_JUMP_TRACE") != nullptr;
            if (trace && insertedCount != 0U)
                std::fprintf(stderr, "TRIAEVUM_CULLED_KEPT %zu alpha=%.2f turn=%.1f move=%.1f\n", insertedCount, alpha,
                             cameraTurn * 57.2958F, cameraMove);
        }
    }
    if (insertedCount != 0U || hiddenCount != 0U) {
        const size_t count = current.Draws.size();
        std::vector<Oot3dPicaVisualDrawView> merged;
        merged.reserve(output.Draws.size() + insertedCount);
        for (size_t index = 0; index < output.Draws.size(); ++index) {
            if (index >= count || hidden.empty() || !hidden[index]) merged.push_back(std::move(output.Draws[index]));
            if (index < count && !inserted.empty())
                for (auto& extra : inserted[index]) merged.push_back(std::move(extra));
        }
        output.Draws = std::move(merged);
    }
    return !output.Draws.empty();
}

} // namespace

struct Oot3dPicaPreparedVisualTransition::Impl {
    VisualDrawMatching Matching;
    std::vector<VisualDrawDeltaFlags> DrawDeltas;
    Oot3dPicaVisualTransitionStats Transition;
    uint64_t PreviousSequence = 0U;
    uint64_t CurrentSequence = 0U;
    size_t PreviousDrawCount = 0U;
    size_t CurrentDrawCount = 0U;
    bool Prepared = false;
};

Oot3dPicaPreparedVisualTransition::Oot3dPicaPreparedVisualTransition()
    : mImpl(std::make_unique<Impl>()) {
}

Oot3dPicaPreparedVisualTransition::~Oot3dPicaPreparedVisualTransition() =
    default;

Oot3dPicaPreparedVisualTransition::Oot3dPicaPreparedVisualTransition(
    Oot3dPicaPreparedVisualTransition&&) noexcept = default;

Oot3dPicaPreparedVisualTransition&
Oot3dPicaPreparedVisualTransition::operator=(
    Oot3dPicaPreparedVisualTransition&&) noexcept = default;

bool Oot3dPicaPreparedVisualTransition::Prepare(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current,
    Oot3dPicaVisualInterpolationTiming* timing) {
    if (mImpl == nullptr) {
        mImpl = std::make_unique<Impl>();
    }
    Reset();

    const auto matchingStart = std::chrono::steady_clock::now();
    mImpl->Matching = BuildVisualDrawMatching(previous, current);
    const auto analysisStart = std::chrono::steady_clock::now();
    mImpl->Transition = AnalyzeVisualTransitionWithMatching(
        previous, current, mImpl->Matching, &mImpl->DrawDeltas);
    const auto finished = std::chrono::steady_clock::now();
    if (timing != nullptr) {
        timing->MatchingNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                analysisStart - matchingStart)
                .count());
        timing->AnalysisNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finished - analysisStart)
                .count());
    }

    mImpl->PreviousSequence = previous.Sequence;
    mImpl->CurrentSequence = current.Sequence;
    mImpl->PreviousDrawCount = previous.Draws.size();
    mImpl->CurrentDrawCount = current.Draws.size();
    mImpl->Prepared = !current.Draws.empty();
    return mImpl->Prepared;
}

bool Oot3dPicaPreparedVisualTransition::Sample(
    const Oot3dPicaVisualFrame& previous,
    const Oot3dPicaVisualFrame& current, float alpha,
    Oot3dPicaVisualFrameViewSample& output,
    Oot3dPicaVisualInterpolationTiming* timing) const {
    if (!Ready() || previous.Sequence != mImpl->PreviousSequence ||
        current.Sequence != mImpl->CurrentSequence ||
        previous.Draws.size() != mImpl->PreviousDrawCount ||
        current.Draws.size() != mImpl->CurrentDrawCount) {
        return false;
    }

    const auto samplingStart = std::chrono::steady_clock::now();
    const bool sampled = SampleVisualFrameViewWithMatching(
        previous, current, alpha, mImpl->Matching, output,
        mImpl->DrawDeltas);
    if (timing != nullptr) {
        const auto finished = std::chrono::steady_clock::now();
        timing->SamplingNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finished - samplingStart)
                .count());
    }
    return sampled;
}

void Oot3dPicaPreparedVisualTransition::Reset() noexcept {
    if (mImpl == nullptr) {
        return;
    }
    mImpl->Matching = {};
    mImpl->DrawDeltas.clear();
    mImpl->Transition = {};
    mImpl->PreviousSequence = 0U;
    mImpl->CurrentSequence = 0U;
    mImpl->PreviousDrawCount = 0U;
    mImpl->CurrentDrawCount = 0U;
    mImpl->Prepared = false;
}

bool Oot3dPicaPreparedVisualTransition::Ready() const noexcept {
    return mImpl != nullptr && mImpl->Prepared;
}

const Oot3dPicaVisualTransitionStats&
Oot3dPicaPreparedVisualTransition::Stats() const noexcept {
    static const Oot3dPicaVisualTransitionStats unavailable;
    return mImpl != nullptr ? mImpl->Transition : unavailable;
}

Oot3dPicaVulkanDrawOverrides Oot3dPicaVisualDrawView::Overrides() const {
    Oot3dPicaVulkanDrawOverrides overrides;
    overrides.VertexUniforms = VertexUniforms.has_value() ? &*VertexUniforms : nullptr;
    overrides.FragmentUniforms = FragmentUniforms.has_value() ? &*FragmentUniforms : nullptr;
    overrides.Viewport = Viewport.has_value() ? &*Viewport : nullptr;
    overrides.BlendConstantColor = BlendConstantColor.has_value() ? &*BlendConstantColor : nullptr;
    overrides.VertexBindings = HasVertexBindingOverrides ? &VertexBindings : nullptr;
    return overrides;
}

uint64_t
ComputeOot3dPicaVisualDrawIdentity(const Oot3dPicaVulkanDrawPlan& plan) {
    uint64_t hash = kFnvOffset;
    // (Not the composition domain and attribution: it can drop to Unknown for a frame
    // when the native tracker misses its spans, and every draw then lost its
    // pairing - a frame shown without in-betweens. CompatibleComposition.)
    HashValue(hash, plan.VertexShader.StateKey);
    HashValue(hash, plan.FragmentShader.StateKey);
    HashValue(hash, plan.Indexed);
    HashValue(hash, plan.IndicesAre16Bit);
    HashValue(hash, plan.VertexCount);
    HashValue(hash, plan.State.VertexInput.PhysicalBaseAddress);
    HashValue(hash, plan.State.VertexInput.IndexPhysicalAddress);
    HashValue(hash, plan.State.VertexInput.VertexOffset);
    for (const auto& loader : plan.State.VertexInput.Loaders) {
        HashValue(hash, loader.PhysicalAddress);
        HashValue(hash, loader.DataOffset);
        HashValue(hash, loader.ByteStride);
        HashValue(hash, loader.ComponentCount);
        for (const uint8_t component : loader.Components) {
            HashValue(hash, component);
        }
    }
    for (const auto& texture : plan.Textures) {
        HashValue(hash, texture.Slot);
        HashValue(hash, texture.State.PhysicalAddress);
        HashValue(hash, texture.State.Width);
        HashValue(hash, texture.State.Height);
        HashValue(hash, texture.State.Format);
        HashValue(hash, texture.State.Type);
    }
    HashValue(hash, plan.State.Framebuffer.Width);
    HashValue(hash, plan.State.Framebuffer.Height);
    HashValue(hash, plan.State.Framebuffer.ColorFormat);
    HashValue(hash, plan.State.Framebuffer.DepthFormat);
    HashValue(hash, plan.State.Topology);
    return hash;
}

void SetOot3dPicaVisualInterfaceSnaps(bool snaps) noexcept { gInterfaceSnaps = snaps; }

bool AreOot3dPicaVisualDrawsCompatible(const Oot3dPicaVulkanDrawPlan& previous,
                                       const Oot3dPicaVulkanDrawPlan& current) {
    return HaveMatchingPipelineLayout(previous, current);
}

bool InterpolateOot3dPicaVisualDraw(const Oot3dPicaVulkanDrawPlan& previous,
                                    const Oot3dPicaVulkanDrawPlan& current,
                                    float alpha,
                                    Oot3dPicaVulkanDrawPlan& output) {
    if (!AreOot3dPicaVisualDrawsCompatible(previous, current)) {
        return false;
    }
    InterpolateCompatibleVisualDraw(previous, current, alpha, output);
    return true;
}

Oot3dPicaVisualTransitionStats
AnalyzeOot3dPicaVisualTransition(const Oot3dPicaVisualFrame& previous,
                                 const Oot3dPicaVisualFrame& current) {
    const auto matching = BuildVisualDrawMatching(previous, current);
    return AnalyzeVisualTransitionWithMatching(previous, current, matching);
}

bool SampleOot3dPicaVisualFrame(const Oot3dPicaVisualFrame& previous,
                                const Oot3dPicaVisualFrame& current,
                                float alpha,
                                Oot3dPicaVisualFrameSample& output) {
    const auto matching = BuildVisualDrawMatching(previous, current);
    return SampleVisualFrameWithMatching(previous, current, alpha, matching, output);
}

bool AnalyzeAndSampleOot3dPicaVisualFrame(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                          float alpha, Oot3dPicaVisualTransitionStats& stats,
                                          Oot3dPicaVisualFrameSample& output) {
    const auto matching = BuildVisualDrawMatching(previous, current);
    std::vector<VisualDrawDeltaFlags> drawDeltas;
    stats = AnalyzeVisualTransitionWithMatching(previous, current, matching, &drawDeltas);
    return SampleVisualFrameWithMatching(previous, current, alpha, matching, output, &drawDeltas);
}

bool AnalyzeAndSampleOot3dPicaVisualFrameView(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                              float alpha, Oot3dPicaVisualTransitionStats& stats,
                                              Oot3dPicaVisualFrameViewSample& output,
                                              Oot3dPicaVisualInterpolationTiming* timing) {
    const auto matchingStart = std::chrono::steady_clock::now();
    const auto matching = BuildVisualDrawMatching(previous, current);
    const auto analysisStart = std::chrono::steady_clock::now();
    std::vector<VisualDrawDeltaFlags> drawDeltas;
    stats = AnalyzeVisualTransitionWithMatching(previous, current, matching, &drawDeltas);
    const auto samplingStart = std::chrono::steady_clock::now();
    const bool sampled = SampleVisualFrameViewWithMatching(
        previous, current, alpha, matching, output, drawDeltas);
    if (timing != nullptr) {
        const auto finished = std::chrono::steady_clock::now();
        timing->MatchingNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                analysisStart - matchingStart)
                .count());
        timing->AnalysisNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                samplingStart - analysisStart)
                .count());
        timing->SamplingNanoseconds += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finished - samplingStart)
                .count());
    }
    return sampled;
}

void EvaluateOot3dPicaVisualInterpolation(const Oot3dPicaVisualFrame& before, const Oot3dPicaVisualFrame& middle,
                                          const Oot3dPicaVisualFrame& after, std::FILE* output) {
    if (output == nullptr) return;
    Oot3dPicaPreparedVisualTransition transition;
    Oot3dPicaVisualFrameViewSample sample;
    if (!transition.Prepare(before, after) || !transition.Sample(before, after, 0.5F, sample)) return;
    const auto truth = BuildVisualDrawMatching(middle, after);
    const auto outer = BuildVisualDrawMatching(before, after);
    // A point's place on a 720p screen through a draw's projection (f[0..3]).
    const auto screen = [](const Oot3dPicaVertexUniformState& u, const std::array<float, 3>& p,
                           std::array<float, 2>& at) {
        const auto row = [&](size_t r) {
            return u.Floats[r][0] * p[0] + u.Floats[r][1] * p[1] + u.Floats[r][2] * p[2] + u.Floats[r][3];
        };
        const float w = row(3);
        if (!(w > 20.0F) || !std::isfinite(w)) return false;  // at the camera: no meaningful place
        at = {row(0) / w * 640.0F, row(1) / w * 360.0F};
        return std::isfinite(at[0]) && std::isfinite(at[1]);
    };
    double total = 0.0, totalHeld = 0.0;
    size_t measured = 0, over = 0;
    for (const auto& view : sample.Draws) {
        if (view.BasePlan < after.Draws.data() || view.BasePlan >= after.Draws.data() + after.Draws.size()) continue;
        const size_t c = static_cast<size_t>(view.BasePlan - after.Draws.data());
        const auto& match = truth.PreviousByCurrent[c];
        if (!match.has_value()) continue;
        const auto& real = middle.Draws[match->PreviousIndex].VertexShader.Uniforms;
        const auto& shown = view.VertexUniforms.has_value() ? *view.VertexUniforms : after.Draws[c].VertexShader.Uniforms;
        const auto& held = after.Draws[c].VertexShader.Uniforms;
        if (!UsesModelViewLayout(real) || !UsesModelViewLayout(shown)) continue;
        // Each bone: its origin and the ends of its axes (8 units out), the
        // worst point of the worst bone.
        float worst = 0.0F, worstHeld = 0.0F;
        size_t worstRow = 0;
        // Only rows this draw loaded (the rest are the draw before's).
        const auto* realBefore =
            match->PreviousIndex > 0U ? &middle.Draws[match->PreviousIndex - 1U].VertexShader.Uniforms : nullptr;
        for (size_t first = 20U; first + 2U < 0x59U; first += 3U) {
            if (realBefore != nullptr && !RowsMoved(*realBefore, real, first) &&
                realBefore->Floats[first] == real.Floats[first])
                continue;
            const float dr = Determinant3(AffineAt(real, first)), ds = Determinant3(AffineAt(shown, first));
            if (!(std::abs(dr) > 1.0e-15F) || !std::isfinite(ds) || real.Floats[first] == held.Floats[first] &&
                real.Floats[first + 1] == held.Floats[first + 1] && real.Floats[first + 2] == held.Floats[first + 2] &&
                shown.Floats[first] == held.Floats[first] && shown.Floats[first + 1] == held.Floats[first + 1])
                continue;
            for (size_t point = 0; point < 4U; ++point) {
                std::array<float, 3> local{};
                if (point > 0U) local[point - 1U] = 8.0F / std::cbrt(std::max(std::abs(dr), 1.0e-12F));
                const auto place = [&](const Oot3dPicaVertexUniformState& u) {
                    std::array<float, 3> p{};
                    for (size_t r = 0; r < 3U; ++r)
                        p[r] = u.Floats[first + r][0] * local[0] + u.Floats[first + r][1] * local[1] +
                               u.Floats[first + r][2] * local[2] + u.Floats[first + r][3];
                    return p;
                };
                std::array<float, 2> a{}, b{}, h{};
                if (!screen(real, place(real), a) || !screen(real, place(shown), b) || !screen(real, place(held), h))
                    continue;
                const float error = std::hypot(a[0] - b[0], a[1] - b[1]);
                if (error > worst) { worst = error; worstRow = first; }
                worstHeld = std::max(worstHeld, std::hypot(a[0] - h[0], a[1] - h[1]));
            }
        }
        if (worst > 1.0e4F) continue;  // behind the camera
        ++measured;
        total += worst;
        totalHeld += worstHeld;
        const auto& draw = after.Draws[c];
        const auto& pair = outer.PreviousByCurrent[c];
        if (worst > 4.0F) {
            ++over;
            std::fprintf(output, "D %llu draw=%zu err=%.1f held=%.1f row=%zu kind=%d model=%08x ord=%u verts=%u motion=%d\n",
                         static_cast<unsigned long long>(middle.Sequence), c, worst, worstHeld, worstRow,
                         pair.has_value() ? static_cast<int>(pair->Kind) : -1, draw.Composition.NativeValue,
                         draw.Composition.Ordinal, draw.VertexCount, outer.MotionFor(c) != nullptr ? (outer.MotionFor(c)->Snap ? 2 : 1) : 0);
        }
    }
    std::fprintf(output, "F %llu measured=%zu mean=%.2f held=%.2f over=%zu\n",
                 static_cast<unsigned long long>(middle.Sequence), measured, measured ? total / measured : 0.0,
                 measured ? totalHeld / measured : 0.0, over);
    // Skeletons: every joint (bone origin) and its axes' ends 8 units out as
    // drawn (each bone read back from the draws' sampled palette) against
    // the real middle skeleton, through that model's middle projection. Per
    // model its worst joint. Any method is measured the same way.
    const auto middlePoses = CollectFramePoses(middle);
    const auto afterPoses = CollectFramePoses(after);
    if (afterPoses.empty()) return;
    std::unordered_map<const Oot3dPicaModelPose*, const Oot3dPicaModelPose*> realOf;
    for (const auto& [key, pose] : afterPoses) {
        const auto found = middlePoses.find(key);
        if (found != middlePoses.end() && found->second->Skeleton == pose->Skeleton &&
            found->second->Bones.size() == pose->Bones.size())
            realOf.emplace(pose, found->second);
    }
    std::unordered_map<const Oot3dPicaModelPose*, const Oot3dPicaVertexUniformState*> projectionOf;
    for (const auto& draw : middle.Draws)
        if (draw.Skinning != nullptr && draw.Skinning->Pose != nullptr && UsesModelViewLayout(draw.VertexShader.Uniforms))
            projectionOf.emplace(draw.Skinning->Pose.get(), &draw.VertexShader.Uniforms);
    struct Worst {
        float Error = 0.0F, Held = 0.0F;
        size_t Bone = 0;
    };
    std::unordered_map<const Oot3dPicaModelPose*, Worst> worstOf;
    for (const auto& view : sample.Draws) {
        if (view.BasePlan < after.Draws.data() || view.BasePlan >= after.Draws.data() + after.Draws.size()) continue;
        const auto& draw = *view.BasePlan;
        if (draw.Skinning == nullptr || draw.Skinning->Pose == nullptr) continue;
        const auto real = realOf.find(draw.Skinning->Pose.get());
        if (real == realOf.end()) continue;
        const auto projection = projectionOf.find(real->second);
        if (projection == projectionOf.end()) continue;
        const auto& skinning = *draw.Skinning;
        if (!SkinningMatchesUniforms(skinning, draw.VertexShader.Uniforms)) continue;
        const auto& shownUniforms = view.VertexUniforms.has_value() ? *view.VertexUniforms : draw.VertexShader.Uniforms;
        auto& worst = worstOf[skinning.Pose.get()];
        for (size_t entry = 0; entry < skinning.Bones.size(); ++entry) {
            const auto bone = static_cast<size_t>(skinning.Bones[entry]);
            // shown palette = bone * post: the bone as drawn.
            Affine34 post = skinning.InverseBind ? skinning.Pose->InverseBinds[bone] : Affine34{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
            for (size_t r = 0; r < 3U; ++r)
                post[r][3] += post[r][0] * skinning.Offset[0] + post[r][1] * skinning.Offset[1] + post[r][2] * skinning.Offset[2];
            const auto inversePost = InvertAffine(post);
            if (!inversePost) continue;
            const auto shown = MultiplyAffine(AffineAt(shownUniforms, 20U + 3U * entry), *inversePost);
            const auto& r = real->second->Bones[bone];
            const float det = Determinant3(r);
            if (!(std::abs(det) > 1.0e-15F)) continue;
            for (size_t point = 0; point < 4U; ++point) {
                std::array<float, 3> local{};
                if (point > 0U) local[point - 1U] = 8.0F / std::cbrt(std::abs(det));
                const auto place = [&](const Affine34& m) {
                    std::array<float, 3> p{};
                    for (size_t row = 0; row < 3U; ++row)
                        p[row] = m[row][0] * local[0] + m[row][1] * local[1] + m[row][2] * local[2] + m[row][3];
                    return p;
                };
                std::array<float, 2> a{}, b{}, h{};
                if (!screen(*projection->second, place(r), a) || !screen(*projection->second, place(shown), b) ||
                    !screen(*projection->second, place(skinning.Pose->Bones[bone]), h))
                    continue;
                const float error = std::hypot(a[0] - b[0], a[1] - b[1]);
                if (error > worst.Error) { worst.Error = error; worst.Bone = bone; }
                worst.Held = std::max(worst.Held, std::hypot(a[0] - h[0], a[1] - h[1]));
            }
        }
    }
    double poseTotal = 0.0, poseHeld = 0.0;
    size_t poseMeasured = 0;
    for (const auto& [pose, worst] : worstOf) {
        if (worst.Error > 1.0e4F) continue;
        ++poseMeasured;
        poseTotal += worst.Error;
        poseHeld += worst.Held;
        if (worst.Error > 4.0F)
            std::fprintf(output, "B %llu model=%08x bones=%zu err=%.1f held=%.1f bone=%zu parent=%d\n",
                         static_cast<unsigned long long>(middle.Sequence), pose->Model, pose->Bones.size(), worst.Error,
                         worst.Held, worst.Bone, pose->Parents[worst.Bone]);
    }
    std::fprintf(output, "S %llu models=%zu mean=%.2f held=%.2f\n", static_cast<unsigned long long>(middle.Sequence),
                 poseMeasured, poseMeasured ? poseTotal / poseMeasured : 0.0, poseMeasured ? poseHeld / poseMeasured : 0.0);
}

bool ViewOot3dPicaVisualFrame(
    const Oot3dPicaVisualFrame& frame,
    Oot3dPicaVisualFrameViewSample& output) {
    output.TopTransfer = frame.TopTransfer;
    output.MemoryFills = &frame.MemoryFills;
    output.DisplayTransfers = &frame.DisplayTransfers;
    output.MatchedDraws = 0;
    output.StrictUniqueMatchedDraws = 0;
    output.StrictOrdinalMatchedDraws = 0;
    output.StructuralOrdinalMatchedDraws = 0;
    output.PipelineOrdinalMatchedDraws = 0;
    output.InterpolatedDraws = 0;
    output.InterpolatedPerInstanceVertexDraws = 0;
    output.InterpolatedPerVertexDraws = 0;
    output.ViewAdjustmentValid = false;  // drawn exactly as the game frame
    output.Draws.resize(frame.Draws.size());
    for (size_t index = 0; index < frame.Draws.size(); ++index) {
        auto& view = output.Draws[index];
        view.BasePlan = &frame.Draws[index];
        view.VertexUniforms.reset();
        view.FragmentUniforms.reset();
        view.Viewport.reset();
        view.BlendConstantColor.reset();
        view.VertexBindings.clear();
        view.HasVertexBindingOverrides = false;
    }
    return !output.Draws.empty();
}

void Oot3dPicaVisualFrameAccumulator::Append(Oot3dPicaVulkanDrawPlan plan) {
    mPendingDraws.push_back(std::move(plan));
}

void Oot3dPicaVisualFrameAccumulator::Append(
    Oot3dPicaMemoryFillSubmission fill) {
    mPendingMemoryFills.push_back(std::move(fill));
}

void Oot3dPicaVisualFrameAccumulator::Append(
    Oot3dPicaDisplayTransferSubmission transfer) {
    mPendingDisplayTransfers.push_back(std::move(transfer));
}

std::optional<Oot3dPicaVisualFrame> Oot3dPicaVisualFrameAccumulator::Finish(
    const Oot3dPicaDisplayTransferSubmission& topTransfer, bool allowTransferOnly) {
    if (mPendingDraws.empty() && (!allowTransferOnly || (mPendingMemoryFills.empty() && mPendingDisplayTransfers.empty()))) {
        return std::nullopt;
    }
    Oot3dPicaVisualFrame frame;
    frame.Sequence = mNextSequence++;
    frame.TopTransfer = topTransfer;
    frame.Draws = std::move(mPendingDraws);
    frame.StrictDrawIdentities.reserve(frame.Draws.size());
    for (const auto& draw : frame.Draws) {
        frame.StrictDrawIdentities.push_back(ComputeOot3dPicaVisualDrawIdentity(draw));
    }
    frame.MemoryFills = std::move(mPendingMemoryFills);
    frame.DisplayTransfers = std::move(mPendingDisplayTransfers);
    mPendingDraws.clear();
    mPendingMemoryFills.clear();
    mPendingDisplayTransfers.clear();
    return frame;
}

size_t Oot3dPicaVisualFrameAccumulator::PendingDrawCount() const {
    return mPendingDraws.size();
}

Oot3dPicaVisualFrameAccumulatorState
Oot3dPicaVisualFrameAccumulator::CaptureState() const {
    return {mNextSequence, mPendingDraws, mPendingMemoryFills,
            mPendingDisplayTransfers};
}

bool Oot3dPicaVisualFrameAccumulator::RestoreState(
    Oot3dPicaVisualFrameAccumulatorState state) {
    if (state.NextSequence == 0U) {
        return false;
    }
    mNextSequence = state.NextSequence;
    mPendingDraws = std::move(state.PendingDraws);
    mPendingMemoryFills = std::move(state.PendingMemoryFills);
    mPendingDisplayTransfers = std::move(state.PendingDisplayTransfers);
    return true;
}

double Oot3dPicaVisualContinuityTracker::CurrentThreshold() const {
    constexpr double kMinimumNormalizedDeltaThreshold = 0.5;
    constexpr double kBaselineMultiplier = 4.0;
    constexpr double kBaselineAllowance = 0.1;
    if (mAcceptedDeltas.empty()) {
        return kMinimumNormalizedDeltaThreshold;
    }
    auto sorted = mAcceptedDeltas;
    std::sort(sorted.begin(), sorted.end());
    const double baseline = sorted[sorted.size() / 2U];
    return std::max(kMinimumNormalizedDeltaThreshold,
                    baseline * kBaselineMultiplier + kBaselineAllowance);
}

bool Oot3dPicaVisualContinuityTracker::Accept(
    const Oot3dPicaVisualTransitionStats& transition) {
    constexpr size_t kMaximumAcceptedDeltaHistory = 31U;
    constexpr double kMinimumMatchedDrawCoverage = 0.85;
    if (transition.ModelViewCamera) {
        // Turning fast, many draws come into view and every matrix changes a
        // lot: the coverage/motion rule below rejected those frames (shown
        // without in-betweens). With the camera measured, only a cut or a
        // frame mostly unlike the last (a room/scene change) is discontinuous.
        constexpr double kMinimumModelViewCoverage = 0.4;
        if (transition.CameraDiscontinuousDraws != 0U ||
            transition.MatchedDrawCoverage < kMinimumModelViewCoverage) {
            mAcceptedDeltas.clear();
            return false;
        }
        return true;
    }
    const bool structuralDiscontinuity =
        transition.MatchedDrawCoverage < kMinimumMatchedDrawCoverage;
    const bool motionOutlier =
        transition.MedianNormalizedContinuousDelta > CurrentThreshold();
    if (transition.TopTargetMatchedDraws == 0U ||
        (transition.CameraDiscontinuousDraws != 0U &&
         transition.CameraDiscontinuousDraws * 2U >= transition.CameraMatchedDraws) ||
        (structuralDiscontinuity && motionOutlier)) {
        mAcceptedDeltas.clear();
        return false;
    }
    if (mAcceptedDeltas.size() == kMaximumAcceptedDeltaHistory) {
        mAcceptedDeltas.erase(mAcceptedDeltas.begin());
    }
    mAcceptedDeltas.push_back(
        transition.MedianNormalizedContinuousDelta);
    return true;
}

Oot3dPicaVisualContinuityTrackerState
Oot3dPicaVisualContinuityTracker::CaptureState() const {
    return {mAcceptedDeltas};
}

bool Oot3dPicaVisualContinuityTracker::RestoreState(
    Oot3dPicaVisualContinuityTrackerState state) {
    constexpr size_t kMaximumAcceptedDeltaHistory = 31U;
    if (state.AcceptedDeltas.size() > kMaximumAcceptedDeltaHistory ||
        std::any_of(state.AcceptedDeltas.begin(), state.AcceptedDeltas.end(),
                    [](double value) {
                        return !std::isfinite(value) || value < 0.0;
                    })) {
        return false;
    }
    mAcceptedDeltas = std::move(state.AcceptedDeltas);
    return true;
}

} // namespace Oot3dNativeGame
