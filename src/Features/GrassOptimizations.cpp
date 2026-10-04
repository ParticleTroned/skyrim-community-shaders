#include "GrassOptimizations.h"

#include "Globals.h"
#include "GpuPass.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

#include <array>
#include <bit>
#include <cstring>
#include <d3d11_1.h>

namespace
{
	constexpr UINT kBatchBufferSlot = 9;
	constexpr UINT kFadeResourceSlot = 2;

	class ScopedComputeBindings
	{
	public:
		explicit ScopedComputeBindings(ID3D11DeviceContext1* ctx) : context(ctx)
		{
			context->CSGetShader(shader.put(), classes.data(), &classCount);
			for (UINT i = 0; i < buffers.size(); ++i)
				context->CSGetConstantBuffers1(i, 1, buffers[i].put(), &first[i], &count[i]);
			context->CSGetShaderResources(0, 1, resource.put());
			context->CSGetUnorderedAccessViews(0, 1, output.put());
		}
		~ScopedComputeBindings()
		{
			ID3D11UnorderedAccessView* emptyOutput = nullptr;
			ID3D11ShaderResourceView* emptyResource = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &emptyOutput, nullptr);
			context->CSSetShaderResources(0, 1, &emptyResource);
			for (UINT i = 0; i < buffers.size(); ++i) {
				auto buffer = buffers[i].get();
				context->CSSetConstantBuffers1(i, 1, &buffer, &first[i], &count[i]);
			}
			auto srv = resource.get();
			auto uav = output.get();
			context->CSSetShaderResources(0, 1, &srv);
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
			context->CSSetShader(shader.get(), classes.data(), classCount);
			for (UINT i = 0; i < classCount; ++i)
				classes[i]->Release();
		}
		ScopedComputeBindings(const ScopedComputeBindings&) = delete;
		ScopedComputeBindings& operator=(const ScopedComputeBindings&) = delete;

	private:
		ID3D11DeviceContext1* context;
		winrt::com_ptr<ID3D11ComputeShader> shader;
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> classes{};
		UINT classCount = static_cast<UINT>(classes.size());
		std::array<winrt::com_ptr<ID3D11Buffer>, 2> buffers;
		std::array<UINT, 2> first{}, count{};
		winrt::com_ptr<ID3D11ShaderResourceView> resource;
		winrt::com_ptr<ID3D11UnorderedAccessView> output;
	};

	class ScopedBatchBindings
	{
	public:
		ScopedBatchBindings(ID3D11DeviceContext1* ctx, ID3D11Buffer* batch, ID3D11ShaderResourceView* fade) : context(ctx)
		{
			context->VSGetConstantBuffers1(kBatchBufferSlot, 1, previousBuffer.put(), &first, &count);
			context->VSGetShaderResources(kFadeResourceSlot, 1, previousResource.put());
			context->VSSetConstantBuffers(kBatchBufferSlot, 1, &batch);
			context->VSSetShaderResources(kFadeResourceSlot, 1, &fade);
		}
		~ScopedBatchBindings()
		{
			auto cb = previousBuffer.get();
			auto srv = previousResource.get();
			context->VSSetConstantBuffers1(kBatchBufferSlot, 1, &cb, &first, &count);
			context->VSSetShaderResources(kFadeResourceSlot, 1, &srv);
		}
		ScopedBatchBindings(const ScopedBatchBindings&) = delete;
		ScopedBatchBindings& operator=(const ScopedBatchBindings&) = delete;

	private:
		ID3D11DeviceContext1* context;
		winrt::com_ptr<ID3D11Buffer> previousBuffer;
		winrt::com_ptr<ID3D11ShaderResourceView> previousResource;
		UINT first = 0, count = 0;
	};

	HRESULT Upload(ID3D11DeviceContext* context, ID3D11Buffer* buffer, const void* bytes, size_t length)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		const auto result = context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(result))
			return result;
		std::memcpy(mapped.pData, bytes, length);
		context->Unmap(buffer, 0);
		return S_OK;
	}
}

std::pair<std::string, std::vector<std::string>> GrassOptimizations::GetFeatureSummary()
{
	return { "Combines grass groups that share the same placement and material to reduce draw calls.",
		{ "Preserves grass density and viewing distance", "Preserves grass fading, wind and collision", "Can be switched on or off in game" } };
}

