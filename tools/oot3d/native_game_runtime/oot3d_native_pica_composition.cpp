#include "oot3d_native_pica_composition.h"

#include "oot3d_typed_gameplay_bridge.h"
#include "recomp/a32_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <limits>
#include <string_view>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kPicaCommandListCursorAddress = 0x0054CC4CU;
constexpr uint32_t kMeshPacketByteCountOffset = 0x10U;
// Exact UI draw owners from ui_native_workflow_closure. The enclosing
// DrawViewPass also executes scene callbacks and must NOT be classified UI.
constexpr std::array kUiViews{
    std::pair{0x0042B9F4U, 0x003004E0U},
    std::pair{0x0041EC50U, 0x00419830U},
    std::pair{0x0041F308U, 0x0041983CU},
    std::pair{0x00425930U, 0x0041EB14U},
    // Orthographic renderer-local backdrop (single model at GraphRenderer+743C).
    std::pair{0x002FFF7CU, 0x00419824U},
    // Renderer orthographic deferred-model queue 6 (including Hint/Visions).
    // 0041AFAC installs its own UI projection before consuming the queue;
    // neither the shared queue executor nor its update/enqueue callers own UI.
    std::pair{0x0041AFACU, 0x00419890U},
    // Screen-space overlay queue 4, consumed after DrawViewPass's UI projection.
    // Includes Hint primitives enqueued earlier by 0047087C/00483D10.
    std::pair{0x004228E4U, 0x00300524U},
    // HintMovie_Init installs these two native UI dispatch callbacks in
    // GraphRenderer+7440/+7444. Only their exact invocation sites are UI.
    std::pair{0x00471F60U, 0x0030051CU},
    std::pair{0x00477E30U, 0x0041984CU},
    // Message renderers in DrawViewPass: backdrop/choice, auxiliary glyphs,
    // contextual message glyphs and the main message context.
    std::pair{0x0042CB54U, 0x00300530U},
    std::pair{0x0042A278U, 0x0030053CU},
    std::pair{0x00427A3CU, 0x00300548U},
    std::pair{0x0042CBCCU, 0x00300554U}};
constexpr std::array kCmbReturnPcs{
    0x002FADDCU, 0x002FADF4U, 0x002FAEC0U, 0x002FAED8U,
    0x002FEA68U, 0x002FEA74U, 0x003FE408U, 0x003FE414U,
    0x003FE430U, 0x003FE43CU,
};

struct AtmosphereScopeDescriptor {
    uint32_t EntryPc;
    std::span<const uint32_t> ReturnPcs;
};

constexpr std::array kObjectKankyoReturnPcs{kOot3dActorDrawCallbackReturn};
constexpr std::array kSceneRendererReturnPcs{
    0x002E2B8CU, 0x002E2BB8U, 0x0045D3E8U};
constexpr std::array kGameplayFadeReturnPcs{0x002E2BD0U};
constexpr std::array kRandomizedOverlayReturnPcs{0x002E2BDCU};
constexpr std::array kRandomizedModelsReturnPcs{0x002E2BE8U};
constexpr std::array kPrecipitationReturnPcs{0x002E2CD0U};
constexpr std::array kCosineOverlayReturnPcs{0x002E2D2CU};
constexpr std::array kProjectedOverlayReturnPcs{0x002E2D34U};
constexpr std::array kNormalizedColorReturnPcs{0x002E2D78U, 0x002E2DA4U};
// Gameplay_Draw builds the native environment queue through the owners above,
// then executes that queue at 0x002E2C00. The same generic executor services
// unrelated queues elsewhere, so only this exact continuation is atmospheric.
constexpr std::array kGameplayAtmosphereQueueReturnPcs{0x002E2C04U};
// The only native xref to this primitive consumer is the kankyo/effect render
// context submit at 0x003FBC58.
constexpr std::array kKankyoEffectPrimitiveReturnPcs{0x003FBC5CU};
constexpr std::array kPrimitivePacketReturnPcs{
    0x003FB984U, 0x003FB998U, 0x004527BCU};

