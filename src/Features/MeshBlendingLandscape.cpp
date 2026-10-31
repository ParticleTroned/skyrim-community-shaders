#include "MeshBlendingLandscape.h"

#include "../Globals.h"
#include "../TruePBR/BSLightingShaderMaterialPBRLandscape.h"
#include "../Util.h"
#include "../Utils/Form.h"
#include "../Utils/Game.h"
#include "MeshBlendingLandscapeSnapshot.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "../State.h"
#endif

#include <chrono>
#include <cstring>
#include <exception>
#include <iterator>
#include <mutex>

namespace
{
	namespace Landscape = CSX::MeshBlendingLandscapePolicy;
	using ShaderFlag = RE::BSShaderProperty::EShaderPropertyFlag;
	using Rejection = MeshBlendingLandscape::Rejection;
#ifdef DEVBENCH_BRIDGE_ENABLED
	bool CollectDiagnostics()
	{
		return globals::state && globals::state->IsDeveloperMode();
	}
#endif

	const RE::BSFixedString& SnapshotName()
	{
		static const RE::BSFixedString name("CSXMeshBlendingLANDSnapshot");
		return name;
	}

	const RE::BSFixedString& PublicationName()
	{
		static const RE::BSFixedString name("CSXMeshBlendingLANDPublication");
		return name;
	}

	std::span<const std::int32_t> ReadMetadata(const RE::NiObjectNET& a_owner, const RE::BSFixedString& a_name)
	{
		const auto* extra = a_owner.GetExtraData(a_name);
		static const REL::Relocation<const RE::NiRTTI*> integerRTTI{ RE::NiIntegersExtraData::Ni_RTTI };
		if (!extra || extra->GetRTTI() != integerRTTI.get())
			return {};
		const auto* integers = static_cast<const RE::NiIntegersExtraData*>(extra);
		if (!integers->value || integers->size > Landscape::MaximumSnapshotBytes / sizeof(std::int32_t))
			return {};
		return { integers->value, integers->size };
	}

	RE::NiPointer<RE::NiIntegersExtraData> CreateMetadata(const RE::BSFixedString& a_name, const std::vector<std::int32_t>& a_words)
	{
		// Use the stock engine type and heaps, checking both allocations before copying.
		auto* raw = RE::calloc<RE::NiIntegersExtraData>(1u);
		if (!raw)
			return {};
		if (!RE::stl::emplace_vtable(raw)) {
			RE::free(raw);
			return {};
		}
		RE::NiPointer<RE::NiIntegersExtraData> result(raw);
		result->name = a_name;
		result->value = RE::NiAlloc<std::int32_t>(a_words.size());
		if (!result->value)
			return {};
		std::memcpy(result->value, a_words.data(), a_words.size() * sizeof(std::int32_t));
		result->size = static_cast<std::uint32_t>(a_words.size());
		return result;
	}

	struct TextureBindings
	{
		std::array<RE::NiSourceTexture*, Landscape::LayerCount> diffuse{};
		Landscape::MaterialLayout layout = Landscape::MaterialLayout::kVanilla;
		std::uint32_t count = 0u;
	};

	Rejection ReadBindings(const RE::BSShaderMaterial* a_material, TextureBindings& a_bindings)
	{
		if (!a_material)
			return Rejection::MaterialLayout;
		std::uintptr_t vtable = 0u;
		std::memcpy(&vtable, a_material, sizeof(vtable));
		static const auto vanillaVtable = RE::BSLightingShaderMaterialLandscape::VTABLE[0].address();
		if (vtable == vanillaVtable) {
			const auto* material = static_cast<const RE::BSLightingShaderMaterialLandscape*>(a_material);
			a_bindings.layout = Landscape::MaterialLayout::kVanilla;
			a_bindings.count = material->numLandscapeTextures;
			a_bindings.diffuse[0] = material->diffuseTexture.get();
			for (std::size_t layer = 1u; layer < Landscape::LayerCount; ++layer)
				a_bindings.diffuse[layer] = material->landscapeDiffuseTexture[layer - 1u].get();
		} else if (const auto* material = BSLightingShaderMaterialPBRLandscape::GetRegistered(a_material)) {
			a_bindings.layout = Landscape::MaterialLayout::kPBR;
			a_bindings.count = material->numLandscapeTextures;
			for (std::size_t layer = 0u; layer < Landscape::LayerCount; ++layer)
				a_bindings.diffuse[layer] = material->landscapeBaseColorTextures[layer].get();
		} else {
			return Rejection::MaterialLayout;
		}
		return a_bindings.count > 0u && a_bindings.count <= Landscape::LayerCount ? Rejection::None : Rejection::LayerCount;
	}

