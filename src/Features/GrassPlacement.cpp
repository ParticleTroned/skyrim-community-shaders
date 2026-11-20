#include "GrassPlacement.h"
#include "AdaptiveBrightness.h"
#include "Buffer.h"
#include "Globals.h"
#include "GrassOptimizations/GrassPolicy.h"
#include "GrassOptimizations/GrassRuntime.h"
#include "GrassPlacementPolicy.h"
#include "State.h"
#include "Utils/Form.h"
#include "Utils/LocationLayers.h"

#include <DirectXPackedVector.h>
#include <RE/B/BGSLocation.h>
#include <RE/G/GridCellArray.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiNode.h>
#include <RE/T/TES.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESWorldSpace.h>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace GrassPlacement
{
	namespace
	{
		constexpr UINT kVertexSlot = 10;
		constexpr size_t kMaximumShapes = 4096, kMaximumGroups = 131072, kMaximumCells = 16384;
		using Shape = RE::BSMultiStreamInstanceTriShape;
		using VertexBuffer = RE::BSGraphics::VertexBuffer;
		struct ShapeState
		{
			std::string model;
			Envelope bound;
			std::vector<VertexBuffer*> buffers;
			GrassRuntime::GroupLayout layout{};
			RE::NiPoint3 translation{};
			bool observed = false, translationOnly = false;
		};
		struct GroupState
		{
			Shape* owner = nullptr;
			Shape::InstanceGroup* group = nullptr;
			uint32_t index = 0;
			RE::NiPoint3 center{};
			Envelope bound;
			History history;
			std::optional<CellKey> sourceCell, cell;
		};
		struct CellProfile
		{
			uint32_t cell = 0, location = 0, world = 0, users = 0;
			bool attached = false;
			std::vector<const Layer*> layers;
		};
		struct Configuration
		{
			Settings global;
			std::unordered_map<std::string, Layer> rules;
			uint64_t revision = 0;
			bool active = false;
		};
		struct Runtime
		{
			std::mutex mutex;
			std::unordered_map<Shape*, ShapeState> shapes;
			std::unordered_map<VertexBuffer*, GroupState> groups;
			std::map<CellKey, CellProfile> cells;
			Configuration configuration;
			std::vector<const Layer*> interiorLayers;
			uint32_t frame = 0, world = 0, interior = 0;
			bool active = false, enabled = false, boundsDirty = false;
			float padding = 0;
			std::atomic_bool hooksReady{ false }, failed{ false }, tracking{ false };
			std::unique_ptr<ConstantBuffer> constants;
			std::array<float, 4> uploaded{};
			bool uploadedValid = false, bindingKnown = false;
			ID3D11Buffer* boundNative = nullptr;
			winrt::com_ptr<ID3D11DeviceContext> nativeContext;
			winrt::com_ptr<ID3D11DeviceContext1> nativeContext1;
		};
		Runtime& Get()
		{
			static Runtime value;
			return value;
		}
		void Fail(const char* operation, const std::exception& error)
		{
			if (!Get().failed.exchange(true))
				logger::error("Grass placement disabled after {}: {}", operation, error.what());
		}
		CellProfile& GetCell(Runtime& runtime, const CellKey& key)
		{
			if (runtime.cells.size() >= kMaximumCells && !runtime.cells.contains(key))
				throw std::length_error("Grass placement live-cell capacity exceeded");
			return runtime.cells[key];
		}
		void ReleaseCell(Runtime& runtime, GroupState& group)
		{
			if (!group.cell)
				return;
			const auto found = runtime.cells.find(*group.cell);
			if (found != runtime.cells.end() && found->second.users)
				--found->second.users;
			group.cell.reset();
		}
		void AssignCell(Runtime& runtime, GroupState& group)
		{
			const auto cell = runtime.configuration.rules.empty() ? std::nullopt : group.sourceCell;
			if (cell == group.cell)
				return;
			ReleaseCell(runtime, group);
			if (cell)
				++GetCell(runtime, *cell).users;
			group.cell = cell;
		}
		std::vector<const Layer*> LayersForCell(const Configuration& configuration, const RE::TESObjectCELL* cell)
		{
			std::vector<const Layer*> layers;
			if (!cell || configuration.rules.empty())
				return layers;
			const auto* world = cell->IsExteriorCell() ? cell->GetRuntimeData().worldSpace : nullptr;
			Util::VisitLocationLayers(world, cell->GetLocation(), cell, [&](const RE::TESForm* form) {
				if (const auto rule = configuration.rules.find(Util::GetFormFileKey(form)); rule != configuration.rules.end())
					layers.push_back(&rule->second);
			});
			return layers;
		}
		bool PublishConfiguration(Runtime& runtime)
		{
			const auto& balance = globals::features::adaptiveBrightness;
			const auto revision = balance.GetGrassPlacementRevision();
			if (revision == runtime.configuration.revision)
				return false;
			Configuration next;
			next.revision = revision;
			next.global = balance.settings.globalProfile.grassPlacement;
			next.active = Active(next.global);
			for (const auto& rule : balance.settings.locationOverrides) {
				next.active |= Active(rule.profile.grassPlacement);
				next.rules[rule.key] = { rule.profile.grassPlacement, rule.layered };
			}
			// No reader retains layer pointers outside the runtime lock.
			for (auto& [key, cell] : runtime.cells) cell.layers.clear();
			runtime.interiorLayers.clear();
			runtime.configuration = std::move(next);
			return true;
		}
		ShapeState& GetShape(Runtime& runtime, Shape* shape)
		{
			if (runtime.shapes.size() >= kMaximumShapes && !runtime.shapes.contains(shape))
				throw std::length_error("Grass placement shape capacity exceeded");
			return runtime.shapes[shape];
		}
		void PadShape(Runtime& runtime, Shape* shape, ShapeState& source)
		{
			source.bound.Apply(shape->worldBound.radius, runtime.padding);
		}
		void RestoreGroupBounds(Shape* shape, GroupState& group)
		{
			const auto& attached = shape->GetMultiStreamTrishapeRuntimeData().instanceGroups;
			if (group.owner == shape && group.index < attached.size() && attached[group.index] == group.group)
				group.bound.Restore(group.group->size.z);
		}
		std::optional<CellKey> ReadSourceCell(const Shape::InstanceGroup& group, const RE::NiPoint3& origin)
		{
			const auto bytes = uint64_t(group.instanceCount) * GrassPolicy::kRecordBytes;
			if (!group.vertexBuffer || !GrassRuntime::MatchesInstanceBytes(*group.vertexBuffer, bytes) ||
				!GrassRuntime::HasInstanceData(*group.vertexBuffer, bytes))
				return {};
			std::array<uint16_t, 2> root{};
			std::memcpy(root.data(), group.vertexBuffer->m_data, sizeof(root));
			return SourceCell(origin.x + DirectX::PackedVector::XMConvertHalfToFloat(root[0]),
				origin.y + DirectX::PackedVector::XMConvertHalfToFloat(root[1]));
		}
		void RefreshGroups(Runtime& runtime, Shape* shape, ShapeState& source)
		{
			const auto& groups = shape->GetMultiStreamTrishapeRuntimeData().instanceGroups;
			source.translationOnly = GrassRuntime::HasTranslationOnly(shape->world);
			for (uint32_t index = 0; index < groups.size(); ++index) {
				auto* group = groups[index];
				if (!group || !group->vertexBuffer)
					continue;
				if (runtime.groups.size() >= kMaximumGroups && !runtime.groups.contains(group->vertexBuffer))
					throw std::length_error("Grass placement group capacity exceeded");
				auto& entry = runtime.groups[group->vertexBuffer];
				if (entry.owner && (entry.owner != shape || entry.group != group))
					throw std::runtime_error("Grass placement requires uniquely owned native group buffers");
				if (!entry.owner) {
					// Publish the owner's key before the entry can outlive a failed allocation.
					source.buffers.push_back(group->vertexBuffer);
					entry.owner = shape;
					entry.group = group;
				}
				entry.index = index;
				const auto cell = source.translationOnly ? ReadSourceCell(*group, shape->world.translate) : std::nullopt;
				if (entry.center != group->center || entry.sourceCell != cell)
					entry.history = {};
				entry.center = group->center;
				entry.sourceCell = cell;
				AssignCell(runtime, entry);
				entry.bound.Apply(group->size.z, runtime.padding);
			}
			// Reconcile after scanning: native removal can move a surviving group to another index.
			std::erase_if(source.buffers, [&](auto* buffer) {
				const auto found = runtime.groups.find(buffer);
				if (found == runtime.groups.end() || found->second.owner != shape)
					return true;
				auto& entry = found->second;
				if (entry.index < groups.size() && groups[entry.index] == entry.group && entry.group->vertexBuffer == buffer)
					return false;
				RestoreGroupBounds(shape, entry);
				ReleaseCell(runtime, entry);
				runtime.groups.erase(found);
				return true;
			});
			source.layout = GrassRuntime::CaptureGroupLayout(shape->GetMultiStreamTrishapeRuntimeData());
			source.translation = shape->world.translate;
			source.observed = true;
		}
		void RefreshBounds(Runtime& runtime)
		{
			std::vector<RE::NiPointer<RE::NiNode>> parents;
			{
				const GrassRuntime::GroupReadLock groupLock;
				std::scoped_lock lock(runtime.mutex);
				for (auto& [shape, source] : runtime.shapes) {
					PadShape(runtime, shape, source);
					if (shape->parent && std::none_of(parents.begin(), parents.end(), [&](const auto& parent) { return parent.get() == shape->parent; }))
						parents.emplace_back(shape->parent);
					if (runtime.padding != 0)
						RefreshGroups(runtime, shape, source);
					else {
						for (auto* buffer : source.buffers)
							if (auto found = runtime.groups.find(buffer); found != runtime.groups.end())
								RestoreGroupBounds(shape, found->second);
						source.buffers.clear();
						source.observed = false;
					}
				}
				if (runtime.padding == 0) {
					runtime.groups.clear();
					runtime.cells.clear();
				}
			}
			RE::NiUpdateData update{};
			for (const auto& parent : parents) parent->UpdateUpwardPass(update);
			std::scoped_lock lock(runtime.mutex);
			runtime.boundsDirty = false;
		}
		bool PublishFrame(Runtime& runtime)
		{
			const auto& balance = globals::features::adaptiveBrightness;
			auto* state = globals::state;
			std::scoped_lock lock(runtime.mutex);
			if (runtime.failed) {
				runtime.enabled = runtime.active = false;
				runtime.boundsDirty |= runtime.padding != 0;
				runtime.padding = 0;
				runtime.tracking.store(false, std::memory_order_release);
				return runtime.boundsDirty;
			}
			const bool changed = PublishConfiguration(runtime);
			runtime.frame = state ? state->frameCount : 0;
			runtime.enabled = IsAvailable() && state && balance.IsRuntimeEnabled() && !state->IsWorldLoadTransitionActive();
			const bool active = runtime.enabled && runtime.configuration.active;
			const float oldPadding = runtime.padding;
			runtime.padding = active || runtime.active ? kMaximumOffset : 0;
			runtime.active = active;
			runtime.boundsDirty |= runtime.padding != oldPadding;
			runtime.tracking.store(runtime.padding != 0, std::memory_order_release);
			if (runtime.padding == 0)
				return runtime.boundsDirty;
			auto* tes = globals::game::tes;
			const auto* world = tes ? tes->GetRuntimeData2().worldSpace : nullptr;
			const uint32_t worldID = world ? world->GetFormID() : 0;
			const uint32_t interiorID = tes && tes->interiorCell ? tes->interiorCell->GetFormID() : 0;
			const bool environmentChanged = runtime.world != worldID || runtime.interior != interiorID;
			if (environmentChanged) {
				runtime.cells.clear();
				for (auto& [buffer, group] : runtime.groups) {
					group.history = {};
					group.cell.reset();
					AssignCell(runtime, group);
				}
			}
			runtime.world = worldID;
			runtime.interior = interiorID;
			if (changed || environmentChanged) {
				if (changed && !environmentChanged)
					for (auto& [buffer, group] : runtime.groups) AssignCell(runtime, group);
				runtime.interiorLayers = LayersForCell(runtime.configuration, tes ? tes->interiorCell : nullptr);
				for (auto& [key, profile] : runtime.cells)
					profile.layers = LayersForCell(runtime.configuration, RE::TESForm::LookupByID<RE::TESObjectCELL>(profile.cell));
			}
			for (auto& [key, cell] : runtime.cells) cell.attached = false;
			if (active && !runtime.configuration.rules.empty() && tes && !tes->interiorCell && tes->gridCells) {
				const auto length = tes->gridCells->length;
				for (uint32_t x = 0; x < length; ++x)
					for (uint32_t y = 0; y < length; ++y) {
						auto* cell = tes->gridCells->GetCell(x, y);
						if (!cell || !cell->IsAttached() || !cell->IsExteriorCell())
							continue;
						const auto* coordinates = cell->GetCoordinates();
						if (!coordinates)
							continue;
						auto& profile = GetCell(runtime, { coordinates->cellX, coordinates->cellY });
						const auto* location = cell->GetLocation();
						const uint32_t locationID = location ? location->GetFormID() : 0;
						if (profile.cell != cell->GetFormID() || profile.location != locationID || profile.world != worldID) {
							profile.layers = LayersForCell(runtime.configuration, cell);
							profile.cell = cell->GetFormID();
							profile.location = locationID;
							profile.world = worldID;
						}
						profile.attached = true;
					}
			}
			std::erase_if(runtime.cells, [](const auto& entry) { return !entry.second.users && !entry.second.attached; });
			return runtime.boundsDirty;
		}
	}

	bool IsAvailable() { return Get().hooksReady && !Get().failed && globals::features::adaptiveBrightness.loaded; }
	void SetHooksReady(bool ready) { Get().hooksReady = ready; }
	void UpdateFrame()
	{
		auto& runtime = Get();
		if (!runtime.hooksReady)
			return;
		try {
			if (PublishFrame(runtime))
				RefreshBounds(runtime);
		} catch (const std::exception& error) {
			Fail("frame publication", error);
		}
	}
	void RecordModel(Shape* shape, const char* path)
	try {
		if (!shape || !IsAvailable())
			return;
		auto& runtime = Get();
		std::scoped_lock lock(runtime.mutex);
		auto& source = GetShape(runtime, shape);
		source.model = path ? ModelKey(path) : std::string{};
		PadShape(runtime, shape, source);
	} catch (const std::exception& error) {
		Fail("model tracking", error);
	}

	void ObserveGroups(Shape* shape)
	try {
		if (!shape || !IsAvailable())
			return;
		const auto property = shape->GetGeometryRuntimeData().shaderProperty.get();
		if (!property || property->GetRTTI() != REL::Relocation<const RE::NiRTTI*>{ RE::NiRTTI_BSGrassShaderProperty }.get())
			return;
		auto& runtime = Get();
		if (!runtime.tracking.load(std::memory_order_acquire)) {
			std::scoped_lock lock(runtime.mutex);
			GetShape(runtime, shape);
			return;
		}
		const GrassRuntime::GroupReadLock groupLock;
		std::scoped_lock lock(runtime.mutex);
		auto& source = GetShape(runtime, shape);
		PadShape(runtime, shape, source);
		if (runtime.padding == 0)
			return;
		const auto layout = GrassRuntime::CaptureGroupLayout(shape->GetMultiStreamTrishapeRuntimeData());
		if (source.observed && source.layout == layout && source.translation == shape->world.translate &&
			source.translationOnly == GrassRuntime::HasTranslationOnly(shape->world))
			return;
		RefreshGroups(runtime, shape, source);
	} catch (const std::exception& error) {
		Fail("group tracking", error);
	}

	void RemoveShape(Shape* shape)
	{
		auto& runtime = Get();
		std::scoped_lock lock(runtime.mutex);
		if (const auto found = runtime.shapes.find(shape); found != runtime.shapes.end())
			for (auto* buffer : found->second.buffers) {
				const auto group = runtime.groups.find(buffer);
				if (group != runtime.groups.end() && group->second.owner == shape) {
					ReleaseCell(runtime, group->second);
					runtime.groups.erase(group);
				}
			}
		runtime.shapes.erase(shape);
	}
	void BeforeGroupMutation(Shape* shape, uint32_t removed)
	{
		auto& runtime = Get();
		if (!runtime.tracking.load(std::memory_order_acquire))
			return;
		const GrassRuntime::GroupReadLock groupLock;
		std::scoped_lock lock(runtime.mutex);
		if (const auto found = runtime.shapes.find(shape); found != runtime.shapes.end()) {
			auto& source = found->second;
			source.bound.Restore(shape->worldBound.radius);
			// Remove our envelope before native code reuses or relocates any group.
			std::erase_if(source.buffers, [&](auto* buffer) {
				const auto foundGroup = runtime.groups.find(buffer);
				if (foundGroup == runtime.groups.end() || foundGroup->second.owner != shape)
					return true;
				auto& group = foundGroup->second;
				RestoreGroupBounds(shape, group);
				if (removed == UINT32_MAX || group.index != removed)
					return false;
				ReleaseCell(runtime, group);
				runtime.groups.erase(foundGroup);
				return true;
			});
			source.observed = false;
		}
	}
	void RestoreWorldBound(Shape* shape)
	{
		auto& runtime = Get();
		if (!runtime.tracking.load(std::memory_order_acquire))
			return;
		std::scoped_lock lock(runtime.mutex);
		if (auto found = runtime.shapes.find(shape); found != runtime.shapes.end())
			found->second.bound.Restore(shape->worldBound.radius);
	}
	void UpdateWorldBound(Shape* shape)
	{
		auto& runtime = Get();
		if (!runtime.tracking.load(std::memory_order_acquire))
			return;
		std::scoped_lock lock(runtime.mutex);
		if (auto found = runtime.shapes.find(shape); found != runtime.shapes.end())
			PadShape(runtime, shape, found->second);
	}
	std::array<float, 2> GetOffsets(VertexBuffer* buffer)
	{
		auto& runtime = Get();
		if (!runtime.tracking.load(std::memory_order_acquire) || runtime.failed)
			return {};
		std::scoped_lock lock(runtime.mutex);
		const auto found = runtime.groups.find(buffer);
		if (found == runtime.groups.end())
			return {};
		auto& group = found->second;
		const auto source = runtime.shapes.find(group.owner);
		if (source == runtime.shapes.end() || !source->second.translationOnly)
			return {};
		if (group.history.frame != runtime.frame) {
			std::span<const Layer* const> layers = runtime.interiorLayers;
			if (!runtime.interior && group.cell)
				if (const auto cell = runtime.cells.find(*group.cell); cell != runtime.cells.end())
					layers = cell->second.layers;
			group.history.Update(runtime.frame, runtime.enabled ? Resolve(runtime.configuration.global, layers, source->second.model) : 0);
		}
		return { group.history.current, group.history.previous };
	}
	float BoundsPadding() { return Get().tracking.load(std::memory_order_acquire) ? kMaximumOffset : 0; }

	BindingScope::BindingScope()
	{
		if (!globals::features::adaptiveBrightness.loaded)
			return;
		auto& runtime = Get();
		if (runtime.nativeContext.get() != globals::d3d::context) {
			runtime.nativeContext.copy_from(globals::d3d::context);
			runtime.nativeContext1 = nullptr;
			runtime.constants.reset();
			runtime.boundNative = nullptr;
			runtime.uploadedValid = false;
			if (runtime.nativeContext)
				runtime.nativeContext->QueryInterface(__uuidof(ID3D11DeviceContext1), runtime.nativeContext1.put_void());
		}
		context = runtime.nativeContext;
		rangedContext = runtime.nativeContext1;
		runtime.bindingKnown = false;
		if (!context)
			return;
		if (rangedContext)
			rangedContext->VSGetConstantBuffers1(kVertexSlot, 1, previous.put(), &first, &count);
		else
			context->VSGetConstantBuffers(kVertexSlot, 1, previous.put());
	}
	BindingScope::~BindingScope()
	{
		Get().bindingKnown = false;
		if (!context)
			return;
		auto* buffer = previous.get();
		if (rangedContext && buffer && (first != 0 || count != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT))
			rangedContext->VSSetConstantBuffers1(kVertexSlot, 1, &buffer, &first, &count);
		else
			context->VSSetConstantBuffers(kVertexSlot, 1, &buffer);
	}

	void PrepareNativeGeometry()
	{
		if (!globals::features::adaptiveBrightness.loaded || !globals::d3d::context)
			return;
		Get().bindingKnown = false;
		BindNative(nullptr);
	}

	void ApplyNativeBinding(bool compute)
	{
		const auto* state = globals::state;
		if (compute || !globals::features::adaptiveBrightness.loaded || !state || !state->currentShader ||
			state->currentShader->shaderType != RE::BSShader::Type::Grass ||
			state->selectedCustomPixelShaderType != RE::BSShader::Type::Grass || !globals::d3d::context)
			return;
		auto& runtime = Get();
		// Native state can overwrite borrowed constants; reassert them at the existing flush boundary.
		globals::d3d::context->VSSetConstantBuffers(kVertexSlot, 1, &runtime.boundNative);
		runtime.bindingKnown = true;
	}

	void BindNative(VertexBuffer* buffer)
	{
		auto& runtime = Get();
		auto* context = globals::d3d::context;
		if (!context)
			return;
		const auto offsets = buffer ? GetOffsets(buffer) : std::array<float, 2>{};
		ID3D11Buffer* binding = nullptr;
		try {
			if (offsets[0] != 0 || offsets[1] != 0) {
				if (!runtime.constants)
					runtime.constants = std::make_unique<ConstantBuffer>(ConstantBufferDesc(16), "GrassPlacement::NativeOffsets");
				const std::array<float, 4> data{ offsets[0], offsets[1], 0, 0 };
				if (!runtime.uploadedValid || runtime.uploaded != data) {
					runtime.constants->Update(data);
					runtime.uploaded = data;
					runtime.uploadedValid = true;
				}
				binding = runtime.constants->CB();
			}
		} catch (const std::exception& error) {
			Fail("constant-buffer upload", error);
		}
		if (!runtime.bindingKnown || runtime.boundNative != binding) {
			context->VSSetConstantBuffers(kVertexSlot, 1, &binding);
			runtime.boundNative = binding;
			runtime.bindingKnown = true;
		}
	}
}