constexpr std::array kAtmosphereScopes{
    AtmosphereScopeDescriptor{kOot3dObjectKankyoDrawEntry,
                              kObjectKankyoReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dSceneRendererSubmitConfiguredModelPassEntry,
        kSceneRendererReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dEnvironmentRendererUpdateGameplayFadeLayersEntry,
        kGameplayFadeReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dEnvironmentRendererUpdateRandomizedOverlayEntry,
        kRandomizedOverlayReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dGameplayDrawRandomizedTextureStageModelsEntry,
        kRandomizedModelsReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dGameplayDrawPrecipitationEffectsEntry,
        kPrecipitationReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dGameplayDrawCosineScaledOverlayEntry,
        kCosineOverlayReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dGameplayDrawConfiguredProjectedOverlayEntry,
        kProjectedOverlayReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dEnvironmentRendererSubmitNormalizedColor4Entry,
        kNormalizedColorReturnPcs},
    AtmosphereScopeDescriptor{kOot3dRendererCommandListExecuteEntry,
                              kGameplayAtmosphereQueueReturnPcs},
    AtmosphereScopeDescriptor{kOot3dKankyoEffectPrimitiveDrawEntry,
                              kKankyoEffectPrimitiveReturnPcs},
};

constexpr auto BuildCompositionHookPcs() {
    constexpr size_t count =
        2U + kUiViews.size() * 2U + kCmbReturnPcs.size() +
        (kObjectKankyoReturnPcs.size() + kSceneRendererReturnPcs.size() +
         kGameplayFadeReturnPcs.size() +
         kRandomizedOverlayReturnPcs.size() +
         kRandomizedModelsReturnPcs.size() +
         kPrecipitationReturnPcs.size() + kCosineOverlayReturnPcs.size() +
         kProjectedOverlayReturnPcs.size() +
         kNormalizedColorReturnPcs.size() +
         kGameplayAtmosphereQueueReturnPcs.size() +
         kKankyoEffectPrimitiveReturnPcs.size()) +
        kAtmosphereScopes.size() + 1U + kPrimitivePacketReturnPcs.size();
    std::array<uint32_t, count> hooks{};
    size_t cursor = 0;
    for (const auto& [entry, returnPc] : kUiViews) {
        hooks[cursor++] = entry;
        hooks[cursor++] = returnPc;
    }
    hooks[cursor++] = kOot3dCmbRendererSubmitDrawHandleEntry;
    hooks[cursor++] = kOot3dMeshCommandPacketSubmitEntry;
    for (const auto pc : kCmbReturnPcs) {
        hooks[cursor++] = pc;
    }
    for (const auto& descriptor : kAtmosphereScopes) {
        hooks[cursor++] = descriptor.EntryPc;
        for (const auto pc : descriptor.ReturnPcs) {
            hooks[cursor++] = pc;
        }
    }
    hooks[cursor++] = kOot3dPicaPrimitivePacketBuilderEntry;
    for (const auto pc : kPrimitivePacketReturnPcs) {
        hooks[cursor++] = pc;
    }
    return hooks;
}

constexpr auto kCompositionHookPcs = BuildCompositionHookPcs();

void SetError(std::string* error, std::string_view message) {
    if (error != nullptr) {
        *error = message;
    }
}

Oot3dPicaCompositionAttribution AttributionForCmbPass(uint8_t pass) {
    // CmbRenderer_SubmitDrawHandle stores this byte at renderer+0x15 and uses
    // it to split MSHS meshes at the native +0x0C boundary. Native control
    // flow submits pass 0 before pass 1 throughout the model-list owners.
    return {
        pass == 0U ? Oot3dPicaCompositionLayer::OpaqueWorld
                   : Oot3dPicaCompositionLayer::TransparentWorld,
        Oot3dPicaCompositionProvenance::NativeCmbDrawPass,
        kOot3dCmbRendererSubmitDrawHandleEntry,
        pass,
    };
}

Oot3dPicaCompositionAttribution AttributionForAtmosphere(
    uint32_t sourcePc, uint32_t nativeValue) {
    return {
        Oot3dPicaCompositionLayer::Atmosphere,
        Oot3dPicaCompositionProvenance::NativeControlFlow,
        sourcePc,
        nativeValue,
    };
}

const AtmosphereScopeDescriptor* FindAtmosphereScope(uint32_t pc) {
    const auto found = std::find_if(
        kAtmosphereScopes.begin(), kAtmosphereScopes.end(),
        [pc](const auto& descriptor) { return descriptor.EntryPc == pc; });
    return found == kAtmosphereScopes.end() ? nullptr : &*found;
}

