"""Exercise production grass allocation, pressure reclamation and failure paths."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


class GrassBufferLifecycleTests(unittest.TestCase):
    def test_production_allocations(self):
        source = (ROOT / "src/Features/GrassOptimizations/GrassBucketRenderer.cpp").read_text(encoding="utf-8")
        methods = "\n".join(block(source, signature) for signature in (
            "bool GrassBucketRenderer::Impl::EnsureRecords(",
            "bool GrassBucketRenderer::Impl::MakeRecordRoom(",
            "bool GrassBucketRenderer::Impl::EnsureResult(",
            "void GrassBucketRenderer::Impl::PrepareRecordPool(",
            "void GrassBucketRenderer::Impl::PrepareCullBuffers(",
            "void GrassBucketRenderer::Impl::RetireCulling("))
        driver = r'''
#include "Features/GrassOptimizations/GrassPolicy.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using UINT = uint32_t;
constexpr uint32_t kCullArgumentBytes = 192;
constexpr uint64_t MiB = 1024 * 1024;
namespace globals { struct State { uint32_t frameCount = 0; }; State storage; State* state = &storage; }
namespace logger { unsigned warnings = 0; void warn(const char*, const char*) { ++warnings; } }
constexpr uint32_t D3D11_BIND_SHADER_RESOURCE = 1, D3D11_BIND_VERTEX_BUFFER = 2;
constexpr uint32_t D3D11_BIND_UNORDERED_ACCESS = 4, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS = 8;
struct D3D11_BOX { UINT left, top, front, right, bottom, back; };
struct Buffer {
    static inline uint64_t aliveBytes = 0, peakBytes = 0;
    struct { uint32_t ByteWidth; } desc;
    struct Resource { Buffer* value; Buffer* get() const { return value; } } resource;
    explicit Buffer(uint32_t size) : desc{size}, resource{this} {
        aliveBytes += size; peakBytes = std::max(peakBytes, aliveBytes);
    }
    ~Buffer() { aliveBytes -= desc.ByteWidth; }
};
unsigned allocations = 0, failAllocation = 0;
std::unique_ptr<Buffer> Allocate(uint32_t bytes) {
    if (++allocations == failAllocation) throw std::runtime_error("injected allocation failure");
    return std::make_unique<Buffer>(bytes);
}
auto RawBuffer(uint32_t bytes, uint32_t, uint32_t, const char*) { return Allocate(bytes); }
template<class T> auto Structured(uint32_t count, bool, bool, const char*) { return Allocate(count * uint32_t(sizeof(T))); }
namespace GrassInstancePacking { constexpr uint32_t kStride = 16; }
struct Source { bool compactRecords = false; };
struct RecordAllocation { uint32_t base = UINT32_MAX, stride = 32; };
struct CullResult { uint32_t capacity = 0, slots = 0; };
struct CullBuffers {
    std::unique_ptr<Buffer> output, extras, arguments;
    uint32_t capacity = 0, argumentCapacity = 0;
    GrassPolicy::CullCapacityRetention retention;
};
struct Bucket {
    std::vector<Source> sources{{}};
    uint32_t RecordStride() const { return records ? records->stride : 32; }
    uint32_t RecordUnits() const { return recordsCapacity * RecordStride() / 16; }
    std::unique_ptr<RecordAllocation> records;
    uint32_t recordsCapacity = 0, instances = 0, dirtyFirstInstance = 0;
    bool recordsDirty = true;
    CullResult result;
};
struct Copy { UINT source, destination, bytes; };
struct Context {
    std::vector<Copy> copies;
    void CopySubresourceRegion(Buffer* target, UINT, UINT offset, UINT, UINT, Buffer* source, UINT, const D3D11_BOX* box) {
        if (box->right > source->desc.ByteWidth || offset + box->right - box->left > target->desc.ByteWidth)
            throw std::runtime_error("Record relocation exceeds pool bounds");
        copies.push_back({box->left, offset, box->right - box->left});
    }
};
struct GrassBucketRenderer { struct Impl {
    uint64_t residentRecordBytes = 0, resultBytes = 0;
    uint32_t frame = 0;
    bool recordLayoutDirty = true;
    std::vector<Bucket> buckets, dormantBuckets;
    std::unique_ptr<Buffer> recordPool;
    std::unique_ptr<Context> context = std::make_unique<Context>();
    CullBuffers cullBuffers;
    std::vector<Bucket*> cullingBuckets;
    std::atomic_uint64_t publishedResultBytes{0};
    bool EnsureRecords(Bucket&);
    bool MakeRecordRoom(uint64_t);
    bool EnsureResult(Bucket&, uint32_t, uint32_t);
    void PrepareRecordPool();
    void PrepareCullBuffers(uint32_t, uint32_t);
    void RetireCulling();
}; };
PRODUCTION_METHODS
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void ResetProbe() { allocations = failAllocation = 0; Buffer::peakBytes = Buffer::aliveBytes; }
int main() {
    try {
        {
            GrassBucketRenderer::Impl state;
            state.buckets.resize(2);
            for (auto& bucket : state.buckets) {
                bucket.instances = 65;
                Require(state.EnsureRecords(bucket), "Record reservation failed");
            }
            Require(!allocations, "Record reservation must not allocate GPU storage");
            state.PrepareRecordPool();
            Require(state.buckets[0].records->base == 0 && state.buckets[1].records->base == 256,
                "Pool did not assign disjoint capacity ranges");
            state.buckets[0].recordsDirty = false;
            state.buckets[1].dirtyFirstInstance = 32;
            auto* retained = state.recordPool.get();
            ResetProbe();
            for (unsigned i = 0; i < 1000; ++i) state.PrepareRecordPool();
            Require(!allocations && state.recordPool.get() == retained && state.context->copies.empty(),
                "Stable frames must neither copy records nor reallocate");
            state.buckets.emplace_back(); state.buckets.back().instances = 1;
            Require(state.EnsureRecords(state.buckets.back()), "Streaming reservation failed");
            failAllocation = 1;
            bool failed = false;
            try { state.PrepareRecordPool(); } catch (const std::runtime_error&) { failed = true; }
            Require(failed, "Injected pool failure was ignored");
            Require(state.recordPool.get() == retained && state.buckets.back().records->base == UINT32_MAX,
                "Failed pool growth changed a retained allocation");
            ResetProbe(); state.PrepareRecordPool();
            Require(state.context->copies.size() == 2 && state.context->copies[0].bytes == 65 * 32 &&
                state.context->copies[1].bytes == 32 * 32 && state.buckets.back().records->base == 512,
                "Pool relocation did not preserve exactly the clean prefixes");
            Require(state.buckets[1].recordsDirty && state.buckets[1].dirtyFirstInstance == 32,
                "Relocation lost the dirty upload suffix");
            const auto copies = state.context->copies.size();
            state.residentRecordBytes -= state.buckets.front().recordsCapacity * GrassPolicy::kRecordBytes;
            state.buckets.erase(state.buckets.begin());
            state.buckets.emplace_back(); state.buckets.back().instances = 128;
            Require(state.EnsureRecords(state.buckets.back()), "Gap reuse reservation failed");
            ResetProbe(); state.PrepareRecordPool();
            Require(!allocations && state.context->copies.size() == copies && state.buckets.back().records->base == 0 &&
                    state.buckets.back().recordsDirty && state.buckets.back().dirtyFirstInstance == 0,
                "Streaming gap reuse reallocated, copied retained records, or lost the new upload");
        }
        {
            GrassBucketRenderer::Impl state;
            state.buckets.resize(2);
            for (auto& bucket : state.buckets) bucket.instances = 1;
            state.buckets[0].sources[0].compactRecords = true;
            for (auto& bucket : state.buckets) Require(state.EnsureRecords(bucket), "Mixed reservation failed");
            state.PrepareRecordPool();
            Require(state.residentRecordBytes == 48 && state.buckets[1].records->base == 1,
                "Mixed pool lost its 16-byte allocation units");
            for (auto& bucket : state.buckets) bucket.recordsDirty = false;
            state.buckets.emplace_back(); state.buckets.back().instances = 1;
            Require(state.EnsureRecords(state.buckets.back()), "Mixed growth reservation failed");
            state.PrepareRecordPool();
            Require(state.context->copies.size() == 2 && state.context->copies[0].bytes == 16 &&
                state.context->copies[1].bytes == 32 && state.context->copies[1].source == 16,
                "Mixed relocation changed a source stride");
            state.buckets[0].sources[0].compactRecords = false;
            Require(state.EnsureRecords(state.buckets[0]) && state.residentRecordBytes == 96 &&
                state.buckets[0].recordsDirty && !state.buckets[0].dirtyFirstInstance,
                "Compact-to-raw transition reused encoded records");
            state.PrepareRecordPool();
            state.buckets[0].sources[0].compactRecords = true;
            Require(state.EnsureRecords(state.buckets[0]) && state.residentRecordBytes == 80,
                "Raw-to-compact transition underflowed reservation accounting");
            state.PrepareRecordPool();
        }
        {
            GrassBucketRenderer::Impl state;
            state.buckets.resize(8);
            for (auto& bucket : state.buckets) {
                bucket.instances = GrassPolicy::kMaxBatchInstances;
                Require(state.EnsureRecords(bucket), "Large reservation failed");
            }
            state.PrepareRecordPool(); state.buckets[0].recordsDirty = false;
            state.buckets.emplace_back(); state.buckets.back().instances = 1;
            Require(state.EnsureRecords(state.buckets.back()), "Pressure reservation failed");
            ResetProbe(); state.PrepareRecordPool();
            Require(Buffer::peakBytes <= GrassPolicy::kMaxResidentRecordBytes && state.buckets[0].recordsDirty &&
                state.buckets[0].dirtyFirstInstance == 0, "Pressure rebuild exceeded budget or retained discarded data");
            state.dormantBuckets.push_back(std::move(state.buckets[0])); state.buckets.erase(state.buckets.begin());
            Require(state.MakeRecordRoom(70 * MiB) && state.dormantBuckets.empty(),
                "Dormant reservations prevented active growth");
        }
        for (unsigned failure = 1; failure <= 3; ++failure) {
            GrassBucketRenderer::Impl state;
            ResetProbe(); failAllocation = failure;
            bool threw = false;
            try { state.PrepareCullBuffers(2048, 384); } catch (const std::runtime_error&) { threw = true; }
            Require(threw && !state.cullBuffers.output && !Buffer::aliveBytes,
                "Partial culling allocation leaked or was published");
        }
        {
            GrassBucketRenderer::Impl state;
            ResetProbe(); state.PrepareCullBuffers(65536, 384);
            auto* retained = state.cullBuffers.output.get();
            ResetProbe();
            for (state.frame = 1; state.frame < GrassPolicy::kCullResultTrimFrames; ++state.frame)
                state.PrepareCullBuffers(128, 192);
            Require(!allocations && state.cullBuffers.output.get() == retained, "Outputs shrank during transient low demand");
            failAllocation = 1;
            state.PrepareCullBuffers(128, 192);
            Require(state.cullBuffers.output.get() == retained && logger::warnings, "Optional shrink failure lost usable output");
            ++state.frame; ResetProbe(); state.PrepareCullBuffers(128, 192);
            Require(state.cullBuffers.capacity == 256 && state.resultBytes == Buffer::aliveBytes,
                "Sustained demand did not reclaim shared output storage");
            Bucket bucket; bucket.instances = 100;
            Require(!state.EnsureResult(bucket, 101, 2) && !state.EnsureResult(bucket, 1, 7), "Invalid per-bucket layout was admitted");
            Require(state.EnsureResult(bucket, 0, 2) && !bucket.result.capacity, "Empty bucket retained output slots");
            bool bounded = false;
            try { state.PrepareCullBuffers(UINT32_MAX, 192); } catch (const std::length_error&) { bounded = true; }
            Require(bounded, "Oversized culling output was admitted");
        }
        {
            GrassBucketRenderer::Impl state;
            ResetProbe(); state.PrepareCullBuffers(4096, 192);
            state.frame = globals::state->frameCount = 1;
            state.PrepareCullBuffers(4096, 192);
            state.cullingBuckets.push_back(nullptr);
            state.RetireCulling();
            Require(state.cullBuffers.output && state.cullingBuckets.empty(),
                "End-of-frame retirement released active output or retained bucket pointers");
            ResetProbe();
            for (uint32_t frame = 2; frame <= GrassPolicy::kCullResultTrimFrames; ++frame) {
                globals::state->frameCount = frame;
                state.RetireCulling();
            }
            Require(state.cullBuffers.output && !allocations, "Transient idle frames released shared output");
            ++globals::state->frameCount; state.RetireCulling();
            Require(!state.cullBuffers.output && !state.resultBytes && !Buffer::aliveBytes && !allocations,
                "Sustained idle frames retained shared output storage");
            state.frame = globals::state->frameCount + 1;
            state.PrepareCullBuffers(1, 192);
            Require(state.cullBuffers.output && state.cullBuffers.capacity == 1,
                "Visible grass could not regrow idle output");
        }
        Require(!Buffer::aliveBytes, "Shared pool owners leaked");
        std::cout << "Production shared grass allocation and relocation checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''.replace("PRODUCTION_METHODS", methods)
        compiler = os.environ.get("CXX") or shutil.which("cl")
        self.assertIsNotNone(compiler, "Run in the MSVC developer environment")
        with tempfile.TemporaryDirectory(prefix="grass-buffer-lifecycle-") as temporary:
            directory = Path(temporary)
            cpp = directory / "driver.cpp"
            cpp.write_text(driver, encoding="utf-8")
            for devbench in (False, True):
                executable = directory / ("devbench.exe" if devbench else "production.exe")
                command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX",
                           "/I" + str(ROOT / "src"), str(cpp), "/Fe:" + str(executable)]
                if devbench:
                    command.append("/DDEVBENCH_BRIDGE_ENABLED")
                result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=300)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(executable)], cwd=directory, capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
