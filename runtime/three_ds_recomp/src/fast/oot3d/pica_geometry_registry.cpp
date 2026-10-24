#include "fast/oot3d/pica_geometry_registry.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>

namespace Fast::Oot3d {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

template <typename Value> void HashValue(uint64_t& hash, const Value& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    for (size_t index = 0; index < sizeof(Value); ++index) {
        hash = (hash ^ bytes[index]) * kFnvPrime;
    }
}

bool AlignPayload(std::vector<uint8_t>& payload, size_t alignment, uint64_t& offset) {
    if (alignment == 0U || payload.size() > std::numeric_limits<size_t>::max() - (alignment - 1U)) {
        return false;
    }
    const size_t aligned = (payload.size() + alignment - 1U) & ~(alignment - 1U);
    payload.resize(aligned);
    offset = aligned;
    return true;
}

bool AppendPayload(std::vector<uint8_t>& payload, std::span<const uint8_t> bytes, size_t alignment, uint64_t& offset) {
    if (!AlignPayload(payload, alignment, offset) ||
        bytes.size() > std::numeric_limits<size_t>::max() - payload.size()) {
        return false;
    }
    payload.insert(payload.end(), bytes.begin(), bytes.end());
    return true;
}

uint64_t StructuralSignature(const PicaGeometryRequest& request) {
    uint64_t hash = kFnvOffset;
    HashValue(hash, request.Indexed);
    HashValue(hash, request.IndicesAre16Bit);
    HashValue(hash, request.VertexCount);
    HashValue(hash, request.BuildPackedStreams);
    for (const auto& stream : request.VertexStreams) {
        HashValue(hash, stream.Binding.Binding);
        HashValue(hash, stream.Binding.ByteStride);
        HashValue(hash, stream.Binding.PerInstance);
        HashValue(hash, stream.Bytes.size());
    }
    for (const auto& attribute : request.VertexAttributes) {
        HashValue(hash, attribute.Location);
        HashValue(hash, attribute.Binding);
        HashValue(hash, attribute.Scalar);
        HashValue(hash, attribute.ComponentCount);
        HashValue(hash, attribute.ByteOffset);
    }
    HashValue(hash, request.IndexBytes.size());
    return hash == 0U ? 1U : hash;
}

// The fields StructuralSignature hashes, raw (with the stream and attribute
// counts): equal fields give the signature already stored with an entry, so a
// cache hit needs no byte-wise hash.
void StructuralFields(const PicaGeometryRequest& request, std::vector<uint64_t>& fields) {
    fields.clear();
    fields.push_back(request.Indexed);
    fields.push_back(request.IndicesAre16Bit);
    fields.push_back(request.VertexCount);
    fields.push_back(request.BuildPackedStreams);
    fields.push_back(request.VertexStreams.size());
    for (const auto& stream : request.VertexStreams) {
        fields.push_back(stream.Binding.Binding);
        fields.push_back(stream.Binding.ByteStride);
        fields.push_back(stream.Binding.PerInstance);
        fields.push_back(stream.Bytes.size());
    }
    fields.push_back(request.VertexAttributes.size());
    for (const auto& attribute : request.VertexAttributes) {
        fields.push_back(attribute.Location);
        fields.push_back(attribute.Binding);
        fields.push_back(static_cast<uint64_t>(attribute.Scalar));
        fields.push_back(attribute.ComponentCount);
        fields.push_back(attribute.ByteOffset);
    }
    fields.push_back(request.IndexBytes.size());
}

bool BuildPreparedGeometry(const PicaGeometryRequest& request, uint64_t structuralSignature,
                           PicaPreparedGeometry& prepared) {
    prepared = {};
    prepared.Identity = request.Identity;
    prepared.ContentVersion = request.ContentVersion;
    prepared.StructuralSignature = structuralSignature;
    prepared.LastUsedFrame = request.FrameId;
    prepared.Indexed = request.Indexed;
    prepared.VertexOrIndexCount = request.VertexCount;
    prepared.SourceBindings.reserve(request.VertexStreams.size());
    for (const auto& stream : request.VertexStreams) {
        if (stream.Binding.ByteStride == 0U || stream.Bytes.empty()) {
            return false;
        }
        uint64_t offset = 0;
        if (!AppendPayload(prepared.Payload, stream.Bytes, 16U, offset)) {
            return false;
        }
        prepared.SourceBindings.push_back(
            { stream.Binding.Binding, offset, stream.Bytes.size(), stream.Binding.ByteStride });
    }

    if (request.BuildPackedStreams) {
        std::vector<PicaNriPackedVertexStream> packedStreams;
        prepared.PackedValid = BuildPicaNriPackedVertexStreams(request.VertexStreams, request.VertexAttributes,
                                                               packedStreams, &prepared.PackedError);
        if (prepared.PackedValid) {
            prepared.PackedBindings.reserve(packedStreams.size());
            for (const auto& stream : packedStreams) {
                uint64_t offset = 0;
                if (!AppendPayload(prepared.Payload, stream.Bytes, 16U, offset)) {
                    return false;
                }
                prepared.PackedBindings.push_back({ stream.Binding, offset, stream.Bytes.size(), stream.ByteStride });
            }
        }
    }

    if (!request.Indexed) {
        return request.VertexCount != 0U;
    }
    const size_t indexSize = request.IndicesAre16Bit ? 2U : 1U;
    if (request.IndexBytes.empty() || request.IndexBytes.size() % indexSize != 0U) {
        return false;
    }
    if (!AlignPayload(prepared.Payload, 4U, prepared.IndexOffset)) {
        return false;
    }
    if (request.IndicesAre16Bit) {
        if (request.IndexBytes.size() > std::numeric_limits<size_t>::max() - prepared.Payload.size()) {
            return false;
        }
        prepared.Payload.insert(prepared.Payload.end(), request.IndexBytes.begin(), request.IndexBytes.end());
    } else {
        if (request.IndexBytes.size() >
            (std::numeric_limits<size_t>::max() - prepared.Payload.size()) / sizeof(uint16_t)) {
            return false;
        }
        const size_t byteOffset = prepared.Payload.size();
        prepared.Payload.resize(byteOffset + request.IndexBytes.size() * sizeof(uint16_t));
        for (size_t index = 0; index < request.IndexBytes.size(); ++index) {
            const uint16_t expanded = request.IndexBytes[index];
            std::memcpy(prepared.Payload.data() + byteOffset + index * sizeof(uint16_t), &expanded, sizeof(expanded));
        }
    }
    const size_t indexCount = request.IndexBytes.size() / indexSize;
    if (indexCount > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    prepared.VertexOrIndexCount = static_cast<uint32_t>(indexCount);
    return prepared.VertexOrIndexCount != 0U;
}

} // namespace

struct PicaGeometryRegistry::Impl {
    explicit Impl(size_t capacity) : Capacity(std::max<size_t>(capacity, 1U)) {
    }