bool Contains(std::span<const uint32_t> values, uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

uint32_t AtmosphereNativeValue(
    uint32_t sourcePc, const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    Oot3dNativePicaCompositionStats& stats) {
    if (sourcePc == kOot3dObjectKankyoDrawEntry) {
        uint16_t selector = std::numeric_limits<uint16_t>::max();
        if (!memory.Read16(state.r[0] + 0x1CU, &selector)) {
            ++stats.AtmosphereSelectorReadFailures;
        }
        return selector;
    }
    if (sourcePc == kOot3dSceneRendererSubmitConfiguredModelPassEntry) {
        return (state.r[2] & 0xFFFFU) | ((state.r[3] & 0xFFFFU) << 16U);
    }
    return 0U;
}

bool SameAttribution(const Oot3dPicaCompositionAttribution& left,
                     const Oot3dPicaCompositionAttribution& right) {
    return left == right;
}

} // namespace

std::span<const uint32_t> Oot3dNativePicaCompositionHookPcs() noexcept {
    return kCompositionHookPcs;
}

std::span<const uint32_t> Mm3dNativePicaCompositionHookPcs() noexcept {
    static constexpr std::array kHooks{kMm3dModelDrawEntry, kMm3dPrimitiveSetDrawEntry,
                                       kMm3dModelDrawReturn};
    return kHooks;
}

namespace {

bool ReadGuestMatrix(oot3d::recomp::a32::MemoryBus& memory, uint32_t address,
                     Oot3dPicaModelPose::Matrix& matrix) {
    for (size_t row = 0; row < 3U; ++row) {
        for (size_t column = 0; column < 4U; ++column) {
            uint32_t bits = 0U;
            if (!memory.Read32(address + static_cast<uint32_t>((row * 4U + column) * 4U), &bits)) return false;
            std::memcpy(&matrix[row][column], &bits, sizeof(bits));
        }
    }
    return true;
}

// The model's skeleton (model + 8): +0x0C its data ([0] the CMB "skl "
// chunk, bone count at +8; [4] the bone records, 0x2C bytes, parent at +2),
// +0x10 the inverse bind matrices, +0x24 the bone matrices the draw loads.
std::shared_ptr<const Oot3dPicaModelPose> ReadMm3dModelPose(oot3d::recomp::a32::MemoryBus& memory,
                                                            uint32_t model) {
    constexpr uint32_t kSkeletonMagic = 0x206C6B73U;  // "skl "
    constexpr uint32_t kMaximumBones = 256U;
    uint32_t skeleton = 0U, data = 0U, chunk = 0U, records = 0U, magic = 0U, count = 0U;
    uint32_t bones = 0U, inverseBinds = 0U;
    if (!memory.Read32(model + 8U, &skeleton) || skeleton == 0U || !memory.Read32(skeleton + 0x0CU, &data) ||
        data == 0U || !memory.Read32(data, &chunk) || !memory.Read32(data + 4U, &records) ||
        !memory.Read32(chunk, &magic) || magic != kSkeletonMagic || !memory.Read32(chunk + 8U, &count) ||
        count == 0U || count > kMaximumBones || !memory.Read32(skeleton + 0x24U, &bones) ||
        !memory.Read32(skeleton + 0x10U, &inverseBinds) || bones == 0U || inverseBinds == 0U)
        return nullptr;
    auto pose = std::make_shared<Oot3dPicaModelPose>();
    pose->Model = model;
    pose->Skeleton = chunk;
    pose->Parents.resize(count);
    pose->Bones.resize(count);
    pose->InverseBinds.resize(count);
    for (uint32_t bone = 0; bone < count; ++bone) {
        uint16_t parent = 0U;
        if (!memory.Read16(records + 0x2CU * bone + 2U, &parent)) return nullptr;
        const auto signedParent = static_cast<int16_t>(parent);
        if (signedParent >= static_cast<int32_t>(count) || signedParent == static_cast<int32_t>(bone))
            return nullptr;
        pose->Parents[bone] = signedParent < 0 ? int16_t{-1} : signedParent;
        if (!ReadGuestMatrix(memory, bones + 48U * bone, pose->Bones[bone]) ||
            !ReadGuestMatrix(memory, inverseBinds + 48U * bone, pose->InverseBinds[bone]))
            return nullptr;
    }
    return pose;
}

} // namespace