void GrassOptimizations::SetEnabled(bool enabled)
{
	if (requestedEnabled.exchange(enabled, std::memory_order_relaxed) != enabled)
		requestedGeneration.fetch_add(1, std::memory_order_release);
}

void GrassOptimizations::DrawSettings()
{
	bool enabled = IsEnabled();
	if (ImGui::Checkbox("Combine grass draws", &enabled))
		SetEnabled(enabled);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Combine compatible grass draws without reducing density or viewing distance.\nPerformance depends on the grass mod and scene.");
	if (!hookInstalled)
		ImGui::TextUnformatted("Native grass rendering: draw hook unavailable.");
}

void GrassOptimizations::LoadSettings(json& settings)
{
	SetEnabled(settings.contains("Enabled") && settings["Enabled"].is_boolean() && settings["Enabled"].get<bool>());
}

void GrassOptimizations::SaveSettings(json& settings)
{
	settings = { { "Enabled", IsEnabled() } };
}

void GrassOptimizations::RestoreDefaultSettings() { SetEnabled(false); }

void GrassOptimizations::ForgetGeometry()
{
	meshIdentity = nullptr;
	groups.clear();
	groupCounts.clear();
	ranges.clear();
	batchDrawn = false;
	groupCursor = 0;
	instanceCount = 0;
}

void GrassOptimizations::LatchFailure(const char* operation, HRESULT result)
{
	if (!resourceFailure)
		logger::error("Grass draw batching disabled until toggle or shader reset: {} failed (0x{:08X})", operation, static_cast<uint32_t>(result));
	resourceFailure = true;
}

