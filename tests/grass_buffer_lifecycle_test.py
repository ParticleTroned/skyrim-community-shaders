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
            "bool GrassBucketRenderer::Impl::EnsureResult("))
        driver = r'''
#include "Features/GrassOptimizations/GrassPolicy.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
constexpr uint64_t MiB = 1024 * 1024;
namespace logger {
    unsigned warnings = 0;
    void warn(const char*, const char*) { ++warnings; }
}
constexpr uint32_t D3D11_BIND_SHADER_RESOURCE = 1, D3D11_BIND_VERTEX_BUFFER = 2;
constexpr uint32_t D3D11_BIND_UNORDERED_ACCESS = 4, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS = 8;
struct Buffer {
    static inline uint64_t aliveBytes = 0, peakBytes = 0;
    uint64_t bytes;
    explicit Buffer(uint64_t size) : bytes(size) {
        aliveBytes += bytes;
        peakBytes = std::max(peakBytes, aliveBytes);
    }
    ~Buffer() { aliveBytes -= bytes; }
};
unsigned allocations = 0, failAllocation = 0;
std::unique_ptr<Buffer> Allocate(uint64_t bytes) {
    if (++allocations == failAllocation) throw std::runtime_error("injected allocation failure");
    return std::make_unique<Buffer>(bytes);
}
auto RawBuffer(uint32_t bytes, uint32_t, uint32_t, const char*) { return Allocate(bytes); }
template<class T> auto Structured(uint32_t count, bool, bool, const char*) { return Allocate(uint64_t(count) * sizeof(T)); }
struct CullResult {
    std::unique_ptr<Buffer> output, extras, arguments;
    uint32_t capacity = 0, slots = 0;
    GrassPolicy::CullCapacityRetention retention;
};
struct Bucket {
    std::unique_ptr<Buffer> records;
    uint32_t recordsCapacity = 0, instances = 0, dirtyFirstInstance = 17;
    bool recordsDirty = false;
    CullResult result;
};
struct GrassBucketRenderer {
    struct Impl {
        uint64_t residentRecordBytes = 0, resultBytes = 0;
        uint64_t lastResultTrimFrame = UINT64_MAX;
        uint32_t frame = 0;
        std::vector<Bucket> dormantBuckets;
        std::atomic_uint64_t publishedResultBytes{0};
        bool EnsureRecords(Bucket&);
        bool MakeRecordRoom(uint64_t);
        bool EnsureResult(Bucket&, uint32_t, uint32_t);
    };
};
PRODUCTION_METHODS
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Bucket Records(uint32_t bytes) {
    Bucket bucket;
    bucket.records = Allocate(bytes);
    bucket.recordsCapacity = bytes / GrassPolicy::kRecordBytes;
    return bucket;
}
void ResetAllocationProbe() { allocations = failAllocation = 0; Buffer::peakBytes = Buffer::aliveBytes; }
int main() {
    try {
        {
            GrassBucketRenderer::Impl state;
            auto bucket = Records(4096);
            bucket.instances = 64;
            state.dormantBuckets.push_back(Records(4096));
            state.residentRecordBytes = Buffer::aliveBytes;
            auto* retained = bucket.records.get();
            ResetAllocationProbe();
            for (unsigned frame = 0; frame < 1000; ++frame)
                Require(state.EnsureRecords(bucket), "Existing records should remain usable");
            Require(!allocations && bucket.records.get() == retained && !bucket.recordsDirty &&
                bucket.dirtyFirstInstance == 17 && state.dormantBuckets.size() == 1,
                "Stable frames must not allocate, dirty records or discard dormant reuse");
        }
        for (bool fail : {false, true}) {
            GrassBucketRenderer::Impl state;
            auto otherActive = Allocate(112 * MiB);
            auto bucket = Records(4 * MiB);
            bucket.instances = bucket.recordsCapacity + 1;
            state.dormantBuckets.push_back(Records(4 * MiB));
            state.dormantBuckets.push_back(Records(8 * MiB));
            state.residentRecordBytes = Buffer::aliveBytes;
            auto* retained = bucket.records.get();
            ResetAllocationProbe();
            failAllocation = fail ? 1 : 0;
            bool threw = false;
            try { Require(state.EnsureRecords(bucket), "Dormant storage must yield to active growth"); }
            catch (const std::runtime_error& error) {
                Require(std::string(error.what()) == "injected allocation failure", error.what());
                threw = true;
            }
            Require(threw == fail && allocations == 1 && state.dormantBuckets.size() == 1,
                "Pressure reclamation must release only enough inactive capacity");
            Require(Buffer::peakBytes <= GrassPolicy::kMaxResidentRecordBytes &&
                state.residentRecordBytes == Buffer::aliveBytes,
                "Old plus replacement storage must stay within the transient budget");
            if (fail) {
                Require(bucket.records.get() == retained && bucket.recordsCapacity == 4 * MiB / 32 &&
                    !bucket.recordsDirty && bucket.dirtyFirstInstance == 17,
                    "Failed replacement must preserve the active buffer and upload prefix");
                failAllocation = 0;
                Require(state.EnsureRecords(bucket), "A later allocation should recover");
            }
            Require(bucket.recordsCapacity == 8 * MiB / 32 && bucket.recordsDirty && !bucket.dirtyFirstInstance,
                "New storage must require a complete upload");
            Require(state.residentRecordBytes == Buffer::aliveBytes, "Reclaimed accounting must survive retry");
        }
        {
            GrassBucketRenderer::Impl state;
            auto otherActive = Allocate(124 * MiB);
            auto bucket = Records(4 * MiB);
            bucket.instances = bucket.recordsCapacity + 1;
            state.residentRecordBytes = Buffer::aliveBytes;
            auto* retained = bucket.records.get();
            ResetAllocationProbe();
            Require(!state.EnsureRecords(bucket) && !allocations && bucket.records.get() == retained,
                "Insufficient active-only budget must retain native fallback without allocation");
            state.dormantBuckets.emplace_back();
            Require(!state.MakeRecordRoom(8 * MiB) && state.dormantBuckets.empty(),
                "An empty dormant entry must neither free charged bytes nor stall reclamation");
            Require(state.residentRecordBytes == Buffer::aliveBytes, "Empty dormant storage changed accounting");
        }
        for (unsigned fail = 0; fail <= 3; ++fail) {
            GrassBucketRenderer::Impl state;
            Bucket bucket;
            bucket.instances = 64;
            ResetAllocationProbe();
            Require(state.EnsureResult(bucket, 64, 2), "Initial culling allocation failed");
            auto* output = bucket.result.output.get();
            auto* extras = bucket.result.extras.get();
            auto* arguments = bucket.result.arguments.get();
            const auto oldBytes = state.resultBytes;
            bucket.instances = 65;
            ResetAllocationProbe();
            failAllocation = fail;
            bool threw = false;
            try { Require(state.EnsureResult(bucket, 65, 6), "Culling growth was rejected"); }
            catch (const std::runtime_error& error) {
                Require(std::string(error.what()) == "injected allocation failure", error.what());
                threw = true;
            }
            Require(threw == (fail != 0), "Each culling component must propagate allocation failure");
            if (fail) {
                Require(bucket.result.output.get() == output && bucket.result.extras.get() == extras &&
                    bucket.result.arguments.get() == arguments && bucket.result.capacity == 64 &&
                    bucket.result.slots == 2 && state.resultBytes == oldBytes && Buffer::aliveBytes == oldBytes,
                    "Partial replacement must release new components and preserve the old complete bundle");
            } else {
                Require(bucket.result.capacity == 128 && bucket.result.slots == 6 &&
                    state.resultBytes == 128 * 6 * GrassPolicy::kSurvivorBytes + 192 &&
                    Buffer::aliveBytes == state.resultBytes,
                    "Completed culling replacement must publish matching slots and accounting");
            }
        }
        {
            GrassBucketRenderer::Impl state;
            Bucket bucket;
            bucket.instances = GrassPolicy::kMaxBatchInstances;
            ResetAllocationProbe();
            Require(state.EnsureResult(bucket, 0, 6) && !allocations && !state.resultBytes,
                "An empty candidate set allocated output storage");
            Require(state.EnsureResult(bucket, 65, 6) && bucket.result.capacity == 128 &&
                state.resultBytes == 128 * 6 * GrassPolicy::kSurvivorBytes + 192,
                "Output capacity must follow candidates, independently of resident records");
            const auto* retained = bucket.result.output.get();
            ResetAllocationProbe();
            for (state.frame = 1; state.frame < 1000; ++state.frame)
                Require(state.EnsureResult(bucket, 65, 6), "Stable candidate output became unavailable");
            Require(!allocations && bucket.result.output.get() == retained && !bucket.records,
                "Stable output sizing changed buffers or allocated source records");
            for (auto invalid : {GrassPolicy::kMaxBatchInstances + 1, UINT32_MAX})
                Require(!state.EnsureResult(bucket, invalid, 6), "Oversized candidate count was accepted");
            bucket.instances = 64;
            Require(!state.EnsureResult(bucket, 65, 6), "Candidates exceeded the resident source count");
            Require(!state.EnsureResult(bucket, 1, 0) && !state.EnsureResult(bucket, 1, 7),
                "Invalid eye/LOD slot count was accepted on the reuse path");
            Require(!allocations && bucket.result.output.get() == retained,
                "Invalid admission altered retained output");
        }
        for (unsigned fail = 0; fail <= 3; ++fail) {
            GrassBucketRenderer::Impl state;
            Bucket bucket;
            bucket.instances = 65536;
            ResetAllocationProbe();
            Require(state.EnsureResult(bucket, bucket.instances, 6), "Dense output allocation failed");
            auto* retained = bucket.result.output.get();
            const auto oldBytes = state.resultBytes;
            ResetAllocationProbe();
            for (state.frame = 1; state.frame < GrassPolicy::kCullResultTrimFrames; ++state.frame) {
                Require(state.EnsureResult(bucket, 1024, 6), "Low demand lost usable output");
                Require(state.EnsureResult(bucket, 1024, 6), "Repeated pass lost usable output");
            }
            Require(!allocations && bucket.result.output.get() == retained,
                "Output shrank before the grace period or counted paired passes twice");
            failAllocation = fail;
            logger::warnings = 0;
            Require(state.EnsureResult(bucket, 1024, 6), "Optional shrinking disabled usable rendering");
            Require(logger::warnings == (fail ? 1u : 0u), "Shrink failure was not reported");
            if (fail) {
                Require(bucket.result.output.get() == retained && bucket.result.capacity == 65536 &&
                    state.resultBytes == oldBytes && Buffer::aliveBytes == oldBytes,
                    "Failed shrink damaged the complete retained bundle or leaked partial buffers");
                failAllocation = 0;
                const auto attempted = allocations;
                for (unsigned retry = 1; retry < GrassPolicy::kCullResultTrimFrames; ++retry) {
                    ++state.frame;
                    Require(state.EnsureResult(bucket, 1024, 6), "Shrink backoff lost rendering");
                }
                Require(allocations == attempted, "Failed shrink retried without its grace period");
                ++state.frame;
                Require(state.EnsureResult(bucket, 1024, 6), "Shrink did not recover after backoff");
            }
            Require(bucket.result.capacity == 2048 && bucket.result.slots == 6 &&
                state.resultBytes == 2048 * 6 * GrassPolicy::kSurvivorBytes + 192 &&
                Buffer::aliveBytes == state.resultBytes && Buffer::peakBytes <= GrassPolicy::kMaxCullResultBytes,
                "Shrink failed to retain headroom or account for old-plus-new output");
            ++state.frame;
            Require(state.EnsureResult(bucket, 2049, 6) && bucket.result.capacity == 4096,
                "A camera turn did not grow output before dispatch");
        }
        {
            GrassBucketRenderer::Impl state;
            Bucket first, second;
            first.instances = second.instances = 4096;
            ResetAllocationProbe();
            Require(state.EnsureResult(first, 4096, 2) && state.EnsureResult(second, 4096, 2),
                "Initial idle-reclamation output failed");
            ResetAllocationProbe();
            for (state.frame = 1; state.frame <= GrassPolicy::kCullResultTrimFrames; ++state.frame) {
                Require(state.EnsureResult(first, 0, 2) && state.EnsureResult(second, 0, 2),
                    "Empty candidates must remain renderable during reclamation");
            }
            Require(!first.result.arguments && second.result.arguments && !allocations,
                "Idle outputs were not released or more than one bucket was trimmed per frame");
            Require(state.EnsureResult(second, 0, 2) && !second.result.arguments && !state.resultBytes,
                "A deferred idle bucket did not reclaim on the next frame");
            Require(state.EnsureResult(first, 1, 2) && first.result.capacity == 1,
                "Released idle output did not regrow for visible candidates");
        }
        {
            GrassBucketRenderer::Impl state;
            Bucket bucket;
            bucket.instances = 65536;
            ResetAllocationProbe();
            Require(state.EnsureResult(bucket, bucket.instances, 6), "Pressure fixture allocation failed");
            auto otherActive = Allocate(GrassPolicy::kMaxCullResultBytes - state.resultBytes);
            state.resultBytes = GrassPolicy::kMaxCullResultBytes;
            auto* retained = bucket.result.output.get();
            ResetAllocationProbe();
            for (state.frame = 1; state.frame <= GrassPolicy::kCullResultTrimFrames; ++state.frame)
                Require(state.EnsureResult(bucket, 1024, 6), "Budget-rejected optional shrink lost rendering");
            Require(!allocations && bucket.result.output.get() == retained &&
                state.resultBytes == Buffer::aliveBytes && Buffer::peakBytes == GrassPolicy::kMaxCullResultBytes,
                "Shrink exceeded replacement budget or discarded usable storage under pressure");
        }
        Require(!Buffer::aliveBytes, "Buffer owners leaked from a completed scenario");
        std::cout << "Production grass pressure reclamation, prefix reuse and allocation failure passed\n";
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