void Oot3dNativePicaCompositionTracker::CloseMm3dPart(uint32_t end) {
    if (!mActiveModelDraw.has_value() || mActiveModelDraw->Part == nullptr) return;
    const uint32_t begin = mActiveModelDraw->PartBegin;
    auto skinning = std::move(mActiveModelDraw->Part);
    mActiveModelDraw->Part.reset();
    if (end <= begin || end - begin > 0x100000U) return;
    auto it = mPartSpans.lower_bound(begin);
    if (it != mPartSpans.begin() && std::prev(it)->second.EndAddress > begin) --it;
    while (it != mPartSpans.end() && it->first < end) it = mPartSpans.erase(it);
    if (mPartSpans.size() >= MaximumModelSpans) mPartSpans.clear();
    mPartSpans.emplace(begin, PartSpan{end, mModelListBase, mModelListSerial, std::move(skinning)});
}

void Oot3dNativePicaCompositionTracker::ObserveMm3dBlockEntry(
    uint32_t pc, const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory) {
    if (pc == kMm3dPrimitiveSetDrawEntry) {
        static const bool disabled = std::getenv("TRIAEVUM_NO_MODEL_POSES") != nullptr;
        uint32_t cursor = 0U;
        if (disabled || !mActiveModelDraw.has_value() || state.r[0] != mActiveModelDraw->Model ||
            !memory.Read32(kMm3dCommandListCursorAddress, &cursor))
            return;
        CloseMm3dPart(cursor);
        auto& draw = *mActiveModelDraw;
        if (!draw.PoseRead) {
            draw.PoseRead = true;
            draw.Pose = ReadMm3dModelPose(memory, draw.Model);
        }
        if (draw.Pose == nullptr) return;
        // The set (CMB "prms"): +0x0C skinning mode (2: smooth), +0x0E bone
        // count, +0x10 offset of its bone table; the shape's +0x18 is the
        // position offset every bone matrix is translated by.
        uint32_t shape = 0U, offsets = 0U;
        uint16_t setOffset = 0U, mode = 0U, count = 0U, tableOffset = 0U;
        if (!memory.Read32(state.r[1], &shape) || !memory.Read32(state.r[1] + 4U, &offsets) ||
            !memory.Read16(offsets + 2U * state.r[2], &setOffset))
            return;
        const uint32_t set = shape + setOffset;
        if (!memory.Read16(set + 0x0CU, &mode) || !memory.Read16(set + 0x0EU, &count) ||
            !memory.Read16(set + 0x10U, &tableOffset) || count == 0U || count > 23U)
            return;
        auto skinning = std::make_shared<Oot3dPicaDrawSkinning>();
        skinning->Pose = draw.Pose;
        skinning->InverseBind = mode == 2U && count > 1U;
        skinning->Bones.resize(count);
        const uint32_t table = set + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(tableOffset)));
        for (uint32_t k = 0; k < count; ++k) {
            uint16_t bone = 0U;
            if (!memory.Read16(table + 2U * k, &bone) || bone >= draw.Pose->Bones.size()) return;
            skinning->Bones[k] = static_cast<int16_t>(bone);
        }
        for (size_t axis = 0; axis < 3U; ++axis) {
            uint32_t bits = 0U;
            if (!memory.Read32(shape + 0x18U + static_cast<uint32_t>(axis * 4U), &bits)) return;
            std::memcpy(&skinning->Offset[axis], &bits, sizeof(bits));
        }
        draw.PartBegin = cursor;
        draw.Part = std::move(skinning);
        return;
    }
    if (pc == kMm3dModelDrawEntry) {
        ++mStats.ModelDrawEntries;
        mActiveModelDraw.reset();
        uint32_t cursor = 0U;
        if (state.r[14] != kMm3dModelDrawReturn ||
            !memory.Read32(kMm3dCommandListCursorAddress, &cursor)) {
            ++mStats.ReadFailures;
            return;
        }
        // A list started over (another buffer, or the cursor went back):
        // ranges recorded in that buffer by earlier frames are stale.
        uint32_t list = 0U;
        uint32_t base = 0U;
        if (memory.Read32(kMm3dGraphicsContextAddress + 0x9CU, &list) &&
            memory.Read32(list + 4U, &base) &&
            (base != mModelListBase || cursor < mModelListCursor)) {
            mModelSpans.erase(mModelSpans.lower_bound(base),
                              mModelSpans.lower_bound(cursor));
            mPartSpans.erase(mPartSpans.lower_bound(base),
                             mPartSpans.lower_bound(cursor));
            mModelListBase = base;
            mModelListSerials[base] = ++mModelListSerial;
            if (mModelListSerials.size() > 64U) mModelListSerials.clear();
        }
        mModelListCursor = cursor;
        mActiveModelDraw = ActiveModelDraw{state.r[0], cursor};
        return;
    }
    if (pc != kMm3dModelDrawReturn || !mActiveModelDraw.has_value()) {
        return;
    }
    uint32_t end = 0U;
    if (!memory.Read32(kMm3dCommandListCursorAddress, &end)) {
        ++mStats.ReadFailures;
        mActiveModelDraw.reset();
        return;
    }
    CloseMm3dPart(end);
    const auto draw = std::move(*mActiveModelDraw);
    mActiveModelDraw.reset();
    // A full list continues in another buffer: the range is not contiguous.
    if (end <= draw.BeginAddress || end - draw.BeginAddress > 0x100000U) {
        if (end != draw.BeginAddress) ++mStats.InvalidModelSpans;
        return;
    }
    auto it = mModelSpans.lower_bound(draw.BeginAddress);
    if (it != mModelSpans.begin() &&
        std::prev(it)->second.EndAddress > draw.BeginAddress) {
        --it;
    }
    while (it != mModelSpans.end() && it->first < end) {
        it = mModelSpans.erase(it);
    }
    if (mModelSpans.size() >= MaximumModelSpans) mModelSpans.clear();
    mModelSpans.emplace(draw.BeginAddress,
                        ModelSpan{end, draw.Model, mModelListBase, mModelListSerial});
    ++mStats.ModelSpans;
}