	Rejection BuildDrawIdentity(RE::BSGeometry* a_geometry, RE::BSShaderProperty* a_property, std::uint32_t a_childIndex,
		Landscape::DrawIdentity& a_identity, TextureBindings& a_bindings)
	{
		if (!a_geometry || !a_property || !a_geometry->parent ||
			a_geometry->GetGeometryRuntimeData().shaderProperty.get() != a_property ||
			a_property->GetRTTI() != globals::rtti::BSLightingShaderPropertyRTTI.get())
			return Rejection::ShaderProperty;
		const auto* property = static_cast<const RE::BSLightingShaderProperty*>(a_property);
		const auto& children = a_geometry->parent->GetChildren();
		if (!property->flags.any(ShaderFlag::kMultiTextureLandscape) || a_childIndex >= children.capacity() ||
			children.capacity() > Landscape::MaximumQuadrantChildren || children[static_cast<std::uint16_t>(a_childIndex)].get() != a_geometry)
			return Rejection::ChildLayout;
		if (const auto reason = ReadBindings(property->material, a_bindings); reason != Rejection::None)
			return reason;
		a_identity.root = reinterpret_cast<std::uintptr_t>(a_geometry->parent);
		a_identity.geometry = reinterpret_cast<std::uintptr_t>(a_geometry);
		a_identity.shaderProperty = reinterpret_cast<std::uintptr_t>(a_property);
		a_identity.material = reinterpret_cast<std::uintptr_t>(property->material);
		a_identity.layout = a_bindings.layout;
		a_identity.textureCount = a_bindings.count;
		a_identity.childSlots = children.capacity();
		a_identity.childIndex = a_childIndex;
		for (std::size_t layer = 0u; layer < Landscape::LayerCount; ++layer)
			a_identity.textures[layer] = reinterpret_cast<std::uintptr_t>(a_bindings.diffuse[layer]);
		return Rejection::None;
	}

	std::string DiffusePath(const RE::TESLandTexture& a_texture)
	{
		auto* textureSet = Util::GetSeasonalSwap(a_texture.textureSet);
		const auto* path = textureSet ? textureSet->GetTexturePath(RE::BSTextureSet::Textures::kDiffuse) : nullptr;
		return path ? Landscape::NormalizeTexturePath(path) : std::string{};
	}

	void CaptureTexture(const RE::TESLandTexture& a_texture, Landscape::CapturedTexture& a_identity)
	{
		a_identity.present = true;
		a_identity.form = Landscape::CanonicalFormKey(Util::GetFormFileKey(&a_texture));
		const auto* editorID = a_texture.GetFormEditorID();
		a_identity.editorID = editorID ? Landscape::NormalizeIdentity(editorID) : std::string{};
		a_identity.diffuse = DiffusePath(a_texture);
		std::array<const RE::BGSMaterialType*, Landscape::MaximumMaterialDepth> parents{};
		for (const auto* material = a_texture.materialType; material; material = material->parentType) {
			if (a_identity.materialCount == parents.size() ||
				std::find(parents.begin(), parents.begin() + a_identity.materialCount, material) != parents.begin() + a_identity.materialCount) {
				a_identity.materialChainValid = false;
				break;
			}
			parents[a_identity.materialCount] = material;
			auto& captured = a_identity.materials[a_identity.materialCount++];
			captured.form = Landscape::CanonicalFormKey(Util::GetFormFileKey(material));
			const auto* materialEditorID = material->GetFormEditorID();
			captured.editorID = materialEditorID ? Landscape::NormalizeIdentity(materialEditorID) : std::string{};
			captured.name = Landscape::NormalizeIdentity(material->materialName.c_str());
		}
	}

