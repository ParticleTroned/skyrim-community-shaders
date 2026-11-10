"""Exercise production grass setup, culling recovery and native wind restoration."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from skylighting_settings_test import block


ROOT = Path(__file__).resolve().parents[1]


def publication_generation_methods(header: str) -> str:
    signatures = (
        ("uint64_t LiveGeneration(", "uint64_t ShaderCache::Queue::LiveGeneration("),
        ("bool IsCurrentGeneration(RE::BSShader::Type", "bool ShaderCache::Queue::IsCurrentGeneration(RE::BSShader::Type"),
        ("bool IsTaskStale(std::optional<uint64_t> a_taskGeneration)", "bool ShaderCache::IsTaskStale(std::optional<uint64_t> a_taskGeneration)"),
        ("bool IsTaskStale(std::optional<uint64_t> a_taskGeneration,", "bool ShaderCache::IsTaskStale(std::optional<uint64_t> a_taskGeneration,"),
    )
    return "\n".join(block(header, signature).replace(signature, qualified, 1)
                     for signature, qualified in signatures)


def run_fixture(source: str, devbench: bool) -> subprocess.CompletedProcess:
    runtime = (ROOT / "src/Features/GrassOptimizations/GrassRuntime.h").read_text(encoding="utf-8")
    hooks = (ROOT / "src/Hooks.cpp").read_text(encoding="utf-8")
    methods = "namespace GrassRuntime {\n" + block(runtime, "inline void PrepareGeometryWithoutAdvancingWind(") + "\n}\n"
    methods += block(hooks, "struct BSGrassShader_SetupGeometry") + ";\n"
    methods += block(hooks, "struct BSGrassShader_RestoreGeometry") + ";\n"
    for signature in ("void GrassBucketRenderer::Impl::ClearDrawContext(",
                      "bool GrassBucketRenderer::Impl::HasActiveDrawContext("):
        if signature in source:
            methods += block(source, signature) + "\n"
    methods += "\n".join(block(source, signature) for signature in (
        "void GrassBucketRenderer::PrepareGeometry(", "void GrassBucketRenderer::DrawGroup(",
        "void GrassBucketRenderer::Impl::MakeNativeVisible(", "void GrassBucketRenderer::RemoveShape(",
        "void GrassBucketRenderer::Impl::EraseResident(",
        "void GrassBucketRenderer::Impl::UnassignQueue(",
        "std::shared_ptr<const QueuedGroup> GrassBucketRenderer::Impl::QueuedFor(",
        "bool GrassBucketRenderer::Impl::MatchesQueuedSources(",
        "void GrassBucketRenderer::Impl::PrepareCulling(", "bool GrassBucketRenderer::Impl::DrawBucket(",
        "bool GrassBucketRenderer::Impl::DrawMainPass(",
        "void GrassBucketRenderer::DrawRenderPass(",
        "void GrassBucketRenderer::FinishGeometry(",
        "void GrassBucketRenderer::EndFrame(",
        "void GrassBucketRenderer::Impl::CopyGeometryConstants(", "void GrassBucketRenderer::Impl::RestoreCurrentGeometry(",
        "bool GrassBucketRenderer::Impl::HasBatchDrawState("))
    driver = r'''
#include "grass_draw_lifecycle_fixture.h"
#include <chrono>
#include <future>
#include <thread>
PRODUCTION_METHODS
void Fixture::NativeSetup(RE::BSShader*, RE::BSRenderPass* pass, uint32_t flags) {
    auto& f = *active;
    if (!f.inOriginalSetup && f.duringNativeSetup) f.duringNativeSetup(pass);
    if (f.inOriginalSetup && f.replacePropertyOnSetup) {
        pass->shaderProperty = f.replacementProperty.get();
        f.shape.geometry.shaderProperty = f.replacementProperty;
    }
    auto& data = GrassRuntime::GetPropertyData(static_cast<RE::BSGrassShaderProperty*>(pass->shaderProperty), globals::game::isVR);
    const float previous = flags & 0x10 ? 10.f : std::bit_cast<float>(data.windTimer);
    if (!(flags & 0x10)) data.windTimer = std::bit_cast<uint32_t>(10.f);
    const auto wind = GrassPolicy::WindVectorOffset(globals::game::isVR);
    f.constants[wind + 3] = 10.f;
    f.constants[wind + 7] = previous;
    ++f.nativeSetupCalls;
    const bool available = f.inOriginalSetup ? f.initialSnapshotAvailable : f.recoveredSnapshotAvailable;
    f.vertex.constantBuffers[2].data = available ? f.constants.data() : nullptr;
}
void Fixture::Setup(uint32_t flags, RE::BSRenderPass* native) {
    globals::state->currentShader = &shader;
    BSGrassShader_SetupGeometry::func.target = NativeSetup;
    inOriginalSetup = true;
    BSGrassShader_SetupGeometry::thunk(&shader, native ? native : &pass, flags);
    inOriginalSetup = false;
}
void Fixture::Restore(RE::BSRenderPass* native) {
    BSGrassShader_RestoreGeometry::func.target = nullptr;
    BSGrassShader_RestoreGeometry::thunk(&shader, native ? native : &pass, 0);
}
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void CheckConstantBufferBindings() {
    FakeContext context;
    ID3D11Buffer bones, currentBones;
    std::array<ID3D11Buffer*, 3> buffers{&bones, nullptr, &currentBones};
    const std::array<UINT, 3> first{16, 0, 32}, count{240, 4096, 16};
    context.VSSetConstantBuffers1(8, 3, buffers.data(), first.data(), count.data());
    std::array<ID3D11Buffer*, 3> actual{};
    std::array<UINT, 3> actualFirst{}, actualCount{};
    context.VSGetConstantBuffers1(8, 3, actual.data(), actualFirst.data(), actualCount.data());
    Require(actual == buffers && actualFirst == first && actualCount == count,
        "Vertex constant-buffer bindings must retain resources, null slots and subranges");
    context.VSSetConstantBuffers(9, 1, buffers.data());
    Require(context.vertexConstants[9] == FakeContext::ConstantBinding{&bones, 0, 4096} &&
            context.vertexConstants[8] == FakeContext::ConstantBinding{&bones, 16, 240} &&
            context.vertexConstants[10] == FakeContext::ConstantBinding{&currentBones, 32, 16},
        "Full-buffer binding must replace the slot window without changing adjacent slots");
    context.VSSetConstantBuffers1(9, 1, &buffers[1], first.data(), count.data());
    Require(context.vertexConstants[9] == FakeContext::ConstantBinding{},
        "Unbinding must discard the requested window, matching D3D11 WARP");
    context.VSSetConstantBuffers(9, 1, nullptr);
    Require(context.vertexConstants[9] == FakeContext::ConstantBinding{},
        "A null buffer array must unbind the requested slots");
}
enum class ParameterRoute : uint32_t {
    Batched, ShaderUnavailable, Disabled, Unadmitted, ExpandedFallback,
    PreparationFailure, AllCulled, SetupFailure, PassFailure, EmptyGroups, FinishFailure
};
void CheckGrassParameterRestoration() {
    using Route = ParameterRoute;
    for (const auto route : {Route::Batched, Route::ShaderUnavailable, Route::Disabled, Route::Unadmitted,
            Route::ExpandedFallback, Route::PreparationFailure, Route::AllCulled, Route::SetupFailure,
            Route::PassFailure, Route::EmptyGroups, Route::FinishFailure})
    for (bool offsetting : {false, true}) for (unsigned binding = 0; binding < (offsetting ? 3u : 2u); ++binding) {
        Fixture f;
        ID3D11Buffer bones, currentBones;
        f.context.constantBufferOffsetting = offsetting;
        ID3D11Buffer* previous = binding == 0 ? nullptr : &bones;
        const UINT first = 16, count = 240;
        if (binding == 2) f.context.VSSetConstantBuffers1(9, 1, &previous, &first, &count);
        else f.context.VSSetConstantBuffers(9, 1, &previous);
        auto* current = &currentBones;
        f.context.VSSetConstantBuffers(10, 1, &current);
        const auto before = f.context.vertexConstants;
        const auto reads = f.context.vertexConstantReads[9], writes = f.context.vertexConstantWrites[9];
        f.renderer.impl->ready = route != Route::ShaderUnavailable;
        if (route == Route::Disabled) f.renderer.impl->frameSettings.Enabled = false;
        if (route == Route::Unadmitted) f.renderer.impl->admitted.clear();
        if (route == Route::ExpandedFallback) f.renderer.impl->nativeFallbackDepth = 1;
        if (route == Route::PreparationFailure) f.renderer.impl->throwOnPrepare = true;
        if (route == Route::AllCulled) f.renderer.impl->allCulled = true;
        if (route == Route::FinishFailure) f.renderer.impl->throwOnDepth = true;
        bool threw = false;
        try {
            f.renderer.DrawRenderPass(&f.pass, 0, false, static_cast<uint32_t>(route),
                [](RE::BSRenderPass*, uint32_t, bool, uint32_t flags) {
                    const auto scenario = static_cast<Route>(flags);
                    auto& active = *Fixture::active;
                    ++active.nativeRenderPassCalls;
                    if (scenario == Route::SetupFailure) REL::Relocation<void (*)(uint32_t)>::throwNext = true;
                    active.Setup();
                    Require(active.context.vertexConstants[9].buffer == &active.buffer,
                        "Native grass setup must bind its own zeroed parameters");
                    if (scenario == Route::PassFailure) throw InjectedDrawFailure("Injected pass failure after setup");
                    if (scenario != Route::EmptyGroups && scenario != Route::FinishFailure) active.Draw();
                    active.Restore();
                });
        } catch (const InjectedDrawFailure&) {
            if (route != Route::PassFailure && route != Route::FinishFailure) throw;
            threw = true;
        }
        const bool nativeDraw = route == Route::ShaderUnavailable || route == Route::Disabled ||
            route == Route::Unadmitted || route == Route::ExpandedFallback ||
            route == Route::PreparationFailure || route == Route::SetupFailure;
        Require(f.nativeRenderPassCalls == 1 && GrassBucketRenderer::nativeCalls == (nativeDraw ? 1u : 0u) &&
                f.renderer.impl->submissions == (route == Route::Batched ? 1u : 0u),
            "The binding regression must exercise the requested native, batched or empty route");
        Require(threw == (route == Route::PassFailure || route == Route::FinishFailure),
            "Expected render-pass failure was not exercised");
        Require(f.context.vertexConstants == before,
            "Grass render pass leaked vertex constants into the following skinned draw");
        Require(f.context.vertexConstantReads[9] == reads + 1 && f.context.vertexConstantWrites[9] == writes + 2,
            "Grass must save b9 once per pass and restore it after native setup");
        RE::BSShader skinnedShader;
        skinnedShader.shaderType = RE::BSShader::Type::Lighting;
        auto skinnedPass = f.pass;
        skinnedPass.shader = &skinnedShader;
        globals::state->currentShader = &skinnedShader;
        f.renderer.DrawRenderPass(&skinnedPass, 0, false, 0,
            [](RE::BSRenderPass*, uint32_t, bool, uint32_t) {
                auto& active = *Fixture::active;
                active.Draw(false);
                auto* nextBones = &active.buffer;
                active.context.VSSetConstantBuffers(9, 1, &nextBones);
            });
        auto afterSkinned = before;
        afterSkinned[9] = {&f.buffer, 0, 4096};
        Require(f.context.vertexConstants == afterSkinned && f.context.vertexConstantReads[9] == reads + 1 &&
                f.context.vertexConstantWrites[9] == writes + 3,
            "A non-grass pass must retain its own binding changes without extra b9 queries or writes");
    }
}
void CheckParameterPassthrough() {
    for (unsigned unavailable = 0; unavailable < 6; ++unavailable) {
        Fixture f;
        auto* pass = &f.pass;
        if (unavailable == 0) pass = nullptr;
        if (unavailable == 1) pass->geometry = nullptr;
        if (unavailable == 2) pass->shader = nullptr;
        if (unavailable == 3) f.shader.shaderType = RE::BSShader::Type::Lighting;
        if (unavailable == 4) f.renderer.impl->context = nullptr;
        if (unavailable == 5) f.renderer.impl->disabled = nullptr;
        f.renderer.DrawRenderPass(pass, 0, false, 0,
            [](RE::BSRenderPass*, uint32_t, bool, uint32_t) {
                auto& active = *Fixture::active;
                ++active.nativeRenderPassCalls;
                auto* buffer = &active.buffer;
                active.context.VSSetConstantBuffers(9, 1, &buffer);
            });
        Require(f.nativeRenderPassCalls == 1 && f.context.vertexConstantReads[9] == 0 &&
                f.context.vertexConstantWrites[9] == 1 && f.context.vertexConstants[9].buffer == &f.buffer,
            "Unavailable grass passes/resources must not query or restore native buffer changes");
    }
}
void CheckQueuedStreamingFallback() {
    for (bool failExpanded : {false, true}) for (unsigned mutation = 0; mutation < 4; ++mutation) {
        Fixture f;
        ID3D11Buffer bones;
        auto* previous = &bones;
        const UINT first = 16, count = 240;
        f.context.VSSetConstantBuffers1(9, 1, &previous, &first, &count);
        const auto before = f.context.vertexConstants;
        auto otherShape = f.shape;
        auto otherSource = f.renderer.impl->buckets.front().sources.front();
        otherSource.identity = &otherShape;
        otherSource.lifetime = std::make_shared<Lifetime>();
        auto queue = otherSource.queue;
        queue->members.push_back(&otherShape);
        queue->instances += 4;
        ++queue->slices;
        f.renderer.impl->buckets.front().sources.push_back(otherSource);
        f.renderer.impl->buckets.front().instances += 4;
        f.renderer.impl->residents.emplace(&otherShape, otherSource);
        f.QueueSources();
        const auto retire = [&] {
            f.renderer.impl->EraseResident(&otherShape);
            ++otherSource.lifetime->generation;
        };
        if (mutation == 1) retire();
        if (mutation == 2) {
            retire();
            f.renderer.impl->EraseResident(&f.shape);
        }
        std::vector<RE::BSMultiStreamInstanceTriShape*> drawn;
        static std::function<void(RE::BSRenderPass*)> visit;
        visit = [&](RE::BSRenderPass* pass) {
            drawn.push_back(pass->geometry);
            if (mutation == 3 && pass->geometry == &f.shape) retire();
            f.Setup(0, pass);
            Require(f.context.vertexConstants[9].buffer == &f.buffer,
                "Each expanded native member must receive grass parameters");
            if (failExpanded && pass->geometry == &otherShape)
                throw InjectedDrawFailure("Injected expanded-member failure");
            f.Draw(pass->geometry == &f.shape);
            f.Restore(pass);
        };
        for (unsigned pass = 0; pass < 2; ++pass) {
            drawn.clear();
            bool threw = false;
            try {
                f.renderer.DrawRenderPass(&f.pass, 0, false, 0,
                    [](RE::BSRenderPass* native, uint32_t, bool, uint32_t) { visit(native); });
            } catch (const InjectedDrawFailure&) { threw = true; }
            Require(drawn == std::vector{&f.shape, &otherShape},
                "Streaming invalidation lost or duplicated a suppressed native draw");
            Require(threw == failExpanded && f.renderer.impl->nativeFallbackDepth == 0 &&
                    f.context.vertexConstants == before,
                "Expanded fallback must restore the incoming binding and nesting depth, including exceptions");
            ++f.pass.passEnum;
        }
        Require(f.renderer.impl->submissions == 0, "Native fallback unexpectedly submitted a batch");
        const auto pinned = otherSource.lifetime->queued.lock();
        Require(pinned && pinned->members.size() == 2, "Queued sources were not retained for paired passes");
        f.renderer.EndFrame();
        Require(f.renderer.impl->queuedGroups.empty(), "Frame-end retained stale native draw owners");
    }
}
void CheckNestedParameterRestoration() {
    for (bool unwind : {false, true}) {
        Fixture f;
        ID3D11Buffer bones;
        auto* previous = &bones;
        const UINT first = 16, count = 240;
        f.context.VSSetConstantBuffers1(9, 1, &previous, &first, &count);
        const auto before = f.context.vertexConstants;
        bool threw = false;
        try {
            f.renderer.DrawRenderPass(&f.pass, 0, false, unwind ? 1u : 0u,
                [](RE::BSRenderPass* pass, uint32_t, bool, uint32_t flags) {
                    auto& active = *Fixture::active;
                    active.Setup();
                    const auto outer = active.context.vertexConstants;
                    active.renderer.DrawRenderPass(pass, 0, false, flags,
                        [](RE::BSRenderPass*, uint32_t, bool, uint32_t fail) {
                            auto& nested = *Fixture::active;
                            nested.Setup();
                            nested.Draw();
                            if (fail) throw InjectedDrawFailure("Injected nested-pass failure");
                            nested.Restore();
                        });
                    Require(active.context.vertexConstants == outer,
                        "An inner grass pass must restore its outer pass's parameters");
                    active.Draw();
                    active.Restore();
                });
        } catch (const InjectedDrawFailure&) { threw = true; }
        Require(threw == unwind && f.context.vertexConstants == before,
            "Nested grass passes must restore previous bones, including exception unwinding");
    }
}
void CheckRenderFamilyIsolation() {
    {
        Fixture f;
        auto otherShape = f.shape;
        auto& bucket = f.renderer.impl->buckets.front();
        auto otherSource = bucket.sources.front();
        otherSource.identity = &otherShape;
        otherSource.lifetime = std::make_shared<Lifetime>();
        bucket.sources.front().queue->members.push_back(&otherShape);
        bucket.sources.push_back(otherSource);
        bucket.instances += otherSource.queue->instances;
        otherSource.queue->instances = bucket.instances;
        ++otherSource.queue->slices;
        f.renderer.impl->residents.emplace(&otherShape, otherSource);
        f.renderer.impl->admitted.emplace(&otherShape, Admission{0, 1});
        f.QueueSources();
        f.renderer.impl->ready = true;
        const auto draw = [](RE::BSRenderPass* pass, uint32_t, bool, uint32_t) {
            auto& current = *Fixture::active;
            ++current.nativeRenderPassCalls;
            if (pass->geometry != &current.shape) {
                current.Draw(false);
                return;
            }
            current.Setup();
            current.Draw();
            current.Restore();
        };
        f.renderer.DrawRenderPass(&f.pass, 0, false, 0, draw);
        Require(f.nativeRenderPassCalls == 1 && f.renderer.impl->submissions == 1 &&
                GrassBucketRenderer::nativeCalls == 0,
            "Restoring committed grass must preserve the wrapper result and prevent duplicate native queue expansion");
        ++f.pass.passEnum;
        f.renderer.DrawRenderPass(&f.pass, 0, false, 0, draw);
        Require(f.nativeRenderPassCalls == 2 && f.renderer.impl->submissions == 2 &&
                GrassBucketRenderer::nativeCalls == 0,
            "Paired grass coverage must also suppress native expansion of the second queue member");
    }
    for (bool changedDescriptors : {false, true}) for (unsigned boundary = 0; boundary < 6; ++boundary) {
        Fixture f;
        f.renderer.impl->ready = true;
        if (boundary == 1 || boundary == 2) {
            bool threw = false;
            try {
                f.renderer.DrawRenderPass(&f.pass, 0, false, 0,
                    [](RE::BSRenderPass*, uint32_t, bool, uint32_t flags) {
                        auto& current = *Fixture::active;
                        current.Setup();
                        current.Draw();
                        if (flags) throw std::runtime_error("Injected render-pass failure");
                        current.Restore();
                    });
            } catch (const std::runtime_error&) { threw = true; }
            Require(!threw, "Completed grass render-pass setup unexpectedly failed");
            if (boundary == 2) {
                f.pass.passEnum = 2;
                try {
                    f.renderer.DrawRenderPass(&f.pass, 0, false, 1,
                        [](RE::BSRenderPass*, uint32_t, bool, uint32_t) {
                            auto& current = *Fixture::active;
                            current.Setup();
                            current.Draw();
                            throw std::runtime_error("Injected render-pass failure");
                        });
                } catch (const std::runtime_error&) { threw = true; }
                Require(threw, "The exceptional wrapper route must reach its native draw");
            }
        } else {
            f.Setup();
            f.Draw();
            if (boundary == 0) f.Restore();
            if (boundary == 3) {
                f.renderer.EndFrame();
                ++globals::state->frameCount;
                ++globals::state->frameCountAtomic;
            }
        }
        const auto submissions = f.renderer.impl->submissions;
        const auto culls = f.renderer.impl->cullingAttempts;
        Require(submissions == (boundary == 2 ? 2u : 1u) && GrassBucketRenderer::nativeCalls == 0,
            "The valid grass route must own coverage before testing unrelated draws");
        RE::BSShader treeShader;
        treeShader.shaderType = RE::BSShader::Type::DistantTree;
        RE::BSMultiStreamInstanceTriShape treeShape;
        RE::BSRenderPass treePass{&treeShape};
        treePass.shader = &treeShader;
        treePass.shaderProperty = f.property.get();
        if (changedDescriptors) {
            globals::state->modifiedVertexDescriptor = 0x10000;
            globals::state->modifiedPixelDescriptor = 0x10000;
        }
        if (boundary == 4) globals::state->currentShader = &treeShader;
        if (boundary == 5) {
            f.renderer.DrawRenderPass(&treePass, 0x10000, true, 0,
                [](RE::BSRenderPass*, uint32_t, bool, uint32_t) { Fixture::active->Draw(false); });
        } else f.Draw(false);
        Require(GrassBucketRenderer::nativeCalls == 1 && f.renderer.impl->submissions == submissions &&
                f.renderer.impl->cullingAttempts == culls,
            "A shared draw after restore, wrapper completion, exception, frame end or shader change must stay native");
        globals::state->currentShader = &treeShader;
        f.renderer.DrawRenderPass(&treePass, 0x10000, true, 0,
            [](RE::BSRenderPass*, uint32_t, bool, uint32_t) { Fixture::active->Draw(false); });
        Require(GrassBucketRenderer::nativeCalls == 2 && f.renderer.impl->submissions == submissions,
            "Distant-tree passes must preserve native draws with unchanged and changed descriptors");
        if (boundary == 3) continue;
        globals::state->modifiedVertexDescriptor = 2;
        globals::state->modifiedPixelDescriptor = 3;
        ++f.pass.passEnum;
        f.renderer.impl->buckets.front().sources.front().groupsCurrent = false;
        f.Setup();
        f.Draw(false);
        Require(f.renderer.impl->submissions == submissions + 1 && GrassBucketRenderer::nativeCalls == 2 &&
                f.renderer.impl->cullingAttempts == culls,
            "Fresh grass setup must retain paired coverage after unrelated draws without repeating culling");
        f.Restore();
    }
    for (unsigned foreignShader = 0; foreignShader < 4; ++foreignShader) {
        Fixture f;
        f.renderer.impl->ready = true;
        f.Setup();
        f.Draw();
        RE::BSShader otherShader;
        if (foreignShader == 1) otherShader.shaderType = RE::BSShader::Type::DistantTree;
        if (foreignShader == 2) f.shader.shaderType = RE::BSShader::Type::DistantTree;
        globals::state->currentShader = foreignShader == 0 ? nullptr :
            (foreignShader == 2 ? &f.shader : &otherShader);
        f.Draw(false);
        Require(GrassBucketRenderer::nativeCalls == 1 && f.renderer.impl->submissions == 1,
            "An inactive, foreign or non-grass shader must not consume retained grass coverage");
    }
    {
        Fixture f;
        f.renderer.impl->ready = true;
        f.Setup();
        f.Draw();
        ++f.pass.passEnum;
        f.Setup();
        RE::BSShader treeShader;
        treeShader.shaderType = RE::BSShader::Type::DistantTree;
        globals::state->currentShader = &treeShader;
        f.Draw(false);
        f.Restore();
        f.Draw(false);
        Require(GrassBucketRenderer::nativeCalls == 2 && f.renderer.impl->submissions == 1,
            "Finishing a pending grass pass under a foreign shader must not replay retained coverage");
    }
    {
        Fixture f;
        f.renderer.impl->ready = true;
        f.Setup();
        f.Draw();
        RE::BSMultiStreamInstanceTriShape otherShape;
        f.pass.geometry = &otherShape;
        f.Draw(false);
        f.Restore();
        f.pass.geometry = &f.shape;
        f.Draw();
        Require(GrassBucketRenderer::nativeCalls == 2 && f.renderer.impl->submissions == 1,
            "A pass whose geometry changed must stay native and retire its prior admission");
    }
    {
        Fixture f;
        f.renderer.impl->ready = true;
        f.Setup();
        f.Draw();
        ++f.pass.passEnum;
        REL::Relocation<void (*)(uint32_t)>::throwNext = true;
        f.Setup();
        f.Draw();
        Require(f.renderer.impl->failed && GrassBucketRenderer::nativeCalls == 1 && f.renderer.impl->submissions == 1,
            "A failure after admitting a fresh pass must discard its draw context before native fallback");
    }
    {
        Fixture f;
        f.renderer.impl->ready = true;
        f.Setup();
        auto otherPass = f.pass;
        f.renderer.FinishGeometry(&otherPass);
        f.Draw();
        BSGrassShader_RestoreGeometry::func.target = [](RE::BSShader*, RE::BSRenderPass*, uint32_t) {
            throw std::runtime_error("Injected native restore failure");
        };
        bool threw = false;
        try { BSGrassShader_RestoreGeometry::thunk(&f.shader, &f.pass, 0); }
        catch (const std::runtime_error&) { threw = true; }
        Require(threw && f.renderer.impl->submissions == 1,
            "A different pass must not retire the active grass geometry");
        f.Draw(false);
        Require(GrassBucketRenderer::nativeCalls == 1 && f.renderer.impl->submissions == 1,
            "A native restoration exception must leave subsequent shared draws native");
    }
    {
        Fixture f;
        f.renderer.impl->ready = true;
        f.Setup();
        f.renderer.impl->throwOnDepth = true;
        bool threw = false;
        try { f.Restore(); } catch (const std::runtime_error&) { threw = true; }
        Require(threw, "Injected geometry-finish failure must reach restoration");
        f.renderer.impl->throwOnDepth = false;
        f.Draw();
        Require(GrassBucketRenderer::nativeCalls == 1 && f.renderer.impl->submissions == 0,
            "Exceptional geometry finish must retire the active pass before another shared draw");
    }
}
int main() {
    try {
        CheckConstantBufferBindings();
        for (bool vr : {false, true}) {
            globals::game::isVR = vr;
            CheckParameterPassthrough();
            CheckGrassParameterRestoration();
            CheckNestedParameterRestoration();
            CheckRenderFamilyIsolation();
            CheckQueuedStreamingFallback();
            for (bool loaded : {false, true}) for (bool enabled : {false, true}) {
                Fixture f;
                globals::features::grassOptimizations.loaded = loaded;
                globals::features::grassOptimizations.enabled = enabled;
                f.Setup();
                const auto expected = loaded && enabled ? 1u : 0u;
                Require(globals::game::shadowState->addressUpdates == expected &&
                        globals::game::shadowState->filterUpdates == expected,
                    "Sampler overrides must follow both the loaded and enabled grass gates");
            }
            {
                Fixture f;
                f.Setup();
                f.renderer.impl->ready = true;
                f.Draw();
                Require(f.renderer.impl->submissions == 1 && GrassBucketRenderer::nativeCalls == 0,
                    "Late graphics readiness must batch at the actual draw; setup must not cache native fallback");
                Require(f.nativeSetupCalls == 1,
                    "An already captured current pass must not repeat native setup or restoration");
                f.Draw();
                Require(f.renderer.impl->submissions == 1 && GrassBucketRenderer::nativeCalls == 0,
                    "A completed batch must suppress repeated native groups without resubmission");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw(false);
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1,
                    "A mismatching native draw must never submit an early batch");
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 2,
                    "Native rendering already submitted must prevent later whole-bucket duplication");
            }
            for (unsigned invalid = 0; invalid < 7; ++invalid) {
                Fixture f;
                f.renderer.impl->ready = true;
                f.initialSnapshotAvailable = false;
                f.Setup();
                auto& source = f.renderer.impl->buckets.front().sources.front();
                if (invalid == 0) source.lifetime->alive = false;
                if (invalid == 1) ++source.lifetime->generation;
                if (invalid == 2) source.groupsCurrent = false;
                if (invalid == 3) source.lifetime->queued.reset();
                if (invalid == 4) source.queue->representative = nullptr;
                if (invalid == 5) source.queue.reset();
                if (invalid == 6) ++source.queue->queuedFrame;
                source.forbidNativeGeometry = invalid < 2;
                f.renderer.impl->PrepareCulling(&f.pass, &f.shader, 0, Fixture::NativeSetup);
                Require(f.nativeSetupCalls == 1 && f.renderer.impl->cullingAttempts == 0,
                    "Invalidated representatives must be rejected before native visibility or geometry preparation");
            }
            for (bool failNativeSetup : {false, true}) {
                Fixture f;
                auto otherShape = f.shape;
                auto& state = *f.renderer.impl;
                auto& source = state.buckets.front().sources.front();
                source.identity = &otherShape;
                source.queue->members = {&otherShape};
                source.queue->representative = &otherShape;
                state.residents.emplace(&otherShape, source);
                state.identities.emplace(&otherShape, std::make_pair(source.lifetime, std::string{}));
                f.QueueSources();
                state.ready = true;
                f.Setup();
                std::promise<bool> attempted;
                auto attemptedResult = attempted.get_future();
                std::promise<void> removed;
                auto removedResult = removed.get_future();
                std::jthread remover;
                bool started = false;
                f.duringNativeSetup = [&](RE::BSRenderPass* pass) {
                    if (pass->geometry != &otherShape || started) return;
                    started = true;
                    remover = std::jthread([&] {
                        const bool unprotected = state.captureMutex.try_lock();
                        if (unprotected) state.captureMutex.unlock();
                        attempted.set_value(!unprotected);
                        f.renderer.RemoveShape(&otherShape);
                        otherShape.geometry.shaderProperty.reset();
                        removed.set_value();
                    });
                    Require(attemptedResult.get() && source.lifetime->alive,
                        "Native representative setup must exclude concurrent removal");
                    if (failNativeSetup) throw std::runtime_error("Injected native setup failure");
                };
                state.beforeBucketWork = [&] {
                    Require(removedResult.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
                        "Capture protection must end before buffer and culling preparation");
                };
                bool failed = false;
                try { state.PrepareCulling(&f.pass, &f.shader, 0, Fixture::NativeSetup); }
                catch (const std::runtime_error& error) {
                    failed = true;
                    Require(std::string(error.what()) == "Injected native setup failure",
                        "Unexpected failure in concurrent preparation");
                }
                Require(failed == failNativeSetup && started,
                    "Native setup failure injection did not reach the representative");
                Require(removedResult.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
                    "Removal must finish after normal or exceptional native setup");
                remover.join();
                Outcome outcome = Outcome::Native;
                Require(!source.lifetime->alive && !state.DrawBucket(state.buckets.front(), outcome),
                    "Removal before first submission must retain native fallback");
                Require(f.property->windTimer == std::bit_cast<uint32_t>(10.f) &&
                        f.constants[GrassPolicy::WindVectorOffset(vr) + 7] == 9.f,
                    "Concurrent removal must not disturb restoration of the real draw's wind");
            }
            for (bool sameLights : {false, true}) {
                Fixture f;
                RE::BSLight light;
                RE::BSLight* lights[]{&light};
                f.pass.sceneLights = lights;
                f.pass.numLights = 1;
                if (sameLights) f.renderer.impl->buckets.front().sources.front().lights = {&light};
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw();
                Require(f.nativeSetupCalls == (sameLights ? 1u : 3u) && f.renderer.impl->submissions == 1,
                    "Current-pass reuse must require matching lights and retain native setup otherwise");
            }
            for (bool failPreparation : {false, true}) {
                Fixture f;
                auto otherShape = f.shape;
                auto& source = f.renderer.impl->buckets.front().sources.front();
                source.identity = &otherShape;
                source.queue->members = {&otherShape};
                source.queue->representative = &otherShape;
                f.QueueSources();
                f.renderer.impl->ready = true;
                f.renderer.impl->throwOnPrepare = failPreparation;
                f.Setup();
                bool failed = false;
                try { f.renderer.impl->PrepareCulling(&f.pass, &f.shader, 0, Fixture::NativeSetup); }
                catch (const std::runtime_error&) { failed = true; }
                Require(failed == failPreparation && f.nativeSetupCalls == 3,
                    "Preparing another representative must run native setup and restore the current pass, including exceptions");
                Require(f.property->windTimer == std::bit_cast<uint32_t>(10.f),
                    "Preparing another representative advanced the current property's wind");
            }
            {
                Fixture f;
                f.Setup();
                f.Draw();
                f.renderer.impl->ready = true;
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 2,
                    "A failed actual draw must retain native rendering for the remainder of the pass");
            }
            {
                Fixture f;
                f.renderer.impl->frameSettings.Enabled = false;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1,
                    "Disabled optimization must retain native rendering");
            }
            {
                Fixture f;
                f.renderer.impl->nativeFallbackDepth = 1;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1,
                    "Expanded native fallback must not recursively attempt batching");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.renderer.impl->throwOnPrepare = true;
                f.Setup();
                f.Draw();
                f.Draw();
                Require(f.renderer.impl->failed && f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 2,
                    "A preparation exception must retain native draws without submitting or retrying");
            }
            {
                Fixture f;
                f.initialSnapshotAvailable = false;
                f.Setup();
                f.renderer.impl->ready = true;
                f.Draw();
                Require(f.renderer.impl->submissions == 1 && GrassBucketRenderer::nativeCalls == 0,
                    "A missing initial CPU snapshot must recover through native preparation");
                Require(f.nativeSetupCalls > 1 &&
                        globals::game::shadowState->addressUpdates == f.nativeSetupCalls &&
                        globals::game::shadowState->filterUpdates == f.nativeSetupCalls,
                    "Batch preparation and restoration must retain native sampler overrides");
            }
            {
                Fixture f;
                f.initialSnapshotAvailable = f.recoveredSnapshotAvailable = false;
                f.Setup();
                f.renderer.impl->ready = true;
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1,
                    "Unrecoverable geometry constants must retain native rendering");
                Require(f.property->windTimer == std::bit_cast<uint32_t>(10.f),
                    "Unrecoverable constants must preserve the real native wind update");
            }
            {
                Fixture f;
                f.initialSnapshotAvailable = false;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw();
                const auto culls = f.renderer.impl->cullingAttempts;
                f.pass.passEnum = 2;
                f.Setup();
                Require(!f.renderer.impl->geometryCPUValid && f.renderer.impl->cullingPrepared,
                    "The later pass must start with cached culling and no CPU snapshot");
                const auto calls = f.nativeSetupCalls;
                f.Draw();
                Require(f.renderer.impl->submissions == 2 && GrassBucketRenderer::nativeCalls == 0 &&
                    f.renderer.impl->cullingAttempts == culls && f.nativeSetupCalls == calls,
                    "A later pass must reuse owned constants without native recovery or repeated culling");
                const auto wind = GrassPolicy::WindVectorOffset(vr);
                Require(f.property->windTimer == std::bit_cast<uint32_t>(10.f) && f.constants[wind + 7] == 10.f,
                    "Later-pass recovery must restore that pass's original native wind");
            }

            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.renderer.impl->allCulled = true;
                f.Setup();
                f.Draw();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 0,
                    "An entirely culled bucket must suppress all native groups without emitting draws");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.renderer.impl->geometryCPUValid = true;
                f.renderer.impl->drawShadersReady = true;
                auto& bucket = f.renderer.impl->buckets.front();
                bucket.plan.prepared = true;
                bucket.plan.geometry.front() = 1;
                Outcome outcome = Outcome::Native;
                Require(f.renderer.impl->DrawBucket(bucket, outcome) && outcome == Outcome::Batched &&
                        bucket.plan.geometry.front() == 1,
                    "Prepared geometry must remain authoritative instead of changing with native constants");
            }
            for (bool replacedSnapshot : {false, true}) {
                Fixture f;
                f.renderer.impl->ready = true;
                f.Setup();
                f.renderer.impl->PrepareCulling(&f.pass, &f.shader, 0, Fixture::NativeSetup);
                auto& source = f.renderer.impl->buckets.front().sources.front();
                if (replacedSnapshot) ++source.lifetime->generation;
                else source.groupsCurrent = false;
                f.Draw();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 2,
                    "Changed group layout or replaced snapshot must reject an already prepared batch");
            }
            for (uint32_t flags : {0u, 0x10u}) {
                for (float previous : {0.f, 9.f}) {
                    for (bool failPreparation : {false, true}) {
                        Fixture f;
                        f.initialSnapshotAvailable = false;
                        f.property->windTimer = std::bit_cast<uint32_t>(previous);
                        f.renderer.impl->ready = true;
                        f.renderer.impl->throwOnPrepare = failPreparation;
                        f.Setup(flags);
                        Require(f.renderer.impl->currentNativeWindTimer == std::bit_cast<uint32_t>(previous),
                            "The real hook must capture native property history before original setup");
                        Require(!f.renderer.impl->geometryCPUValid,
                            "The recovery case must begin without a CPU snapshot");
                        f.Draw();
                        const auto expectedHistory = std::bit_cast<uint32_t>(flags & 0x10 ? previous : 10.f);
                        const auto wind = GrassPolicy::WindVectorOffset(vr);
                        Require(f.property->windTimer == expectedHistory,
                            "Preparation must preserve the real native property's wind history");
                        Require(f.constants[wind + 7] == (flags & 0x10 ? 10.f : previous),
                            "Restoring native constants must retain the original previous wind on success and failure");
                        Require(f.renderer.impl->submissions == (failPreparation ? 0u : 1u),
                            "Recovered constants must batch unless preparation fails");
                        if (!failPreparation) {
                            const auto calls = f.nativeSetupCalls;
                            f.Draw();
                            Require(f.nativeSetupCalls == calls && f.renderer.impl->submissions == 1,
                                "Repeated groups must not advance wind or resubmit the batch");
                        }
                    }
                }
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.Setup();
                f.renderer.impl->currentNativeWindTimer.reset();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1,
                    "Absent native history must fail closed without unsafe restoration");
            }
            {
                Fixture f;
                f.initialSnapshotAvailable = false;
                f.renderer.impl->ready = true;
                f.renderer.impl->throwOnUpload = true;
                f.Setup();
                f.Draw();
                const auto wind = GrassPolicy::WindVectorOffset(vr);
                Require(f.renderer.impl->failed && GrassBucketRenderer::nativeCalls == 1 &&
                    f.property->windTimer == std::bit_cast<uint32_t>(10.f) && f.constants[wind + 7] == 9.f,
                    "An upload failure must preserve native wind and original constants before fallback");
            }
            for (unsigned unavailable : {0u, 1u, 2u}) {
                Fixture f;
                f.initialSnapshotAvailable = false;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw();
                f.pass.passEnum = 2;
                f.Setup();
                const auto calls = f.nativeSetupCalls;
                if (unavailable == 0) f.renderer.impl->failed = true;
                if (unavailable == 1) f.renderer.impl->shaderAllowed = false;
                if (unavailable == 2) f.renderer.impl->depthAllowed = false;
                f.Draw();
                Require(f.nativeSetupCalls == calls && f.renderer.impl->submissions == (unavailable == 2 ? 1u : 2u) &&
                        GrassBucketRenderer::nativeCalls == (unavailable == 2 ? 1u : 0u),
                    "Committed main passes must finish with owned inputs; other depth targets remain native");
            }
            for (unsigned mutation : {0u, 1u, 2u, 3u, 4u}) for (bool empty : {false, true}) {
                Fixture f;
                f.renderer.impl->ready = true;
                f.renderer.impl->allCulled = empty;
                f.Setup();
                f.Draw();
                auto& bucket = f.renderer.impl->buckets.front();
                auto& source = bucket.sources.front();
                const auto geometry = bucket.plan.geometry;
                const auto culls = f.renderer.impl->cullingAttempts;
                if (mutation == 0) source.lifetime->alive = false;
                if (mutation == 1) ++source.lifetime->generation;
                if (mutation == 2) source.groupsCurrent = false;
                if (mutation == 3) source.groups.clear();
                f.pass.passEnum = 2;
                f.Setup();
                f.renderer.impl->geometryCPU.fill(123.f);
                f.Draw(mutation != 4);
                f.Draw(mutation != 4);
                Require(f.renderer.impl->submissions == (empty ? 0u : 2u) && GrassBucketRenderer::nativeCalls == 0 &&
                        f.renderer.impl->cullingAttempts == culls && bucket.plan.geometry == geometry,
                    "Native changes after depth must neither alter owned coverage, repeat culling nor admit native colour");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                BSGrassShader_RestoreGeometry::func.target = [](RE::BSShader*, RE::BSRenderPass* pass, uint32_t) {
                    Require(Fixture::active->renderer.impl->submissions == (pass->passEnum == 2 ? 2u : 1u),
                        "Owned coverage must be submitted before native geometry restoration");
                };
                const auto draw = [](RE::BSRenderPass*, uint32_t, bool, uint32_t) {
                    auto& current = *Fixture::active;
                    current.Setup();
                    if (!current.renderer.impl->buckets.front().sources.front().groups.empty()) current.Draw();
                    BSGrassShader_RestoreGeometry::thunk(&current.shader, &current.pass, 0);
                };
                f.renderer.DrawRenderPass(&f.pass, 0, false, 0, draw);
                auto& source = f.renderer.impl->buckets.front().sources.front();
                source.groups.clear();
                source.queue->members.clear();
                f.renderer.impl->residents.clear();
				f.renderer.impl->queues.clear();
                f.pass.passEnum = 2;
                f.renderer.DrawRenderPass(&f.pass, 0, false, 0, draw);
                f.renderer.DrawRenderPass(&f.pass, 0, false, 0, draw);
                Require(f.renderer.impl->submissions == 2 && GrassBucketRenderer::nativeCalls == 0,
                    "A retained batch must finish colour once even when removal eliminates every native group callback");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.Setup();
                f.Draw(false);
                f.renderer.impl->buckets.front().sources.clear();
                f.pass.passEnum = 2;
                f.Setup();
                f.Draw();
                Require(!f.renderer.impl->failed && GrassBucketRenderer::nativeCalls == 2,
                    "Native ownership must not revisit removed source metadata");
            }
            for (bool missingShaders : {false, true}) {
                Fixture f;
                f.renderer.impl->ready = !missingShaders;
                f.renderer.impl->pairedShadersAvailable = !missingShaders;
                f.Setup();
                f.Draw(false);
                f.pass.passEnum = 2;
                f.renderer.impl->ready = f.renderer.impl->pairedShadersAvailable = true;
                f.Setup();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 2,
                    "Native coverage must stay native across main passes even if readiness improves");
                f.renderer.impl->buckets.front().outcomes.Clear();
                f.Draw();
                Require(f.renderer.impl->submissions == 1,
                    "A new frame must allow recovery from native coverage");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.renderer.impl->pairedShadersAvailable = false;
                f.Setup();
                f.Draw();
                Require(f.renderer.impl->submissions == 0 && f.renderer.impl->cullingAttempts == 0 &&
                        GrassBucketRenderer::nativeCalls == 1,
                    "A missing paired shader must choose native before any depth or culling submission");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.renderer.impl->depthAllowed = false;
                f.Setup();
                f.Draw();
                f.renderer.impl->depthAllowed = true;
                f.Draw();
                Require(f.renderer.impl->submissions == 1 && GrassBucketRenderer::nativeCalls == 1,
                    "A native shadow or offscreen pass must not lock main-view coverage");
            }
            {
                Fixture f;
                f.renderer.impl->ready = true;
                f.replacePropertyOnSetup = true;
                f.Setup();
                const auto calls = f.nativeSetupCalls;
                f.Draw();
                const auto wind = GrassPolicy::WindVectorOffset(vr);
                Require(!f.renderer.impl->currentNativeWindTimer && f.nativeSetupCalls == calls &&
                    f.renderer.impl->submissions == 0 && GrassBucketRenderer::nativeCalls == 1 &&
                    f.replacementProperty->windTimer == std::bit_cast<uint32_t>(10.f) && f.constants[wind + 7] == 3.f,
                    "Replacing the native property must invalidate its captured wind without changing the replacement's constants");
            }
            {
                Fixture f;
                f.renderer.impl->geometryCPUValid = true;
                const auto saved = globals::game::currentVertexShader;
                globals::game::currentVertexShader = nullptr;
                f.renderer.impl->CopyGeometryConstants();
                globals::game::currentVertexShader = saved;
                Require(!f.renderer.impl->geometryCPUValid,
                    "An unavailable shader handle must invalidate stale CPU constants");
            }
        }
        std::cout << "Production grass snapshot recovery, wind restoration, draw ordering and fallback passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
'''.replace("PRODUCTION_METHODS", methods)
    return compile_fixture(driver, devbench)


def compile_fixture(driver: str, devbench: bool = False) -> subprocess.CompletedProcess:
    compiler = os.environ.get("CXX") or shutil.which("cl")
    if not compiler:
        raise RuntimeError("Run in the MSVC developer environment")
    with tempfile.TemporaryDirectory(prefix="grass-draw-lifecycle-") as temporary:
        directory = Path(temporary)
        cpp = directory / "driver.cpp"
        executable = directory / "driver.exe"
        cpp.write_text(driver, encoding="utf-8")
        command = [compiler, "/nologo", "/std:c++latest", "/EHsc", "/W4", "/WX",
                   "/I" + str(ROOT / "src"), "/I" + str(ROOT / "tests"),
                   str(cpp), "/Fe:" + str(executable)]
        if devbench:
            command.append("/DDEVBENCH_BRIDGE_ENABLED")
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return subprocess.run([str(executable)], cwd=directory, capture_output=True, text=True, timeout=10)


class GrassDrawLifecycleTests(unittest.TestCase):
    def test_retained_cache_resources(self):
        source = (ROOT / "src/ShaderCache.cpp").read_text(encoding="utf-8")
        methods = "template <typename Interface, typename Cache>\n" + block(source, "winrt::com_ptr<Interface> RetainShaderResource(")
        methods += "\ntemplate <typename ShaderType, typename MutexType>\n" + block(source, "void ReleaseShader(")
        for signature in ("winrt::com_ptr<ID3D11VertexShader> ShaderCache::RetainVertexShader(",
                          "winrt::com_ptr<ID3D11PixelShader> ShaderCache::RetainPixelShader(",
                          "void ShaderCache::EvictShaderResources("):
            methods += "\n" + block(source, signature)
        driver = r"""
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
struct Resource {
    int references = 1;
    std::mutex* owner;
    void AddRef() {
        bool locked = false;
        std::thread probe([&] { locked = owner->try_lock(); if (locked) owner->unlock(); });
        probe.join();
        assert(!locked);
        ++references;
    }
    void Release() { assert(references > 0); --references; }
};
using ID3D11VertexShader = Resource;
using ID3D11PixelShader = Resource;
namespace winrt {
template<class T> struct com_ptr {
    T* value = nullptr;
    com_ptr() = default;
    com_ptr(const com_ptr&) = delete;
    com_ptr(com_ptr&& other) noexcept : value(other.value) { other.value = nullptr; }
    ~com_ptr() { if (value) value->Release(); }
    void copy_from(T* pointer) { assert(!value); value = pointer; if (value) value->AddRef(); }
    explicit operator bool() const { return value != nullptr; }
};
}
namespace RE { struct BSShader {
    enum class Type { Grass, Invalid };
    struct TypeValue { Type get() const { return Type::Grass; } } shaderType;
}; }
namespace logger { template<class... T> void warn(const char*, T&&...) {} }
enum class ShaderClass { Vertex, Pixel, Compute };
struct ShaderCache {
    enum class PixelShaderLookup { Draw };
    struct Entry { Resource* shader; };
    using Cache = std::array<std::unordered_map<uint32_t, std::unique_ptr<Entry>>, 1>;
    Cache vertexShaders, pixelShaders, computeShaders;
    std::mutex vertexShadersMutex, pixelShadersMutex, computeShadersMutex;
    std::atomic_uint64_t graphicsShaderRevision{0};
    bool enabled = true, evictDuringLookup = false;
    const void* GetVertexShader(const RE::BSShader&, uint32_t descriptor) {
        if (evictDuringLookup) EvictShaderResources(RE::BSShader::Type::Grass, descriptor, ShaderClass::Vertex);
        return enabled ? this : nullptr;
    }
    const void* GetPixelShader(const RE::BSShader&, uint32_t descriptor, PixelShaderLookup) {
        if (evictDuringLookup) EvictShaderResources(RE::BSShader::Type::Grass, descriptor, ShaderClass::Pixel);
        return enabled ? this : nullptr;
    }
    winrt::com_ptr<ID3D11VertexShader> RetainVertexShader(const RE::BSShader&, uint32_t);
    winrt::com_ptr<ID3D11PixelShader> RetainPixelShader(const RE::BSShader&, uint32_t);
    void EvictShaderResources(RE::BSShader::Type, uint32_t, ShaderClass);
};
METHODS
int main() {
    for (bool vertex : {false, true}) {
        ShaderCache cache;
        auto& map = vertex ? cache.vertexShaders : cache.pixelShaders;
        auto& mutex = vertex ? cache.vertexShadersMutex : cache.pixelShadersMutex;
        const auto stage = vertex ? ShaderClass::Vertex : ShaderClass::Pixel;
        Resource resource{1, &mutex};
        map[0][8] = std::make_unique<ShaderCache::Entry>(&resource);
        RE::BSShader shader;
        const auto retain = [&] { return vertex ? cache.RetainVertexShader(shader, 8) : cache.RetainPixelShader(shader, 8); };
        cache.enabled = false;
        assert(!retain() && resource.references == 1);
        cache.enabled = true;
        {
            auto held = retain();
            assert(held && resource.references == 2);
            cache.EvictShaderResources(RE::BSShader::Type::Grass, 8, stage);
            assert(cache.graphicsShaderRevision == 1 && resource.references == 1);
            assert(!retain());
            cache.EvictShaderResources(RE::BSShader::Type::Grass, 8, stage);
            assert(cache.graphicsShaderRevision == 1);
        }
        assert(resource.references == 0);
        resource.references = 1;
        map[0][8] = std::make_unique<ShaderCache::Entry>(&resource);
        cache.evictDuringLookup = true;
        assert(!retain() && resource.references == 0 && cache.graphicsShaderRevision == 2);
    }
}
""".replace("METHODS", methods)
        result = compile_fixture(driver)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_concurrent_graphics_shader_publication(self):
        source = (ROOT / "src/ShaderCache.cpp").read_text(encoding="utf-8")
        methods = "\n".join(block(source, signature) for signature in (
            "RE::BSGraphics::VertexShader* ShaderCache::MakeAndAddVertexShader(",
            "RE::BSGraphics::PixelShader* ShaderCache::MakeAndAddPixelShader("))
        header = (ROOT / "src/ShaderCache.h").read_text(encoding="utf-8")
        methods += publication_generation_methods(header)
        driver = r'''
#include "shader_publication_fixture.h"
METHODS
int main() {
    for (bool vertex : {false, true}) {
        ShaderCache cache;
        RE::BSShader shader;
        const auto publish = [&](uint32_t descriptor, std::optional<uint64_t> generation = {}) -> const void* {
            if (vertex) return cache.MakeAndAddVertexShader(shader, descriptor, generation);
            return cache.MakeAndAddPixelShader(shader, descriptor, generation);
        };
        testDevice.creates = resource.releases = 0;
        concurrent = true;
        const void* workerResult = nullptr;
        std::thread worker([&] { workerResult = publish(7); });
        const void* drawResult = publish(7);
        worker.join();
        concurrent = false;
        assert(drawResult && drawResult == workerResult);
        assert(testDevice.creates == 1 && resource.releases == 0 && cache.graphicsShaderRevision == 0);
        assert(publish(7) == drawResult && testDevice.creates == 1);
        assert(!publish(7, 0) && !publish(8, 0) && testDevice.creates == 1);
        failCompile = true;
        assert(!publish(8) && testDevice.creates == 1);
        failCompile = false;
        testDevice.fail = true;
        assert(!publish(8) && testDevice.creates == 2);
        testDevice.fail = false;
        assert(publish(8) && testDevice.creates == 3 && resource.releases == 0);
        const auto previousGeneration = cache.compilationSet.LiveGeneration(RE::BSShader::Type::Water);
        cache.compilationSet.typeEpochs[0].fetch_add(1);
        assert(!publish(7, previousGeneration) && !publish(8, previousGeneration));
        assert(testDevice.creates == 3);
    }
}
'''.replace("METHODS", methods)
        result = compile_fixture(driver)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_graphics_revision_invalidation_coverage(self):
        source = (ROOT / "src/ShaderCache.cpp").read_text(encoding="utf-8")
        for signature in ("void ShaderCache::Clear()", "void ShaderCache::Clear(RE::BSShader::Type"):
            with self.subTest(signature=signature):
                self.assertIn("graphicsShaderRevision.fetch_add(1, std::memory_order_release)", block(source, signature))

    def test_paired_shader_readiness(self):
        source = (ROOT / "src/Features/GrassOptimizations/GrassBucketRenderer.cpp").read_text(encoding="utf-8")
        method = block(source, "bool GrassBucketRenderer::Impl::PrepareDrawShaders(")
        driver = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>
struct ID3D11VertexShader {};
struct ID3D11PixelShader {};
namespace RE { struct BSShader { enum class Type { Grass }; }; }
template<class T> struct Retained { T* value = nullptr; explicit operator bool() const { return value != nullptr; } };
namespace SIE {
struct ShaderCache {
    enum class GrassShaderTechniques { RenderDepth = 8 };
    enum class GrassShaderFlags { NoCollision = 0x400, BasicCollision = 0x800, AlphaTest = 0x10000 };
    static constexpr uint32_t GetCharacterCategoryFlag(RE::BSShader::Type) { return 0x200; }
    struct Vertex { ID3D11VertexShader* shader; };
    struct Pixel { ID3D11PixelShader* shader; };
    std::array<ID3D11VertexShader, 2> vertexObjects;
    std::array<ID3D11PixelShader, 8> pixelObjects;
    std::array<Vertex, 2> vertices;
    std::array<Pixel, 8> pixels;
    uint64_t revision = 0;
    uint64_t GetGraphicsShaderRevision() const { return revision; }
    unsigned mutateAt = 0;
    unsigned calls = 0;
    uint32_t lastCollisionFlag = 0;
    int missing = -1;
    unsigned throwAt = 0;
    ShaderCache() {
        for (unsigned i = 0; i < vertices.size(); ++i) vertices[i].shader = &vertexObjects[i];
        for (unsigned i = 0; i < pixels.size(); ++i) pixels[i].shader = &pixelObjects[i];
    }
    Retained<ID3D11VertexShader> RetainVertexShader(RE::BSShader&, uint32_t descriptor) {
        assert((descriptor & ~(8u | 0x400u | 0x800u)) == 0);
        lastCollisionFlag = descriptor & 0xC00u;
        const auto index = (descriptor & 8u) ? 1 : 0;
        ++calls;
        if (calls == mutateAt) ++revision;
        if (calls == throwAt) throw std::runtime_error("Injected shader-cache failure");
        return {missing == index ? nullptr : vertices[index].shader};
    }
    Retained<ID3D11PixelShader> RetainPixelShader(RE::BSShader&, uint32_t descriptor) {
        assert((descriptor & ~(8u | 0x10000u | 0x200u)) == 0);
        const auto index = ((descriptor & 8) ? 4 : 0) + ((descriptor & 0x10000) ? 2 : 0) + ((descriptor & 0x200) ? 1 : 0);
        ++calls;
        if (calls == mutateAt) ++revision;
        if (calls == throwAt) throw std::runtime_error("Injected shader-cache failure");
        return {missing == index + 2 ? nullptr : pixels[index].shader};
    }
};
}
namespace globals { SIE::ShaderCache* shaderCache; }
class GrassBucketRenderer {
public:
    struct Impl {
        RE::BSShader native;
        RE::BSShader* currentNativeShader = &native;
        bool drawShadersChecked = false, drawShadersReady = false;
        uint64_t drawShaderRevision = 0;
        uint32_t drawCollisionFlag = 0;
        bool frameCollisionEnabled = true, frameCollisionBasic = false;
        std::array<Retained<ID3D11VertexShader>, 2> drawVertexShaders;
        std::array<Retained<ID3D11PixelShader>, 8> drawPixelShaders;
        bool PrepareDrawShaders();
    };
};
METHOD
int main() {
    {
        SIE::ShaderCache cache;
        globals::shaderCache = &cache;
        GrassBucketRenderer::Impl renderer;
        assert(renderer.PrepareDrawShaders() && cache.calls == 10);
        renderer.frameCollisionEnabled = false;
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders() && cache.calls == 20 && cache.lastCollisionFlag == 0x400);
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders() && cache.calls == 20);
        renderer.frameCollisionEnabled = true;
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders() && cache.calls == 30 && cache.lastCollisionFlag == 0);
        renderer.frameCollisionBasic = true;
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders() && cache.calls == 40 && cache.lastCollisionFlag == 0x800);
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders() && cache.calls == 40);
        renderer.frameCollisionEnabled = false;
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders() && cache.calls == 50 && cache.lastCollisionFlag == 0x400);
    }
    for (unsigned failure = 1; failure <= 10; ++failure) {
        SIE::ShaderCache cache;
        globals::shaderCache = &cache;
        cache.throwAt = failure;
        GrassBucketRenderer::Impl renderer;
        bool threw = false;
        try { renderer.PrepareDrawShaders(); } catch (const std::runtime_error&) { threw = true; }
        assert(threw && !renderer.drawShadersReady);
        cache.throwAt = 0;
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders());
        ++cache.revision;
        renderer.drawShadersChecked = false;
        cache.throwAt = cache.calls + failure;
        threw = false;
        try { renderer.PrepareDrawShaders(); } catch (const std::runtime_error&) { threw = true; }
        assert(threw && !renderer.drawShadersReady);
    }
    for (int missing = -1; missing < 10; ++missing) {
        SIE::ShaderCache cache;
        globals::shaderCache = &cache;
        cache.missing = missing;
        GrassBucketRenderer::Impl renderer;
        assert(renderer.PrepareDrawShaders() == (missing == -1));
        assert(cache.calls == 10);
        cache.missing = -1;
        assert(renderer.PrepareDrawShaders() == (missing == -1));
        assert(cache.calls == 10);
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders());
        const auto calls = cache.calls;
        assert(calls == (missing == -1 ? 10u : 20u));
        for (unsigned i = 0; i < cache.vertices.size(); ++i) assert(renderer.drawVertexShaders[i].value == &cache.vertexObjects[i]);
        for (unsigned i = 0; i < cache.pixels.size(); ++i) assert(renderer.drawPixelShaders[i].value == &cache.pixelObjects[i]);
        cache.missing = 0;
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders() && cache.calls == calls);
        ++cache.revision;
        assert(renderer.PrepareDrawShaders() && cache.calls == calls);
        renderer.drawShadersChecked = false;
        assert(!renderer.PrepareDrawShaders() && cache.calls == calls + 10);
        cache.missing = -1;
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders() && cache.calls == calls + 20);
    }
    for (unsigned mutation = 1; mutation <= 10; ++mutation) {
        SIE::ShaderCache cache;
        globals::shaderCache = &cache;
        cache.mutateAt = mutation;
        GrassBucketRenderer::Impl renderer;
        assert(!renderer.PrepareDrawShaders());
        renderer.drawShadersChecked = false;
        assert(renderer.PrepareDrawShaders());
    }
}
'''.replace("METHOD", method)
        result = compile_fixture(driver)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_collision_diagnostic_failure_without_heap(self):
        source = (ROOT / "src/Features/GrassCollision.cpp").read_text(encoding="utf-8")
        method = block(source, "void GrassCollision::RecordDiagnosticsFailure(")
        driver = r'''
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <new>
bool failAllocation = false;
void* operator new(std::size_t size) {
    if (failAllocation) throw std::bad_alloc{};
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc{};
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
struct GrassCollision {
    std::atomic_bool diagnosticsFailed{false};
    std::mutex diagnosticsMutex;
    std::array<char, 512> diagnosticsError{};
    void RecordDiagnosticsFailure(const char*, const std::exception&) noexcept;
};
METHOD
struct Failure : std::exception {
    const char* text;
    explicit Failure(const char* value) : text(value) {}
    const char* what() const noexcept override { return text; }
};
int main() {
    GrassCollision collision;
    const Failure shortError("readback failed");
    std::array<char, 1024> longMessage;
    longMessage.fill('x');
    longMessage.back() = 0;
    const Failure longError(longMessage.data());
    failAllocation = true;
    collision.RecordDiagnosticsFailure("field observations", shortError);
    assert(collision.diagnosticsFailed.load());
    assert(std::strcmp(collision.diagnosticsError.data(), "field observations: readback failed") == 0);
    collision.RecordDiagnosticsFailure("draw bindings", longError);
    assert(std::strncmp(collision.diagnosticsError.data(), "draw bindings: ", 15) == 0);
    assert(std::strlen(collision.diagnosticsError.data()) == collision.diagnosticsError.size() - 1);
    failAllocation = false;
}
'''.replace("METHOD", method)
        result = compile_fixture(driver, devbench=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_production_setup_and_draw(self):
        source = (ROOT / "src/Features/GrassOptimizations/GrassBucketRenderer.cpp").read_text(encoding="utf-8")
        for devbench in (False, True):
            with self.subTest(devbench=devbench):
                result = run_fixture(source, devbench)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