    struct Entry {
        PicaPreparedGeometry Prepared;
        std::vector<uint64_t> Structure;
    };

    size_t Capacity = 1U;
    std::unordered_map<uint64_t, Entry> Entries;
    std::vector<uint64_t> RequestStructure;
    PicaPreparedGeometry Transient;
    std::vector<uint64_t> EvictedIdentities;
    PicaGeometryRegistryStats Statistics;
};

PicaGeometryRegistry::PicaGeometryRegistry(size_t capacity) : mImpl(std::make_unique<Impl>(capacity)) {
}

PicaGeometryRegistry::~PicaGeometryRegistry() = default;

PicaGeometryResolveResult PicaGeometryRegistry::Resolve(const PicaGeometryRequest& request) {
    const bool cacheable = request.IdentityAvailable && request.Identity != 0U && request.ContentVersion != 0U;
    if (!cacheable) {
        ++mImpl->Statistics.DynamicBuilds;
        if (!BuildPreparedGeometry(request, StructuralSignature(request), mImpl->Transient)) {
            return {};
        }
        if (request.BuildPackedStreams && !mImpl->Transient.PackedValid) {
            ++mImpl->Statistics.PackedBuildFailures;
        }
        return { &mImpl->Transient, false, false };
    }

    StructuralFields(request, mImpl->RequestStructure);
    auto found = mImpl->Entries.find(request.Identity);
    if (found != mImpl->Entries.end() && found->second.Prepared.ContentVersion == request.ContentVersion &&
        found->second.Structure == mImpl->RequestStructure) {
        found->second.Prepared.LastUsedFrame = request.FrameId;
        ++mImpl->Statistics.Hits;
        mImpl->Statistics.Entries = mImpl->Entries.size();
        return { &found->second.Prepared, true, true };
    }

    const uint64_t structuralSignature = StructuralSignature(request);
    PicaPreparedGeometry prepared;
    if (!BuildPreparedGeometry(request, structuralSignature, prepared)) {
        return {};
    }
    if (request.BuildPackedStreams && !prepared.PackedValid) {
        ++mImpl->Statistics.PackedBuildFailures;
    }
    ++mImpl->Statistics.Misses;
    if (found != mImpl->Entries.end()) {
        ++mImpl->Statistics.Updates;
        found->second = { std::move(prepared), mImpl->RequestStructure };
    } else {
        if (mImpl->Entries.size() >= mImpl->Capacity) {
            const auto oldest =
                std::min_element(mImpl->Entries.begin(), mImpl->Entries.end(), [](const auto& left, const auto& right) {
                    return left.second.Prepared.LastUsedFrame < right.second.Prepared.LastUsedFrame;
                });
            if (oldest != mImpl->Entries.end()) {
                mImpl->EvictedIdentities.push_back(oldest->first);
                mImpl->Entries.erase(oldest);
                ++mImpl->Statistics.Evictions;
            }
        }
        found = mImpl->Entries.emplace(request.Identity, Impl::Entry{ std::move(prepared), mImpl->RequestStructure })
                    .first;
    }
    mImpl->Statistics.Entries = mImpl->Entries.size();
    return { &found->second.Prepared, false, true };
}

std::vector<uint64_t> PicaGeometryRegistry::TakeEvictedIdentities() {
    auto evicted = std::move(mImpl->EvictedIdentities);
    mImpl->EvictedIdentities.clear();
    return evicted;
}

PicaGeometryRegistryStats PicaGeometryRegistry::Stats() const {
    auto stats = mImpl->Statistics;
    stats.Entries = mImpl->Entries.size();
    return stats;
}

void PicaGeometryRegistry::Clear() {
    mImpl->Entries.clear();
    mImpl->Transient = {};
    mImpl->EvictedIdentities.clear();
    mImpl->Statistics = {};
}

} // namespace Fast::Oot3d