	void WarnRejectedCapture(std::size_t a_quadrant, Rejection a_reason)
	{
		static std::atomic_uint32_t warnings = 0u;
		if (warnings.fetch_add(1u, std::memory_order_relaxed) < 4u)
			logger::warn("[Mesh Blending] LAND quadrant {} rejected ({}); leaving it unblended", a_quadrant,
				MeshBlendingLandscape::RejectionNames[static_cast<std::size_t>(a_reason)]);
	}
}

MeshBlendingLandscape::MeshBlendingLandscape() :
	policy(Landscape::CompilePolicy({}))
{
	// Runtime snapshots carry a process-local stamp as well as exact current owners.
	session = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ reinterpret_cast<std::uintptr_t>(this);
	if (session == 0u)
		session = 1u;
}

void MeshBlendingLandscape::PublishPolicy(std::shared_ptr<const Policy> a_policy) noexcept
{
	if (a_policy)
		policy.store(std::move(a_policy), std::memory_order_release);
}

std::size_t MeshBlendingLandscape::GetBlendPairCount() const noexcept
{
	return policy.load(std::memory_order_acquire)->blendPairCount;
}

void MeshBlendingLandscape::BeginDiscoverySession() noexcept
{
	if (discoveryEpoch.fetch_add(1u, std::memory_order_acq_rel) == UINT64_MAX)
		discoveryEpoch.store(1u, std::memory_order_release);
}

void MeshBlendingLandscape::Capture(RE::TESObjectLAND* a_land, bool a_materialSetupSucceeded, DiscoverySink a_discovery)
{
	if (!a_land || !a_land->loadedData) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (CollectDiagnostics()) {
			const std::scoped_lock diagnosticsLock(diagnosticsMutex);
			++diagnostics.captureAttempts;
		}
		RecordRejection(true, Rejection::MissingLand);
#endif
		return;
	}
	const std::unique_lock lock(metadataMutex);
	// Revoke every previous quadrant before any allocation or layout validation can fail.
	for (auto* root : a_land->loadedData->mesh) {
		if (root)
			root->RemoveExtraData(PublicationName());
	}
	for (std::size_t quadrant = 0u; quadrant < 4u; ++quadrant) {
		if (!a_land->loadedData->mesh[quadrant] && !a_land->loadedData->defQuadTextures[quadrant] &&
			std::none_of(std::begin(a_land->loadedData->quadTextures[quadrant]), std::end(a_land->loadedData->quadTextures[quadrant]),
				[](const auto* texture) { return texture != nullptr; }))
			continue;
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (CollectDiagnostics()) {
			const std::scoped_lock diagnosticsLock(diagnosticsMutex);
			++diagnostics.captureAttempts;
		}
#endif
		try {
			const auto reason = CaptureQuadrant(a_land, quadrant, a_materialSetupSucceeded, a_discovery);
			if (reason != Rejection::None)
				WarnRejectedCapture(quadrant, reason);
#ifdef DEVBENCH_BRIDGE_ENABLED
			else if (CollectDiagnostics()) {
				const std::scoped_lock diagnosticsLock(diagnosticsMutex);
				++diagnostics.captureSuccesses;
			}
#endif
		} catch (const std::exception&) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			RecordRejection(true, Rejection::Exception, static_cast<std::uint32_t>(quadrant));
#endif
			WarnRejectedCapture(quadrant, Rejection::Exception);
		}
	}
}