bool Oot3dNativePicaCompositionTracker::ResolveCommandAddress(
    uint32_t address, Oot3dPicaCompositionAttribution& attribution) const {
    auto it = mModelSpans.upper_bound(address);
    if (it == mModelSpans.begin()) return false;
    --it;
    if (address >= it->second.EndAddress) return false;
    const auto serial = mModelListSerials.find(it->second.ListBase);
    if (serial == mModelListSerials.end() ||
        serial->second != it->second.ListSerial) {
        return false;
    }
    attribution = {Oot3dPicaCompositionLayer::Unknown,
                   Oot3dPicaCompositionProvenance::NativeModelInstance,
                   kMm3dModelDrawEntry, it->second.Model};
    return true;
}

std::shared_ptr<const Oot3dPicaDrawSkinning> Oot3dNativePicaCompositionTracker::ResolveDrawSkinning(
    uint32_t address) const {
    auto it = mPartSpans.upper_bound(address);
    if (it == mPartSpans.begin()) return nullptr;
    --it;
    if (address >= it->second.EndAddress) return nullptr;
    const auto serial = mModelListSerials.find(it->second.ListBase);
    if (serial == mModelListSerials.end() || serial->second != it->second.ListSerial) return nullptr;
    return it->second.Skinning;
}

bool Oot3dNativePicaCompositionTracker::RecordPendingSpan(
    Oot3dPicaCommandListCompositionSpan span) {
    // Reject a crossing owner before erasing nested spans. Otherwise an invalid
    // UI range could destroy valid scene attribution even though it is rejected.
    if (span.Attribution.Layer == Oot3dPicaCompositionLayer::Ui) {
        for (const auto& pending : mPendingSpans) {
            const bool overlaps = span.BeginAddress < pending.EndAddress &&
                                  pending.BeginAddress < span.EndAddress;
            const bool contains = span.BeginAddress <= pending.BeginAddress &&
                                  span.EndAddress >= pending.EndAddress;
            if (overlaps && !contains && !SameAttribution(span.Attribution, pending.Attribution)) {
                ++mStats.OverlappingPacketSpans;
                return false;
            }
        }
    }
    for (auto it = mPendingSpans.begin(); it != mPendingSpans.end();) {
        const bool overlaps = span.BeginAddress < it->EndAddress &&
                              it->BeginAddress < span.EndAddress;
        const bool adjacent = span.EndAddress == it->BeginAddress ||
                              it->EndAddress == span.BeginAddress;
        if (span.Attribution.Layer == Oot3dPicaCompositionLayer::Ui &&
            span.BeginAddress <= it->BeginAddress && span.EndAddress >= it->EndAddress) {
            // A model inside a UI view is UI, not opaque world geometry.
            it = mPendingSpans.erase(it);
            continue;
        }
        if (SameAttribution(span.Attribution, it->Attribution) &&
            (overlaps || adjacent)) {
            span.BeginAddress = std::min(span.BeginAddress, it->BeginAddress);
            span.EndAddress = std::max(span.EndAddress, it->EndAddress);
            it = mPendingSpans.erase(it);
            ++mStats.CoalescedPacketSpans;
            continue;
        }
        if (overlaps) {
            ++mStats.OverlappingPacketSpans;
            return false;
        }
        ++it;
    }
    if (mPendingSpans.size() == MaximumPendingSpans) {
        mPendingSpans.pop_front();
        ++mStats.EvictedPacketSpans;
    }
    mPendingSpans.push_back(std::move(span));
    return true;
}

