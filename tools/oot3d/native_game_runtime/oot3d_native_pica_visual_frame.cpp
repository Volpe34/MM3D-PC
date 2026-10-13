#include <map>
#include "oot3d_native_pica_visual_frame.h"
#include "fast/renderer3ds/pica_vertex_temporal_policy.h"
#include "fast/renderer3ds/pica_camera_temporal_policy.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>
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
    return effectVertexBlend || current.Composition.Layer != Oot3dPicaCompositionLayer::TransparentWorld;
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

    MatchEqualCardinalityGroups(
        previous, current, noPreviousMatches, previousMatched, currentMatched,
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
        if (bestVotes > 0U) {
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
    // the views as a rigid camera motion instead. Slow turns keep the linear
    // blend (the same at the game frames themselves).
    if (matching.CameraDeltaValid) {
        static const bool forced = std::getenv("TRIAEVUM_RIGID_CAMERA") != nullptr;
        static const bool linearOnly = std::getenv("TRIAEVUM_LINEAR_CAMERA") != nullptr;
        const auto& d = matching.CameraDelta;
        const float angle = std::acos(std::clamp((d[0][0] + d[1][1] + d[2][2] - 1.0F) * 0.5F, -1.0F, 1.0F));
        matching.RigidCamera = !linearOnly && (forced || angle > 5.0F * 0.0174533F);
    }
    static const bool orderPairs = std::getenv("TRIAEVUM_ORDER_PAIRS") != nullptr;
    // Left unpaired on purpose (not re-paired below).
    std::vector<bool> keepUnpaired(current.Draws.size(), false);
    if (!orderPairs && matching.CameraDeltaValid) {
        if (const auto inverseCamera = InvertAffine(matching.CameraDelta)) {
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
                if (!match.has_value() || match->Kind == VisualDrawMatchKind::StrictUnique) continue;
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
    if (jumpGuard && matching.CameraDeltaValid) {
        // World motion of every model/bone matrix with the camera change removed.
        // Only the first model matrix: later bone registers keep leftovers from
        // earlier draws when a model doesn't use them (PICA uniforms persist),
        // and those never follow the camera, so checking them rejected nearly
        // every pairing while the camera turned (the global stutter).
        const auto worldJump = [&](const Oot3dPicaVertexUniformState& before,
                                   const Oot3dPicaVertexUniformState& after) {
            float worst = 0.0F;
            for (size_t first = 20U; first < 21U; first += 3U) {
                const auto predicted = MultiplyAffine(matching.CameraDelta, AffineAt(after, first));
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
            const float limit = match->Kind == VisualDrawMatchKind::StrictUnique ? kMaximumUniqueJump
                                                                                  : kMaximumOrdinalJump;
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
            std::fprintf(matchAudit, "D %zu %d %d %d %016llx %016llx %08x %08x %u %d %.3f %.3f %.3f\n", currentIndex,
                         match.has_value() ? static_cast<int>(match->PreviousIndex) : -1,
                         match.has_value() ? static_cast<int>(match->Kind) : -1,
                         UsesModelViewLayout(draw.VertexShader.Uniforms) ? 1 : 0,
                         static_cast<unsigned long long>(currentStrict[currentIndex]),
                         static_cast<unsigned long long>(currentStructural[currentIndex]), texture,
                         draw.State.VertexInput.PhysicalBaseAddress, draw.VertexCount,
                         static_cast<int>(draw.Composition.Layer), f[20][3], f[21][3], f[22][3]);
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

Affine34 PartialCameraDelta(const Affine34& d, float w) {
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
                                   Oot3dPicaVertexUniformState& output) {
    if (!matching.RigidCamera || !CameraInterpolationEnabled() || !matching.CameraDeltaValid ||
        !UsesModelViewLayout(previous) || !UsesModelViewLayout(current))
        return;
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
    Oot3dPicaVertexUniformState& vertexUniforms) {
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
    std::array<float, 4>& blendConstantColor) {
    InterpolateVisualVertexUniforms(previous, current, alpha,
                                    vertexUniforms);
    InterpolateVisualFragmentUniforms(previous, current, alpha,
                                      fragmentUniforms);
    InterpolateVisualViewport(previous, current, alpha, viewport);
    InterpolateVisualBlendConstantColor(previous, current, alpha,
                                        blendConstantColor);
}

void InterpolateCompatibleVisualDraw(const Oot3dPicaVulkanDrawPlan& previous, const Oot3dPicaVulkanDrawPlan& current,
                                     float alpha, Oot3dPicaVulkanDrawPlan& output) {
    output = current;
    InterpolateCompatibleVisualRenderState(previous, current, alpha, output.VertexShader.Uniforms,
                                           output.FragmentShader.Uniforms, output.State.Viewport,
                                           output.State.OutputMerger.Blend.ConstantColor);
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
            InterpolateCompatibleVisualDraw(previousDraw, currentDraw, alpha, sampled);
            if (match->Kind == VisualDrawMatchKind::GeometryOrdinal)
                sampled.FragmentShader.Uniforms = currentDraw.FragmentShader.Uniforms;
            ApplyRigidCameraInterpolation(matching, previousDraw.VertexShader.Uniforms,
                                          currentDraw.VertexShader.Uniforms, alpha,
                                          sampled.VertexShader.Uniforms);
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
            InterpolateVisualVertexUniforms(
                previousDraw, currentDraw, alpha, *sampled.VertexUniforms);
            ApplyRigidCameraInterpolation(matching, previousDraw.VertexShader.Uniforms,
                                          currentDraw.VertexShader.Uniforms, alpha, *sampled.VertexUniforms);
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
            std::vector<std::vector<Oot3dPicaVisualDrawView>> inserted;
            size_t insertedCount = 0;
            for (size_t previousIndex = 0; previousIndex < previous.Draws.size(); ++previousIndex) {
                if (currentOfPrevious[previousIndex] != kNone) continue;
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
            if (insertedCount != 0U) {
                std::vector<Oot3dPicaVisualDrawView> merged;
                merged.reserve(output.Draws.size() + insertedCount);
                for (size_t index = 0; index < output.Draws.size(); ++index) {
                    merged.push_back(std::move(output.Draws[index]));
                    for (auto& extra : inserted[index]) merged.push_back(std::move(extra));
                }
                output.Draws = std::move(merged);
                static const bool trace = std::getenv("TRIAEVUM_JUMP_TRACE") != nullptr;
                if (trace)
                    std::fprintf(stderr, "TRIAEVUM_CULLED_KEPT %zu alpha=%.2f turn=%.1f move=%.1f\n", insertedCount,
                                 alpha, cameraTurn * 57.2958F, cameraMove);
            }
        }
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