MeshBlendingLandscape::Rejection MeshBlendingLandscape::CaptureQuadrant(RE::TESObjectLAND* a_land, std::size_t a_quadrant,
	bool a_materialSetupSucceeded, const DiscoverySink& a_discovery)
{
	const auto reject = [&](Rejection reason, [[maybe_unused]] std::uint32_t layer = Landscape::LayerCount,
							[[maybe_unused]] std::string_view expected = {}, [[maybe_unused]] std::string_view bound = {}) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		RecordRejection(true, reason, static_cast<std::uint32_t>(a_quadrant), layer, expected, bound);
#endif
		return reason;
	};
	auto* root = a_land->loadedData->mesh[a_quadrant];
	std::array<Landscape::CapturedTexture, Landscape::LayerCount> textures{};
	std::array<RE::TESLandTexture*, Landscape::LayerCount> records{};
	const auto currentPolicy = policy.load(std::memory_order_acquire);
	const auto observe = [&](const Landscape::CapturedTexture& texture) {
		if (a_discovery.capture && texture.present) {
			const auto view = Landscape::ViewTexture(texture);
			a_discovery.capture(a_discovery.context, view, Landscape::ClassifyTexture(view, *currentPolicy));
		}
	};
	// Recording identifies readable LTEX records even when their render layout is unsupported.
	for (std::size_t layer = 0u; layer < Landscape::LayerCount; ++layer) {
		auto* texture = layer == 0u ? a_land->loadedData->defQuadTextures[a_quadrant] : a_land->loadedData->quadTextures[a_quadrant][layer - 1u];
		if (layer == 0u && !texture && root)
			texture = Util::GetDefaultLandTexture();
		records[layer] = texture;
		if (texture) {
			CaptureTexture(*texture, textures[layer]);
			observe(textures[layer]);
		}
	}
	if (a_discovery.capture) {
		for (std::size_t extra = Landscape::LayerCount - 1u; extra < std::size(a_land->loadedData->quadTextures[a_quadrant]); ++extra) {
			if (const auto* texture = a_land->loadedData->quadTextures[a_quadrant][extra]) {
				Landscape::CapturedTexture identity;
				CaptureTexture(*texture, identity);
				observe(identity);
			}
		}
	}
	if (!a_materialSetupSucceeded)
		return reject(Rejection::MaterialSetup);
	if (!root)
		return reject(Rejection::MissingGeometry);
	const auto& children = root->GetChildren();
	if (children.empty() || children.capacity() > Landscape::MaximumQuadrantChildren)
		return reject(Rejection::ChildLayout);
	RE::BSGeometry* geometry = nullptr;
	std::uint32_t childIndex = 0u;
	for (std::uint16_t child = 0u; child < children.capacity(); ++child) {
		auto* candidate = children[child] ? children[child]->AsGeometry() : nullptr;
		auto* property = candidate ? candidate->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
		if (!property || !property->flags.any(ShaderFlag::kMultiTextureLandscape))
			continue;
		if (geometry)
			return reject(Rejection::MultipleGeometries);
		geometry = candidate;
		childIndex = child;
	}
	if (!geometry || geometry->parent != root)
		return reject(Rejection::MissingGeometry);
	Landscape::SnapshotHeader header{ session, nextPublication++, {} };
	TextureBindings bindings;
	if (const auto reason = BuildDrawIdentity(geometry, geometry->GetGeometryRuntimeData().shaderProperty.get(), childIndex, header.draw, bindings);
		reason != Rejection::None)
		return reject(reason);
	for (std::size_t layer = 0u; layer < Landscape::LayerCount; ++layer) {
		if (records[layer] && records[layer]->GetFormID() == 0u && bindings.layout == Landscape::MaterialLayout::kVanilla) {
			textures[layer] = {};
			if (const auto* texture = Util::GetDefaultLandTexture()) {
				CaptureTexture(*texture, textures[layer]);
				observe(textures[layer]);
			}
		}
		if (!textures[layer].present)
			continue;
		if (layer >= bindings.count)
			return reject(Rejection::LayerCount, static_cast<std::uint32_t>(layer));
		if (!bindings.diffuse[layer] || textures[layer].diffuse.empty())
			return reject(Rejection::MissingTexture, static_cast<std::uint32_t>(layer), textures[layer].diffuse);
		const auto boundPath = Landscape::NormalizeTexturePath(bindings.diffuse[layer]->name.c_str());
		if (textures[layer].diffuse != boundPath)
			return reject(Rejection::TextureIdentity, static_cast<std::uint32_t>(layer), textures[layer].diffuse, boundPath);
	}
	const auto words = Landscape::EncodeSnapshot(header, textures);
	if (!words)
		return reject(Rejection::SnapshotEncoding);
	auto snapshot = CreateMetadata(SnapshotName(), *words);
	auto publication = CreateMetadata(PublicationName(), Landscape::EncodePublication(header));
	if (!snapshot || !publication)
		return reject(Rejection::MetadataAllocation);
	geometry->RemoveExtraData(SnapshotName());
	if (!geometry->AddExtraData(snapshot.get()) || !root->AddExtraData(publication.get()))
		return reject(Rejection::MetadataPublication);
	return Rejection::None;
}