void Oot3dNativePicaCompositionTracker::ObserveBlockEntry(
    uint32_t pc, const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory) {
    if (mMm3dModelInstances) {
        ObserveMm3dBlockEntry(pc, state, memory);
        return;
    }
    const auto ui = std::find(kUiViews.begin(), kUiViews.end(), std::pair{pc, state.r[14]});
    if (ui != kUiViews.end() && !mActiveUiScope) {
        ActiveAtmosphereScope scope{};
        scope.SourcePc = pc;
        scope.ReturnPc = state.r[14];
        scope.BeginAddressValid = memory.Read32(kPicaCommandListCursorAddress, &scope.BeginAddress);
        mActiveUiScope = std::move(scope);
        ++mStats.UiScopeEntries;
        return;
    }
    if (mActiveUiScope && pc == mActiveUiScope->ReturnPc) {
        const auto scope = std::move(*mActiveUiScope);
        mActiveUiScope.reset();
        uint32_t end = 0;
        if (scope.BeginAddressValid && memory.Read32(kPicaCommandListCursorAddress, &end) &&
            end > scope.BeginAddress && !(end & 7U) && !(scope.BeginAddress & 7U)) {
            if (RecordPendingSpan({scope.BeginAddress, end,
                    {Oot3dPicaCompositionLayer::Ui, Oot3dPicaCompositionProvenance::NativeUiLifecycle,
                     scope.SourcePc, 0U}})) ++mStats.UiCommandSpans;
        }
        ++mStats.UiScopeExits;
        return;
    }
    if (const auto* descriptor = FindAtmosphereScope(pc);
        descriptor != nullptr) {
        // The shared primitive writer services UI as well as environment draws.
        // Preserve the outer owner instead of relabelling its packets atmosphere.
        if (mActiveUiScope) return;
        if (mActiveAtmosphereScope.has_value()) {
            ++mStats.NestedAtmosphereScopeEntries;
            return;
        }
        if (!Contains(descriptor->ReturnPcs, state.r[14])) {
            if (pc == kOot3dRendererCommandListExecuteEntry) {
                ++mStats.IgnoredUnrelatedCommandListExecutions;
            } else {
                ++mStats.InvalidAtmosphereReturnAddresses;
            }
            return;
        }
        ActiveAtmosphereScope scope{};
        scope.SourcePc = pc;
        scope.ReturnPc = state.r[14];
        scope.NativeValue =
            AtmosphereNativeValue(pc, state, memory, mStats);
        scope.BeginAddressValid = memory.Read32(
            kPicaCommandListCursorAddress, &scope.BeginAddress);
        if (!scope.BeginAddressValid) {
            ++mStats.ReadFailures;
        }
        mActiveAtmosphereScope = std::move(scope);
        ++mStats.AtmosphereScopeEntries;
        return;
    }

    if (mActiveAtmosphereScope.has_value() &&
        pc == mActiveAtmosphereScope->ReturnPc) {
        auto scope = std::move(*mActiveAtmosphereScope);
        mActiveAtmosphereScope.reset();
        uint32_t endAddress = 0U;
        const bool endAddressValid =
            memory.Read32(kPicaCommandListCursorAddress, &endAddress);
        const bool rangeValid =
            scope.BeginAddressValid && endAddressValid &&
            (scope.BeginAddress & 7U) == 0U &&
            (endAddress & 7U) == 0U && endAddress >= scope.BeginAddress;
        if (!endAddressValid) {
            ++mStats.ReadFailures;
        }
        bool directSpanRecorded = false;
        if (rangeValid && endAddress > scope.BeginAddress) {
            Oot3dPicaCommandListCompositionSpan span{
                scope.BeginAddress, endAddress,
                AttributionForAtmosphere(scope.SourcePc,
                                         scope.NativeValue)};
            if (RecordPendingSpan(std::move(span))) {
                ++mStats.DirectAtmosphereSpans;
                mStats.DirectAtmosphereBytes +=
                    endAddress - scope.BeginAddress;
                directSpanRecorded = true;
            }
        }
        if (!rangeValid) {
            ++mStats.InvalidAtmosphereCommandRanges;
        }
        if ((!rangeValid || !directSpanRecorded) &&
            !scope.PacketSpans.empty()) {
            for (auto& span : scope.PacketSpans) {
                if (RecordPendingSpan(std::move(span))) {
                    ++mStats.RecoveredAtmospherePacketSpans;
                }
            }
        }
        ++mStats.AtmosphereScopeExits;
        return;
    }

    if (mActiveCmbReturnPc.has_value() && pc == *mActiveCmbReturnPc) {
        mActiveCmbPass.reset();
        mActiveCmbReturnPc.reset();
        ++mStats.CmbPassExits;
        return;
    }

    if (pc == kOot3dCmbRendererSubmitDrawHandleEntry) {
        const uint32_t pass = state.r[1];
        if (pass > 1U || !Contains(kCmbReturnPcs, state.r[14])) {
            mActiveCmbPass.reset();
            mActiveCmbReturnPc.reset();
            if (pass > 1U) {
                ++mStats.InvalidCmbPassEntries;
            } else {
                ++mStats.InvalidCmbReturnAddresses;
            }
            return;
        }
        mActiveCmbPass = static_cast<uint8_t>(pass);
        mActiveCmbReturnPc = state.r[14];
        if (pass == 0U) {
            ++mStats.CmbOpaquePassEntries;
        } else {
            ++mStats.CmbTransparentPassEntries;
        }
        return;
    }

    if (mActivePrimitivePacket.has_value() &&
        pc == mActivePrimitivePacket->ReturnPc) {
        auto packet = std::move(*mActivePrimitivePacket);
        mActivePrimitivePacket.reset();
        uint32_t endAddress = 0U;
        const bool endAddressValid =
            packet.ContextAddress <=
                std::numeric_limits<uint32_t>::max() - 8U &&
            memory.Read32(packet.ContextAddress + 8U, &endAddress);
        if (!endAddressValid) {
            ++mStats.ReadFailures;
        }
        const bool rangeValid =
            packet.BeginAddressValid && endAddressValid &&
            (packet.BeginAddress & 7U) == 0U &&
            (endAddress & 7U) == 0U && endAddress > packet.BeginAddress;
        if (!rangeValid) {
            ++mStats.InvalidPrimitivePacketRanges;
        } else if (RecordPendingSpan(
                       {packet.BeginAddress, endAddress,
                        packet.Attribution})) {
            ++mStats.PrimitivePacketSpans;
            mStats.PrimitivePacketBytes +=
                endAddress - packet.BeginAddress;
        }
        ++mStats.PrimitivePacketExits;
        return;
    }
    if (Contains(kPrimitivePacketReturnPcs, pc)) {
        ++mStats.UnmatchedPrimitivePacketExits;
        return;
    }
    if (pc == kOot3dPicaPrimitivePacketBuilderEntry) {
        if (mActivePrimitivePacket.has_value()) {
            ++mStats.NestedPrimitivePacketEntries;
            return;
        }
        if (!Contains(kPrimitivePacketReturnPcs, state.r[14])) {
            ++mStats.InvalidPrimitivePacketReturnAddresses;
            return;
        }
        ActivePrimitivePacket packet{};
        packet.ReturnPc = state.r[14];
        packet.BeginAddressValid =
            memory.Read32(state.r[0], &packet.ContextAddress) &&
            packet.ContextAddress <=
                std::numeric_limits<uint32_t>::max() - 8U &&
            memory.Read32(packet.ContextAddress + 8U,
                          &packet.BeginAddress);
        if (!packet.BeginAddressValid) {
            ++mStats.ReadFailures;
        }
        if (mActiveUiScope) {
            packet.Attribution = {
                Oot3dPicaCompositionLayer::Ui,
                Oot3dPicaCompositionProvenance::NativeUiLifecycle,
                mActiveUiScope->SourcePc, 0U};
        } else if (mActiveAtmosphereScope.has_value()) {
            packet.Attribution = AttributionForAtmosphere(
                mActiveAtmosphereScope->SourcePc,
                mActiveAtmosphereScope->NativeValue);
        } else if (mActiveCmbPass.has_value()) {
            packet.Attribution = AttributionForCmbPass(*mActiveCmbPass);
        } else {
            packet.Attribution = {
                Oot3dPicaCompositionLayer::Unknown,
                Oot3dPicaCompositionProvenance::NativeControlFlow,
                kOot3dPicaPrimitivePacketBuilderEntry,
                state.r[14],
            };
        }
        mActivePrimitivePacket = std::move(packet);
        ++mStats.PrimitivePacketEntries;
        return;
    }
    if (pc != kOot3dMeshCommandPacketSubmitEntry) {
        return;
    }

    ++mStats.MeshPacketEntries;
    uint32_t byteCount = 0U;
    uint32_t cursor = 0U;
    if (!memory.Read32(state.r[0] + kMeshPacketByteCountOffset,
                       &byteCount) ||
        !memory.Read32(kPicaCommandListCursorAddress, &cursor)) {
        ++mStats.ReadFailures;
        return;
    }
    const int32_t signedByteCount = static_cast<int32_t>(byteCount);
    if (signedByteCount <= 0) {
        return;
    }
    const uint64_t end = static_cast<uint64_t>(cursor) + byteCount;
    if ((cursor & 7U) != 0U || (byteCount & 7U) != 0U ||
        end > std::numeric_limits<uint32_t>::max()) {
        ++mStats.InvalidPacketSpans;
        return;
    }
    if (!mActiveAtmosphereScope.has_value() &&
        !mActiveCmbPass.has_value()) {
        ++mStats.UnattributedPacketSpans;
        return;
    }

    Oot3dPicaCommandListCompositionSpan span{
        cursor, static_cast<uint32_t>(end), {}};
    if (mActiveAtmosphereScope.has_value()) {
        span.Attribution = AttributionForAtmosphere(
            mActiveAtmosphereScope->SourcePc,
            mActiveAtmosphereScope->NativeValue);
        ++mStats.AtmospherePacketSpans;
        ++mStats.RecordedPacketSpans;
        mActiveAtmosphereScope->PacketSpans.push_back(std::move(span));
        return;
    } else {
        span.Attribution = AttributionForCmbPass(*mActiveCmbPass);
    }
    if (RecordPendingSpan(std::move(span))) {
        ++mStats.RecordedPacketSpans;
    }
}