void GrassOptimizations::SetupResources()
{
	ForgetGeometry();
	mergedInstances.reset();
	instanceFades.reset();
	capacity = 0;
	context1 = nullptr;
	disabledConstants.reset();
	enabledConstants.reset();
	dispatchConstants.reset();
	groupTable.reset();
	fadeShader.Reset();
	resourceFailure = false;
	try {
		auto desc = ConstantBufferDesc(16, false);
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		std::array<uint32_t, 4> disabled{};
		D3D11_SUBRESOURCE_DATA data{ disabled.data(), 0, 0 };
		// Immutable buffers require initialization for every byte in the allocation.
		desc.ByteWidth = sizeof(disabled);
		disabledConstants = std::make_unique<Buffer>(desc, &data, "GrassOptimizations::NativeConstants");
		disabled[0] = 1;
		enabledConstants = std::make_unique<Buffer>(desc, &data, "GrassOptimizations::BatchConstants");
		dispatchConstants = std::make_unique<Buffer>(ConstantBufferDesc(16), nullptr, "GrassOptimizations::DispatchConstants");
		auto tableDesc = StructuredBufferDesc<GrassDrawBatch::Range>(static_cast<uint64_t>(GrassDrawBatch::kFadeGroups), false, true);
		groupTable = std::make_unique<Buffer>(tableDesc, nullptr, "GrassOptimizations::GroupRanges");
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srv.Buffer.NumElements = GrassDrawBatch::kFadeGroups;
		groupTable->CreateSRV(srv);
		DX::ThrowIfFailed(globals::d3d::context->QueryInterface(__uuidof(ID3D11DeviceContext1), context1.put_void()));
		if (!fadeShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassBatchFadeCS.hlsl", {}, "cs_5_0", "main", "GrassOptimizations::BatchFadeCS"))
			LatchFailure("fade shader compilation", E_FAIL);
	} catch (const DX::com_exception& error) {
		LatchFailure("resource setup", error.Error());
	} catch (const std::exception& error) {
		logger::error("Grass batch resource setup: {}", error.what());
		LatchFailure("resource setup", E_FAIL);
	}
}

void GrassOptimizations::ClearShaderCache()
{
	ForgetGeometry();
	fadeShader.Reset();
	resourceFailure = false;
}

void GrassOptimizations::PrepareGeometry(RE::BSRenderPass* pass)
{
	ForgetGeometry();
	// Cached variants can outlive a feature toggle; their native branch must remain initialized.
	if (globals::d3d::context) {
		auto buffer = disabledConstants ? disabledConstants->resource.get() : nullptr;
		globals::d3d::context->VSSetConstantBuffers(kBatchBufferSlot, 1, &buffer);
	}
	const auto generation = requestedGeneration.load(std::memory_order_acquire);
	if (resourceGeneration != generation) {
		resourceGeneration = generation;
		if (resourceFailure)
			fadeShader.Reset();
		resourceFailure = false;
	}
	if (!loaded || !IsEnabled() || !hookInstalled || resourceFailure || !pass || !pass->geometry)
		return;
	if (!context1 || !disabledConstants || !enabledConstants || !dispatchConstants || !groupTable) {
		SetupResources();
		if (resourceFailure)
			return;
	}
	auto* shape = netimmerse_cast<RE::BSMultiStreamInstanceTriShape*>(pass->geometry);
	if (!shape)
		return;
	const auto& nativeGroups = shape->GetMultiStreamTrishapeRuntimeData().instanceGroups;
	if (2u * shape->GetMultiStreamTrishapeRuntimeData().instanceSize != GrassDrawBatch::kInstanceStride ||
		nativeGroups.size() > GrassDrawBatch::kFadeGroups)
		return;
	try {
		for (uint32_t index = 0; index < nativeGroups.size(); ++index) {
			auto* group = nativeGroups[index];
			if (!group || !group->isVisible || !group->instanceCount)
				continue;
			if (!group->vertexBuffer || !group->vertexBuffer->buffer) {
				ForgetGeometry();
				return;
			}
			winrt::com_ptr<ID3D11Buffer> source;
			source.copy_from(reinterpret_cast<ID3D11Buffer*>(group->vertexBuffer->buffer));
			D3D11_BUFFER_DESC desc{};
			source->GetDesc(&desc);
			const uint64_t bytes = static_cast<uint64_t>(group->instanceCount) * GrassDrawBatch::kInstanceStride;
			if (bytes > desc.ByteWidth || desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) {
				ForgetGeometry();
				return;
			}
			groups.push_back({ group->vertexBuffer, std::move(source), group->instanceCount });
			groupCounts.push_back({ group->instanceCount, index });
		}
		if (!GrassDrawBatch::BuildRanges(groupCounts, ranges, instanceCount)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (diagnosticsEnabled.load(std::memory_order_relaxed) && groups.size() > 1)
				++capacityFallbacks;
#endif
			ForgetGeometry();
			return;
		}
		meshIdentity = shape->GetGeometryRuntimeData().rendererData;
		vertexDescriptor = std::bit_cast<uint64_t>(shape->GetGeometryRuntimeData().vertexDesc);
		triangleCount = shape->GetTrishapeRuntimeData().triangleCount;
	} catch (const std::bad_alloc&) {
		ForgetGeometry();
		LatchFailure("group snapshot", E_OUTOFMEMORY);
	}
}

bool GrassOptimizations::HasBatchShader() const
{
	const auto state = globals::state;
	if (!loaded || !globals::shaderCache->IsEnabled() || !globals::shaderCache->IsEnableRequested() || !state->currentShader ||
		state->currentShader->shaderType.get() != RE::BSShader::Type::Grass || !globals::game::currentVertexShader)
		return false;
	const auto shader = globals::shaderCache->GetVertexShaderIfCached(*state->currentShader, state->modifiedVertexDescriptor);
	if (!shader || shader != *globals::game::currentVertexShader)
		return false;
	winrt::com_ptr<ID3D11VertexShader> bound;
	globals::d3d::context->VSGetShader(bound.put(), nullptr, nullptr);
	return bound.get() == reinterpret_cast<ID3D11VertexShader*>(shader->shader);
}

bool GrassOptimizations::EnsureCapacity(uint32_t instances)
{
	if (instances <= capacity && mergedInstances && instanceFades)
		return true;
	try {
		const auto nextCapacity = std::bit_ceil(instances);
		D3D11_BUFFER_DESC vertexDesc{};
		vertexDesc.ByteWidth = nextCapacity * GrassDrawBatch::kInstanceStride;
		vertexDesc.Usage = D3D11_USAGE_DEFAULT;
		vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		auto vertices = std::make_unique<Buffer>(vertexDesc, nullptr, "GrassOptimizations::MergedInstances");
		auto fades = std::make_unique<Buffer>(StructuredBufferDesc<float>(static_cast<uint64_t>(nextCapacity), true, false), nullptr, "GrassOptimizations::InstanceFades");
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srv.Buffer.NumElements = nextCapacity;
		fades->CreateSRV(srv);
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.NumElements = nextCapacity;
		fades->CreateUAV(uav);
		mergedInstances = std::move(vertices);
		instanceFades = std::move(fades);
		capacity = nextCapacity;
		mergedVertexBuffer = { reinterpret_cast<REX::W32::ID3D11Buffer*>(mergedInstances->resource.get()), nullptr, vertexDesc.ByteWidth };
		return true;
	} catch (const DX::com_exception& error) {
		LatchFailure("batch allocation", error.Error());
		return false;
	} catch (const std::exception& error) {
		logger::error("Grass batch allocation: {}", error.what());
		LatchFailure("batch allocation", E_FAIL);
		return false;
	}
}

bool GrassOptimizations::PrepareBatch()
{
	if (!context1 || !dispatchConstants || !groupTable || !enabledConstants || resourceFailure ||
		!fadeShader.Get(L"Data\\Shaders\\GrassOptimizations\\GrassBatchFadeCS.hlsl", {}, "cs_5_0", "main", "GrassOptimizations::BatchFadeCS") ||
		!EnsureCapacity(instanceCount))
		return false;
	winrt::com_ptr<ID3D11Buffer> nativeFade;
	UINT first = 0, count = 0;
	context1->VSGetConstantBuffers1(8, 1, nativeFade.put(), &first, &count);
	if (!nativeFade || count < GrassDrawBatch::kFadeGroups / 4)
		return false;
	D3D11_BUFFER_DESC nativeDesc{};
	nativeFade->GetDesc(&nativeDesc);
	if (static_cast<uint64_t>(first) * 16 + GrassDrawBatch::kFadeGroups * sizeof(float) > nativeDesc.ByteWidth)
		return false;
	const std::array<uint32_t, 4> params{ instanceCount, static_cast<uint32_t>(ranges.size()), 0, 0 };
	auto result = Upload(context1.get(), groupTable->resource.get(), ranges.data(), ranges.size() * sizeof(ranges[0]));
	if (SUCCEEDED(result))
		result = Upload(context1.get(), dispatchConstants->resource.get(), params.data(), sizeof(params));
	if (FAILED(result)) {
		LatchFailure("batch upload", result);
		return false;
	}
#if defined(DEVBENCH_BRIDGE_ENABLED) || defined(TRACY_SUPPORT)
	CS_GPU_PASS("GrassOptimizations::PrepareBatch");
#endif
	UINT offset = 0;
	for (const auto& group : groups) {
		const UINT bytes = group.instances * GrassDrawBatch::kInstanceStride;
		const D3D11_BOX box{ 0, 0, 0, bytes, 1, 1 };
		context1->CopySubresourceRegion(mergedInstances->resource.get(), 0, offset, 0, 0, group.buffer.get(), 0, &box);
		offset += bytes;
	}
	{
		ScopedComputeBindings restore(context1.get());
		auto paramsBuffer = dispatchConstants->resource.get();
		auto fadeBuffer = nativeFade.get();
		auto table = groupTable->srv.get();
		auto output = instanceFades->uav.get();
		context1->CSSetConstantBuffers(0, 1, &paramsBuffer);
		context1->CSSetConstantBuffers1(1, 1, &fadeBuffer, &first, &count);
		context1->CSSetShaderResources(0, 1, &table);
		context1->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		context1->CSSetShader(fadeShader.get(), nullptr, 0);
		context1->Dispatch((instanceCount + 63) / 64, 1, 1);
	}
	return true;
}

void GrassOptimizations::DrawGroup(RE::BSGraphics::Renderer* renderer, RE::BSGraphics::TriShape* mesh,
	uint32_t firstTriangle, uint32_t triangles, uint32_t instances,
	RE::BSGraphics::VertexDesc descriptor, RE::BSGraphics::VertexBuffer* buffer)
{
	auto& self = globals::features::grassOptimizations;
	const bool sameGeometry = mesh == self.meshIdentity && firstTriangle == 0 && triangles == self.triangleCount &&
	                          std::bit_cast<uint64_t>(descriptor) == self.vertexDescriptor;
	if (self.batchDrawn && sameGeometry) {
		if (!instances)
			return;
		// A completed batch already contains these groups, even if the loop visits them out of order.
		for (auto& group : self.groups) {
			if (!group.consumed && buffer == group.identity && instances == group.instances) {
				group.consumed = true;
				if (++self.groupCursor == self.groups.size())
					self.ForgetGeometry();
				return;
			}
		}
	}
	const bool matching = sameGeometry && !self.batchDrawn && !self.groups.empty() &&
	                      buffer == self.groups.front().identity && instances == self.groups.front().instances;
	if (matching && self.groupCursor == 0 && self.HasBatchShader()) {
		if (self.PrepareBatch()) {
			ScopedBatchBindings restore(self.context1.get(), self.enabledConstants->resource.get(), self.instanceFades->srv.get());
#if defined(DEVBENCH_BRIDGE_ENABLED) || defined(TRACY_SUPPORT)
			CS_GPU_PASS("GrassOptimizations::DrawBatch");
#endif
			originalDraw(renderer, mesh, firstTriangle, triangles, self.instanceCount, descriptor, &self.mergedVertexBuffer);
			self.batchDrawn = true;
			self.groups.front().consumed = true;
			++self.groupCursor;
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (self.diagnosticsEnabled.load(std::memory_order_relaxed)) {
				++self.batchedDraws;
				self.combinedGroups += self.groups.size();
				self.combinedInstances += self.instanceCount;
			}
#endif
			return;
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (self.diagnosticsEnabled.load(std::memory_order_relaxed))
			++self.resourceFallbacks;
#endif
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (self.diagnosticsEnabled.load(std::memory_order_relaxed)) {
		++self.nativeDraws;
		if (matching && !self.HasBatchShader())
			++self.shaderFallbacks;
	}
#endif
	// Once one native group has drawn, the complete batch must not be drawn later in this loop.
	if (!self.batchDrawn)
		self.ForgetGeometry();
	originalDraw(renderer, mesh, firstTriangle, triangles, instances, descriptor, buffer);
}

void GrassOptimizations::PostPostLoad()
{
	if (hookInstalled)
		return;
	const auto callsite = REL::RelocationID(100847, 107637).address() + REL::Relocate(0x663, 0x64B);
	if (*reinterpret_cast<const uint8_t*>(callsite) != 0xE8) {
		logger::warn("Grass draw batching unavailable: native draw call instruction differs");
		return;
	}
	int32_t displacement = 0;
	std::memcpy(&displacement, reinterpret_cast<const void*>(callsite + 1), sizeof(displacement));
	const auto target = callsite + 5 + displacement;
	if (target != REL::RelocationID(75479, 77265).address()) {
		logger::warn("Grass draw batching unavailable: native draw call target differs");
		return;
	}
	originalDraw = SKSE::GetTrampoline().write_call<5>(callsite, DrawGroup);
	hookInstalled = true;
	logger::info("Installed native grass group batching hook");
}

#ifdef DEVBENCH_BRIDGE_ENABLED
void GrassOptimizations::SetDiagnosticsEnabled(bool enabled)
{
	diagnosticsEnabled.store(enabled, std::memory_order_relaxed);
}

json GrassOptimizations::GetDiagnostics() const
{
	return { { "loaded", loaded }, { "enabled", IsEnabled() }, { "hookInstalled", hookInstalled },
		{ "diagnosticsEnabled", diagnosticsEnabled.load(std::memory_order_relaxed) },
		{ "batchedDraws", batchedDraws.load(std::memory_order_relaxed) },
		{ "combinedGroups", combinedGroups.load(std::memory_order_relaxed) },
		{ "combinedInstances", combinedInstances.load(std::memory_order_relaxed) },
		{ "nativeDraws", nativeDraws.load(std::memory_order_relaxed) },
		{ "shaderFallbacks", shaderFallbacks.load(std::memory_order_relaxed) },
		{ "capacityFallbacks", capacityFallbacks.load(std::memory_order_relaxed) },
		{ "resourceFallbacks", resourceFallbacks.load(std::memory_order_relaxed) },
		{ "culledInstances", 0 }, { "counterScope", "cumulative while diagnostics enabled" } };
}
#endif