bool MeshBlendingLandscape::TryGetClasses(RE::BSRenderPass* a_pass, std::uint32_t& a_classes,
	DiscoverySink a_discovery, bool a_classify)
{
	a_classes = 0u;
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (CollectDiagnostics()) {
		const std::scoped_lock diagnosticsLock(diagnosticsMutex);
		++diagnostics.drawAttempts;
	}
#endif
	const auto reject = [&]([[maybe_unused]] Rejection reason) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		RecordRejection(false, reason);
#endif
		return false;
	};
	if (!a_pass || !a_pass->geometry)
		return reject(Rejection::MissingGeometry);
	const std::shared_lock lock(metadataMutex);
	const auto words = ReadMetadata(*a_pass->geometry, SnapshotName());
	if (words.empty())
		return reject(Rejection::MissingSnapshot);
	Landscape::SnapshotHeader header;
	if (!Landscape::ReadSnapshotHeader(words, header))
		return reject(Rejection::SnapshotDecoding);
	Landscape::DrawIdentity current;
	TextureBindings bindings;
	if (const auto reason = BuildDrawIdentity(a_pass->geometry, a_pass->shaderProperty, header.draw.childIndex, current, bindings);
		reason != Rejection::None)
		return reject(reason);
	if (!Landscape::MatchesPublication(ReadMetadata(*a_pass->geometry->parent, PublicationName()), header, current, session))
		return reject(Rejection::StalePublication);
	const auto currentPolicy = policy.load(std::memory_order_acquire);
	try {
		if (a_discovery.capture) {
			static thread_local Landscape::DiscoveryCache<> discoveries;
			if (!discoveries.Observe(words, header, a_discovery.epoch, *currentPolicy,
					[&](const Texture& texture, Kind kind) { a_discovery.capture(a_discovery.context, texture, kind); }))
				return reject(Rejection::SnapshotDecoding);
		}
		Landscape::QuadrantClassification classification;
		if (a_classify) {
			static thread_local Landscape::ClassificationCache<> cache;
			if (!cache.Resolve(words, header, *currentPolicy, a_classes, &classification))
				return reject(Rejection::SnapshotDecoding);
		}
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (CollectDiagnostics()) {
			const std::scoped_lock diagnosticsLock(diagnosticsMutex);
			++diagnostics.drawSuccesses;
			if (a_classify) {
				diagnostics.unknownLayerDraws += classification.unknownLayers != 0u;
				diagnostics.rejectedPairDraws += classification.pairRejected;
				diagnostics.zeroClassDraws += classification.classes == 0u;
			}
		}
#endif
		return true;
	} catch (const std::exception&) {
		static std::atomic_uint32_t warnings = 0u;
		if (warnings.fetch_add(1u, std::memory_order_relaxed) < 4u)
			logger::warn("[Mesh Blending] Could not observe resident LAND identities; leaving the draw unblended");
		return reject(Rejection::Exception);
	}
}

#ifdef DEVBENCH_BRIDGE_ENABLED
MeshBlendingLandscape::Diagnostics MeshBlendingLandscape::GetDiagnostics() const
{
	const std::scoped_lock lock(diagnosticsMutex);
	return diagnostics;
}

void MeshBlendingLandscape::ResetDiagnostics()
{
	const std::scoped_lock lock(diagnosticsMutex);
	diagnostics = {};
}

void MeshBlendingLandscape::RecordSeamBypass()
{
	if (CollectDiagnostics()) {
		const std::scoped_lock lock(diagnosticsMutex);
		++diagnostics.seamBypasses;
	}
}

void MeshBlendingLandscape::RecordRejection(bool a_capture, Rejection a_reason, std::uint32_t a_quadrant,
	std::uint32_t a_layer, std::string_view a_expected, std::string_view a_bound)
{
	if (!CollectDiagnostics())
		return;
	const std::scoped_lock lock(diagnosticsMutex);
	auto& counters = a_capture ? diagnostics.captureRejects : diagnostics.drawRejects;
	++counters[static_cast<std::size_t>(a_reason)];
	auto& detail = a_capture ? diagnostics.lastCaptureRejection : diagnostics.lastDrawRejection;
	detail.reason = a_reason;
	detail.quadrant = a_quadrant;
	detail.layer = a_layer;
	detail.expectedTexture = a_expected.substr(0u, Landscape::MaximumIdentityLength);
	detail.boundTexture = a_bound.substr(0u, Landscape::MaximumIdentityLength);
}
#endif