bool Oot3dNativePicaCompositionTracker::TakeCommandListCompositionSpans(
    uint32_t commandListAddress, uint32_t commandListSize,
    std::vector<Oot3dPicaCommandListCompositionSpan>& spans,
    std::string* error) {
    spans.clear();
    const uint64_t end64 =
        static_cast<uint64_t>(commandListAddress) + commandListSize;
    if ((commandListAddress & 3U) != 0U ||
        (commandListSize & 3U) != 0U ||
        end64 > std::numeric_limits<uint32_t>::max()) {
        SetError(error, "submitted command-list range is invalid");
        return false;
    }
    const uint32_t commandListEnd = static_cast<uint32_t>(end64);
    for (auto it = mPendingSpans.begin(); it != mPendingSpans.end();) {
        const bool intersects = it->BeginAddress < commandListEnd &&
                                commandListAddress < it->EndAddress;
        if (!intersects) {
            ++it;
            continue;
        }
        const bool contained = it->BeginAddress >= commandListAddress &&
                               it->EndAddress <= commandListEnd;
        if (contained) {
            spans.push_back(*it);
            ++mStats.ConsumedPacketSpans;
        } else {
            ++mStats.PartiallyOverlappingSubmissions;
        }
        it = mPendingSpans.erase(it);
    }
    std::sort(spans.begin(), spans.end(),
              [](const auto& left, const auto& right) {
                  return left.BeginAddress < right.BeginAddress;
              });
    return true;
}

void Oot3dNativePicaCompositionTracker::Reset() noexcept {
    mActiveUiScope.reset();
    mActiveCmbPass.reset();
    mActiveCmbReturnPc.reset();
    mActiveAtmosphereScope.reset();
    mActivePrimitivePacket.reset();
    mPendingSpans.clear();
    mActiveModelDraw.reset();
    mModelSpans.clear();
    mPartSpans.clear();
    mModelListBase = 0U;
    mModelListCursor = 0U;
    mModelListSerials.clear();
    mStats = {};
}

const Oot3dNativePicaCompositionStats&
Oot3dNativePicaCompositionTracker::Stats() const noexcept {
    return mStats;
}

} // namespace Oot3dNativeGame
