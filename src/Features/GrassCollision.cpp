#include "GrassCollision.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "GpuPass.h"
#include "State.h"
#include "Utils/ActorUtils.h"
#include "Utils/ComputeState.h"
#include "Utils/D3D.h"
#include "Utils/Finite.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/AcceptedDrawService.h"
#	include "ShaderCache.h"
#	include <DirectXPackedVector.h>
#	include <cstdio>
#	include <d3dcompiler.h>
#	include <d3d11shader.h>
#	include <limits>
#endif

static constexpr uint MAX_BOUNDING_BOXES = 64;
static constexpr uint MAX_COLLISIONS_PER_BOUNDING_BOX = 3;
static constexpr uint MAX_COLLISIONS = MAX_BOUNDING_BOXES * MAX_COLLISIONS_PER_BOUNDING_BOX;
static constexpr float MAX_ACTOR_DISTANCE = 4096.0f;
static constexpr float MAX_ACTOR_SQ_DISTANCE = MAX_ACTOR_DISTANCE * MAX_ACTOR_DISTANCE;
static constexpr float MIN_COLLISION_RADIUS_DISTANCE_SCALE = 0.001f;

struct GrassCollisionActorCandidate
{
	RE::ActorHandle handle;
	float sqDistance;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GrassCollision::Settings,
	EnableGrassCollision,
	TrackRagdolls,
	CollisionStrength, CollisionRadiusScale, GrassInteractionRadius, CollisionImpactStrength,
	SpringStrength, Damping, MaximumBend, MaximumCompression, CompressionHeight,
	MaximumCompressibleGrassHeight, CompressionRecovery)

void GrassCollision::DrawSettings()
{
	MenuUI::SettingsPage page(GetShortName().c_str(), { { "bending", "Bending", "Adjust how strongly nearby actors bend grass.", "Grass collision strength", true, true, "Adjust grass bending" } });
	if (page.Is("bending")) {
		Util::Widgets::Checkbox("Enabled", &settings.EnableGrassCollision);
		auto guard = Util::DisableGuard(!settings.EnableGrassCollision);
		Util::Widgets::Checkbox("Track ragdolls", &settings.TrackRagdolls);
		for (const auto& parameter : parameters)
			Util::Widgets::SliderFloat(parameter.label, &(settings.*parameter.member), parameter.minimum, parameter.maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	}
}

void GrassCollision::DrawPerformanceSettings(bool)
{
	Util::Widgets::Checkbox("Enabled", &settings.EnableGrassCollision);
	auto guard = Util::DisableGuard(!settings.EnableGrassCollision);
	DrawStrengthControl();
}

void GrassCollision::DrawStrengthControl()
{
	float strength = GetCollisionStrength();
	if (Util::Widgets::SliderFloat("Bending strength", &strength, kMinCollisionStrength, kMaxCollisionStrength, "%.1fx", ImGuiSliderFlags_AlwaysClamp))
		SetCollisionStrength(strength);
	Util::AddTooltip("Scale grass bending around the player and other actors. 1x uses the simulated response, 0 removes deformation, and higher values amplify it up to the configured bend limit; compression is clamped to full flattening.");
}

float GrassCollision::GetCollisionStrength() const
{
	return Util::ClampFinite(settings.CollisionStrength, kMinCollisionStrength, kMaxCollisionStrength, kDefaultCollisionStrength);
}

bool GrassCollision::SetCollisionStrength(float a_strength)
{
	if (!std::isfinite(a_strength) || a_strength < kMinCollisionStrength || a_strength > kMaxCollisionStrength)
		return false;
	settings.CollisionStrength = a_strength;
	return true;
}

void GrassCollision::NormalizeSettings()
{
	const Settings defaults;
	for (const auto& parameter : parameters)
		settings.*parameter.member = Util::ClampFinite(settings.*parameter.member, parameter.minimum, parameter.maximum, defaults.*parameter.member);
}

bool GrassCollision::Configure(const json& values, std::string& error)
{
	if (!values.is_object()) {
		error = "collisionSettings must be an object";
		return false;
	}
	auto next = settings;
	for (const auto& [name, value] : values.items()) {
		if (name == "EnableGrassCollision" || name == "TrackRagdolls") {
			if (!value.is_boolean()) {
				error = name + " must be a boolean";
				return false;
			}
			(name == "EnableGrassCollision" ? next.EnableGrassCollision : next.TrackRagdolls) = value.get<bool>();
			continue;
		}
		const auto parameter = std::find_if(parameters.begin(), parameters.end(), [&](const auto& item) { return name == item.name; });
		if (parameter == parameters.end() || !value.is_number()) {
			error = "Unknown or nonnumeric collision parameter: " + name;
			return false;
		}
		const double number = value.get<double>();
		if (!std::isfinite(number) || number < parameter->minimum || number > parameter->maximum) {
			error = name + " is outside its finite supported range";
			return false;
		}
		next.*parameter->member = static_cast<float>(number);
	}
	settings = next;
	return true;
}

json GrassCollision::CapturePerformanceSettingsState() const
{
	return settings;
}

void GrassCollision::QueueCollisions()
{
	queuedBoundingBoxes.clear();
	queuedCollisions.clear();
	if (historyResetRequested.exchange(false) || !settings.EnableGrassCollision)
		colliderHistory.clear();
	if (!loaded || resourceFailed || !settings.EnableGrassCollision) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
		if (DiagnosticsActive(frame)) {
			std::scoped_lock lock(diagnosticsMutex);
			queueDiagnostics = QueueDiagnostics{ .frame = frame };
		}
#endif
		return;
	}
	NormalizeSettings();
	const auto frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
	queuedCamera = Util::GetEyePosition(0);
	eastl::vector<GrassCollisionActorCandidate> candidates;
	std::unordered_set<uint32_t> actors;
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto add = [&](RE::ActorHandle handle) {
		RE::ActorPtr actor;
		if (!RE::Actor::LookupByHandle(handle.native_handle(), actor) || !actor || !actor->Is3DLoaded() ||
			(!settings.TrackRagdolls && actor->IsDead()) || !actors.insert(actor->GetFormID()).second)
			return;
		const float distance = queuedCamera.GetSquaredDistance(actor->GetPosition());
		if (std::isfinite(distance) && distance <= MAX_ACTOR_SQ_DISTANCE)
			candidates.push_back({ handle, distance });
	};
	if (const auto* lists = RE::ProcessLists::GetSingleton())
		for (auto handle : lists->highActorHandles)
			add(handle);
	if (player)
		add(player->GetHandle());
	std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) { return left.sqDistance < right.sqDistance; });
	queuedBoundingBoxes.reserve(MAX_BOUNDING_BOXES);
	queuedCollisions.reserve(MAX_COLLISIONS);
#ifdef DEVBENCH_BRIDGE_ENABLED
	QueueDiagnostics diagnostic;
	diagnostic.frame = frame;
	diagnostic.candidates = candidates.size();
	diagnostic.cameraPosition = { queuedCamera.x, queuedCamera.y, queuedCamera.z };
	const bool observe = DiagnosticsActive(frame);
#endif
	struct Shape
	{
		const RE::bhkNiCollisionObject* object;
		ColliderHistory history;
		float radius;
	};
	for (const auto& candidate : candidates) {
		RE::ActorPtr actor;
		if (!RE::Actor::LookupByHandle(candidate.handle.native_handle(), actor) || !actor)
			continue;
		auto* root = actor->Get3D(false);
		if (!root)
			continue;
#ifdef DEVBENCH_BRIDGE_ENABLED
		++diagnostic.actorRoots;
#endif
		std::array<Shape, MAX_COLLISIONS_PER_BOUNDING_BOX> shapes{};
		size_t shapeCount = 0;
		const float minimumRadius = std::sqrt(candidate.sqDistance) * MIN_COLLISION_RADIUS_DISTANCE_SCALE;
		RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* object) {
#ifdef DEVBENCH_BRIDGE_ENABLED
			++diagnostic.shapeVisits;
#endif
			Util::ShapeCollisionCapsule capsule;
			if (!Util::GetShapeCollisionCapsule(object, capsule)) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				++diagnostic.boundFailures;
#endif
				return RE::BSVisit::BSVisitControl::kContinue;
			}
			// Reject unbounded geometry before it can overflow projected sweep arithmetic.
			const auto relativeA = capsule.pointA - queuedCamera;
			const auto relativeB = capsule.pointB - queuedCamera;
			if (std::max({ std::abs(relativeA.x), std::abs(relativeA.y), std::abs(relativeA.z),
					std::abs(relativeB.x), std::abs(relativeB.y), std::abs(relativeB.z) }) > kWorldSize * 2)
				return RE::BSVisit::BSVisitControl::kContinue;
			capsule.radius *= settings.CollisionRadiusScale;
			if (!std::isfinite(capsule.radius) || capsule.radius > kWorldSize || capsule.radius < minimumRadius) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				++diagnostic.radiusRejected;
#endif
				return RE::BSVisit::BSVisitControl::kContinue;
			}
			auto* body = object->body.get()->AsBhkRigidBody();
			auto* rigid = skyrim_cast<RE::hkpRigidBody*>(body->referencedObject.get());
			Shape shape{ object, { capsule.pointA, capsule.pointB, root, body, rigid->collidable.GetShape(), actor->GetFormID(), frame }, capsule.radius };
			if (shapeCount == shapes.size() && shape.radius <= shapes.back().radius)
				return RE::BSVisit::BSVisitControl::kContinue;
			size_t index = std::min(shapeCount, shapes.size() - 1);
			shapeCount = std::min(shapeCount + 1, shapes.size());
			while (index > 0 && shapes[index - 1].radius < shape.radius) {
				shapes[index] = shapes[index - 1];
				--index;
			}
			shapes[index] = shape;
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		BoundingBoxPacked box{};
		box.IndexStart = static_cast<uint32_t>(queuedCollisions.size());
		for (size_t index = 0; index < shapeCount; ++index) {
			const auto& shape = shapes[index];
			const auto& current = shape.history;
			auto previous = current;
			if (const auto found = colliderHistory.find(shape.object); found != colliderHistory.end()) {
				const auto& old = found->second;
				// Never sweep across a replaced skeleton, a missing frame, or an actor teleport.
				if (old.actorID == current.actorID && old.root == current.root && old.body == current.body && old.shape == current.shape &&
					uint32_t(frame - old.frame) == 1 && old.pointA.GetSquaredDistance(current.pointA) <= 512.0f * 512.0f &&
					old.pointB.GetSquaredDistance(current.pointB) <= 512.0f * 512.0f)
					previous = old;
			}
			colliderHistory.insert_or_assign(shape.object, current);
			auto pack = [&](const RE::NiPoint3& point, float radius = 0.0f) {
				const auto relative = point - queuedCamera;
				return float4{ relative.x, relative.y, relative.z, radius };
			};
			CollisionShapePacked packed{ pack(current.pointA, shape.radius), pack(current.pointB), pack(previous.pointA), pack(previous.pointB) };
			queuedCollisions.push_back(packed);
			const float reach = shape.radius + settings.GrassInteractionRadius +
			                    0.5f * std::max(std::hypot(packed.CurrentPointB.x - packed.CurrentPointAAndRadius.x, packed.CurrentPointB.y - packed.CurrentPointAAndRadius.y),
										   std::hypot(packed.PreviousPointB.x - packed.PreviousPointA.x, packed.PreviousPointB.y - packed.PreviousPointA.y));
			for (const auto& point : { packed.CurrentPointAAndRadius, packed.CurrentPointB, packed.PreviousPointA, packed.PreviousPointB }) {
				box.MinExtent.x = std::min(box.MinExtent.x, point.x - reach);
				box.MinExtent.y = std::min(box.MinExtent.y, point.y - reach);
				box.MaxExtent.x = std::max(box.MaxExtent.x, point.x + reach);
				box.MaxExtent.y = std::max(box.MaxExtent.y, point.y + reach);
			}
		}
		box.IndexEnd = static_cast<uint32_t>(queuedCollisions.size());
		if (box.IndexEnd > box.IndexStart) {
			queuedBoundingBoxes.push_back(box);
#ifdef DEVBENCH_BRIDGE_ENABLED
			(actor.get() == player ? diagnostic.playerShapes : diagnostic.otherActorShapes) += box.IndexEnd - box.IndexStart;
#endif
		}
		if (queuedBoundingBoxes.size() == MAX_BOUNDING_BOXES)
			break;
	}
	std::erase_if(colliderHistory, [frame](const auto& entry) { return entry.second.frame != frame; });
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (observe) {
		try {
			diagnostic.boundingBoxes = queuedBoundingBoxes.size();
			diagnostic.shapes = queuedCollisions.size();
			for (size_t i = 0; i < std::min<size_t>(queuedCollisions.size(), 8); ++i)
				diagnostic.shapeSamples.push_back(queuedCollisions[i]);
			std::scoped_lock lock(diagnosticsMutex);
			queueDiagnostics = std::move(diagnostic);
		} catch (const std::exception& error) {
			RecordDiagnosticsFailure("actor observations", error);
		}
	}
#endif
}

void GrassCollision::ResetRuntimeStateAfterGameLoad()
{
	resetRequested.store(true);
	historyResetRequested.store(true);
}

void GrassCollision::ClearFields()
{
	if (!fieldsCleared) {
		const float zero[4]{};
		for (size_t i = 0; i < deformation.size(); ++i) {
			globals::d3d::context->ClearUnorderedAccessViewFloat(deformation[i]->uav.get(), zero);
			globals::d3d::context->ClearUnorderedAccessViewFloat(velocity[i]->uav.get(), zero);
		}
		fieldsCleared = true;
	}
	published = {};
	fieldInitialized = false;
}

bool GrassCollision::NeedsVertexDeformation() const
{
	return loaded && !resourceFailed.load(std::memory_order_relaxed) &&
	       (settings.EnableGrassCollision || vertexHistoryActive.load(std::memory_order_relaxed));
}

void GrassCollision::ReleaseResources()
{
	if (vertexResourcesBound) {
		ID3D11ShaderResourceView* empty[2]{};
		ID3D11Buffer* buffer = nullptr;
		ID3D11SamplerState* sampler = nullptr;
		auto* context = computeContext ? computeContext.get() : globals::d3d::context;
		context->VSSetShaderResources(100, 2, empty);
		context->VSSetConstantBuffers(5, 1, &buffer);
		context->VSSetSamplers(15, 1, &sampler);
		vertexResourcesBound = false;
	}
	deformation = {};
	velocity = {};
	perFrame.reset();
	updateConstants.reset();
	collisionBoundingBoxes.reset();
	collisionInstances.reset();
	fieldSampler = nullptr;
	computeContext = nullptr;
	published = {};
	fieldInitialized = fieldsCleared = false;
	vertexHistoryActive.store(false, std::memory_order_relaxed);
#ifdef DEVBENCH_BRIDGE_ENABLED
	fieldResourcesResident.store(false, std::memory_order_relaxed);
#endif
}

void GrassCollision::Update()
{
	if (!loaded)
		return;
	auto* context = globals::d3d::context;
	const auto frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
	if (resourceFailed || (!settings.EnableGrassCollision && !vertexHistoryActive.load(std::memory_order_relaxed))) {
		if (perFrame || vertexResourcesBound)
			ReleaseResources();
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (lastUpdateFrame != frame) {
			ObserveUpdateDiagnostics(frame);
		}
#endif
		lastUpdateFrame = frame;
		return;
	}
	if (!perFrame && !resourceFailed)
		SetupResources();
	if (!resourceFailed && perFrame && lastUpdateFrame != frame) {
		const bool consecutive = uint32_t(frame - lastUpdateFrame) == 1;
		lastUpdateFrame = frame;
		try {
			NormalizeSettings();
			const auto camera = Util::GetEyePosition(0);
			if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z) ||
				std::abs(camera.x) > 1e8f || std::abs(camera.y) > 1e8f)
				throw std::runtime_error("Invalid collision camera position");
			const float cellSize = kWorldSize / kFieldSize;
			const float2 cell{ std::round(camera.x / cellSize), std::round(camera.y / cellSize) };
			ID3D11ShaderResourceView* empty[2]{};
			context->VSSetShaderResources(100, 2, empty);
			const bool reset = resetRequested.exchange(false);
			if (reset) {
				queuedBoundingBoxes.clear();
				queuedCollisions.clear();
			}
			if (reset || !consecutive || (!fieldInitialized && settings.EnableGrassCollision))
				ClearFields();
			UpdateConstants constants{};
			constants.PosOffset = cell * cellSize - float2{ camera.x, camera.y };
			constants.ArrayOrigin = { uint32_t(int32_t(cell.x) - kFieldSize / 2) & (kFieldSize - 1), uint32_t(int32_t(cell.y) - kFieldSize / 2) & (kFieldSize - 1) };
			constants.ValidMargin = fieldInitialized ? DirectX::XMINT2{ int32_t(previousCell.x - cell.x), int32_t(previousCell.y - cell.y) } : DirectX::XMINT2{ kFieldSize, kFieldSize };
			constants.TimeDelta = globals::game::ui->GameIsPaused() ? 0 : Util::ClampFinite(*globals::game::deltaTime, 0.0f, 1.0f / 15.0f, 0.0f);
			constants.BoundingBoxCount = settings.EnableGrassCollision ? static_cast<uint32_t>(queuedBoundingBoxes.size()) : 0;
			constants.GrassInteractionRadius = settings.GrassInteractionRadius;
			constants.CollisionStrength = settings.CollisionImpactStrength;
			constants.SpringStrength = settings.SpringStrength;
			constants.Damping = settings.Damping;
			constants.MaximumBend = DirectX::XMConvertToRadians(settings.MaximumBend);
			constants.MaximumCompression = settings.MaximumCompression;
			constants.CompressionRecovery = settings.CompressionRecovery;
			const auto shift = queuedCamera - camera;
			for (auto& box : queuedBoundingBoxes) {
				box.MinExtent += float2{ shift.x, shift.y };
				box.MaxExtent += float2{ shift.x, shift.y };
			}
			for (auto& shape : queuedCollisions)
				for (auto* point : { &shape.CurrentPointAAndRadius, &shape.CurrentPointB, &shape.PreviousPointA, &shape.PreviousPointB }) {
					point->x += shift.x;
					point->y += shift.y;
					point->z += shift.z;
				}
#ifdef DEVBENCH_BRIDGE_ENABLED
			const auto shapes = queuedCollisions.size();
#endif
			const bool dispatched = UpdateCollisionTexture(constants);
			PerFrame next{};
			next.PosOffset = constants.PosOffset;
			next.ArrayOrigin = constants.ArrayOrigin;
			// PreviousWorld is relative to the previous camera, so retain its field mapping.
			next.PreviousPosOffset = published.PosOffset;
			next.PreviousArrayOrigin = published.ArrayOrigin;
			next.CollisionStrength = dispatched && settings.EnableGrassCollision ? GetCollisionStrength() : 0;
			next.PreviousCollisionStrength = (dispatched || !settings.EnableGrassCollision) ? published.CollisionStrength : 0;
			next.CompressionHeight = settings.CompressionHeight * 0.01f;
			next.MaximumCompressibleGrassHeight = settings.MaximumCompressibleGrassHeight;
			next.MaximumBend = constants.MaximumBend;
			next.PreviousMaximumBend = published.MaximumBend;
			next.PreviousCompressionHeight = published.CompressionHeight;
			next.PreviousMaximumCompressibleGrassHeight = published.MaximumCompressibleGrassHeight;
			perFrame->Update(next);
			published = next;
			vertexHistoryActive.store(next.CollisionStrength > 0 || next.PreviousCollisionStrength > 0, std::memory_order_relaxed);
			previousCell = cell;
			fieldInitialized = dispatched;
#ifdef DEVBENCH_BRIDGE_ENABLED
			ObserveUpdateDiagnostics(frame, &constants, shapes, dispatched);
#endif
		} catch (const std::exception& error) {
			resourceFailed = true;
			logger::error("Grass collision disabled after resource failure: {}", error.what());
		}
		queuedBoundingBoxes.clear();
		queuedCollisions.clear();
	}
	if (resourceFailed) {
		// Initialization can fail before the first bind while a collision permutation is selected.
		vertexResourcesBound = true;
		ReleaseResources();
		return;
	}
	auto* buffer = perFrame ? perFrame->CB() : nullptr;
	context->VSSetConstantBuffers(5, 1, &buffer);
	ID3D11ShaderResourceView* fields[]{ deformation[fieldIndex] ? deformation[fieldIndex]->srv.get() : nullptr, deformation[1 - fieldIndex] ? deformation[1 - fieldIndex]->srv.get() : nullptr };
	context->VSSetShaderResources(100, 2, fields);
	auto* sampler = fieldSampler.get();
	context->VSSetSamplers(15, 1, &sampler);
	vertexResourcesBound = true;
}

#ifdef DEVBENCH_BRIDGE_ENABLED
bool GrassCollision::DiagnosticsActive(uint32_t a_frame) const
{
	const auto requested = diagnosticsRequestFrame.load(std::memory_order_acquire);
	return !diagnosticsFailed.load(std::memory_order_relaxed) && requested != UINT64_MAX && uint32_t(a_frame - uint32_t(requested)) < kDiagnosticsWindowFrames;
}

void GrassCollision::RecordDiagnosticsFailure(const char* stage, const std::exception& error) noexcept
{
	diagnosticsFailed.store(true, std::memory_order_relaxed);
	std::scoped_lock lock(diagnosticsMutex);
	// Failure reporting must also work when an observation exhausts the heap.
	std::snprintf(diagnosticsError.data(), diagnosticsError.size(), "%s: %s", stage, error.what());
}

void GrassCollision::ObserveUpdateDiagnostics(uint32_t frame, const UpdateConstants* constants, size_t shapes, bool dispatched)
{
	try {
		if (DiagnosticsActive(frame)) {
			std::scoped_lock lock(diagnosticsMutex);
			if (constants)
				updateDiagnostics = UpdateDiagnostics{ frame, constants->BoundingBoxCount, shapes, collisionUpdateCS.get() != nullptr, dispatched,
					constants->TimeDelta, constants->PosOffset, constants->ArrayOrigin, constants->ValidMargin,
					published.CollisionStrength, published.PreviousCollisionStrength, settings };
			else {
				updateDiagnostics = UpdateDiagnostics{ .frame = frame, .collisionStrength = 0, .previousCollisionStrength = 0, .settings = settings };
				if (textureDiagnostics.is_null())
					textureDiagnostics = { { "status", resourceFailed ? "unavailable" : "disabled" }, { "capturedFrame", frame } };
			}
		}
		if (!diagnosticsFailed.load(std::memory_order_relaxed)) {
			if (constants)
				ObserveCollisionTexture(frame, published);
			else
				PollCollisionTexture(frame);
		}
	} catch (const std::exception& error) {
		readbackPending = false;
		RecordDiagnosticsFailure("field observations", error);
	}
	PollVertexProbe(frame);
}

void GrassCollision::RequestDiagnostics()
{
	const auto frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
	std::scoped_lock lock(diagnosticsMutex);
	queueDiagnostics.reset();
	updateDiagnostics.reset();
	drawDiagnostics = {};
	shaderDiagnostics.clear();
	textureDiagnostics = nullptr;
	diagnosticsError.fill(0);
	diagnosticsFailed.store(false, std::memory_order_relaxed);
	diagnosticsRequestFrame.store(frame, std::memory_order_release);
	const auto generation = diagnosticsGeneration.fetch_add(1, std::memory_order_release) + 1;
	vertexProbeFailed.store(false, std::memory_order_release);
	vertexProbe.Request(generation, frame);
}

json GrassCollision::GetDiagnostics() const
{
	std::scoped_lock lock(diagnosticsMutex);
	const auto requested = diagnosticsRequestFrame.load(std::memory_order_acquire);
	json queue = nullptr, update = nullptr;
	if (queueDiagnostics) {
		const auto& item = *queueDiagnostics;
		json shapes = json::array();
		for (const auto& shape : item.shapeSamples)
			shapes.push_back({ { "currentAAndRadius", { shape.CurrentPointAAndRadius.x, shape.CurrentPointAAndRadius.y, shape.CurrentPointAAndRadius.z, shape.CurrentPointAAndRadius.w } },
				{ "currentB", { shape.CurrentPointB.x, shape.CurrentPointB.y, shape.CurrentPointB.z } },
				{ "previousA", { shape.PreviousPointA.x, shape.PreviousPointA.y, shape.PreviousPointA.z } },
				{ "previousB", { shape.PreviousPointB.x, shape.PreviousPointB.y, shape.PreviousPointB.z } } });
		queue = {
			{ "frame", item.frame }, { "actorCandidates", item.candidates }, { "actorRoots", item.actorRoots },
			{ "shapeVisits", item.shapeVisits }, { "shapeBoundFailures", item.boundFailures },
			{ "radiusRejected", item.radiusRejected }, { "playerShapes", item.playerShapes },
			{ "otherActorShapes", item.otherActorShapes }, { "boundingBoxes", item.boundingBoxes },
			{ "collisionShapes", item.shapes }, { "shapeSamplesCameraRelative", std::move(shapes) },
			{ "cameraPosition", { item.cameraPosition.x, item.cameraPosition.y, item.cameraPosition.z } }
		};
	}
	if (updateDiagnostics) {
		const auto& item = *updateDiagnostics;
		update = {
			{ "frame", item.frame }, { "uploadedBoundingBoxes", item.boundingBoxes }, { "uploadedShapes", item.shapes },
			{ "shaderObjectAvailable", item.shaderObjectAvailable }, { "dispatchIssued", item.dispatchIssued },
			{ "timeDelta", item.timeDelta },
			{ "posOffset", { item.posOffset.x, item.posOffset.y } },
			{ "arrayOrigin", { item.arrayOrigin.x, item.arrayOrigin.y } },
			{ "validMargin", { item.validMargin.x, item.validMargin.y } },
			{ "settings", item.settings },
			{ "collisionStrength", item.collisionStrength }, { "previousCollisionStrength", item.previousCollisionStrength }
		};
	}
	const auto& draw = drawDiagnostics;
	const bool observed = draw.nativeChecks + draw.batchedChecks > 0;
	return {
		{ "schemaVersion", 4 }, { "requestGeneration", diagnosticsGeneration.load(std::memory_order_acquire) }, { "loaded", loaded }, { "enabled", settings.EnableGrassCollision }, { "settings", CapturePerformanceSettingsState() }, { "resourceFailed", resourceFailed.load() },
		{ "vertexDeformationActive", NeedsVertexDeformation() }, { "fieldResourceBytes", fieldResourcesResident.load() ? uint64_t(kFieldSize) * kFieldSize * 24 : 0 },
		{ "requestedFrame", requested == UINT64_MAX ? json(nullptr) : json(requested) },
		{ "captureWindowFrames", kDiagnosticsWindowFrames }, { "queue", std::move(queue) }, { "update", std::move(update) },
		{ "diagnosticsError", diagnosticsError.front() ? json(diagnosticsError.data()) : json(nullptr) },
		{ "texture", textureDiagnostics },
		{ "vertexProbe", vertexProbe.Read() }, { "vertexProbeError", vertexProbeFailed.load(std::memory_order_acquire) && diagnosticsError.front() ? json(diagnosticsError.data()) : json(nullptr) },
		{ "drawBindings", { { "observed", observed }, { "firstFrame", observed ? json(draw.firstFrame) : json(nullptr) },
							  { "lastFrame", observed ? json(draw.lastFrame) : json(nullptr) },
							  { "nativeChecks", draw.nativeChecks }, { "disabledShaderChecks", draw.disabledShaderChecks }, { "residualCollisionShaderChecks", draw.residualCollisionShaderChecks }, { "batchedChecks", draw.batchedChecks },
							  { "missingConstantBuffer", draw.missingConstantBuffer }, { "wrongConstantBuffer", draw.wrongConstantBuffer },
							  { "missingTexture", draw.missingTexture }, { "missingPreviousTexture", draw.missingPreviousTexture },
							  { "wrongPreviousTexture", draw.wrongPreviousTexture }, { "missingSampler", draw.missingSampler }, { "wrongSampler", draw.wrongSampler }, { "wrongTexture", draw.wrongTexture },
							  { "collisionShaderChecks", draw.collisionShaderChecks }, { "missingCollisionShaderInputs", draw.missingCollisionShaderInputs },
							  { "unidentifiedVertexShaders", draw.unidentifiedVertexShaders }, { "shaderReflectionFailures", draw.shaderReflectionFailures } } }
	};
}

void GrassCollision::ObserveDrawBindings(bool a_batched)
try {
	const auto frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
	if (!DiagnosticsActive(frame))
		return;
	auto* context = globals::d3d::context;
	winrt::com_ptr<ID3D11Buffer> buffer;
	winrt::com_ptr<ID3D11ShaderResourceView> texture, previousTexture;
	winrt::com_ptr<ID3D11SamplerState> sampler;
	winrt::com_ptr<ID3D11VertexShader> vertex;
	context->VSGetShader(vertex.put(), nullptr, nullptr);
	context->VSGetConstantBuffers(5, 1, buffer.put());
	context->VSGetShaderResources(100, 1, texture.put());
	context->VSGetShaderResources(101, 1, previousTexture.put());
	context->VSGetSamplers(15, 1, sampler.put());
	std::scoped_lock lock(diagnosticsMutex);
	if (!DiagnosticsActive(frame))
		return;
	auto& draw = drawDiagnostics;
	if (draw.nativeChecks + draw.batchedChecks == 0)
		draw.firstFrame = frame;
	draw.lastFrame = frame;
	if (a_batched)
		++draw.batchedChecks;
	else
		++draw.nativeChecks;
	auto* shader = GetDiagnosticShader(vertex.get());
	if (shader && shader->inputs == 0) {
		++draw.disabledShaderChecks;
		return;
	}
	if (shader && shader->inputs == 15 && !NeedsVertexDeformation())
		++draw.residualCollisionShaderChecks;
	if (!buffer)
		++draw.missingConstantBuffer;
	else if (!perFrame || buffer.get() != perFrame->CB())
		++draw.wrongConstantBuffer;
	if (!texture)
		++draw.missingTexture;
	else if (!deformation[fieldIndex] || texture.get() != deformation[fieldIndex]->srv.get())
		++draw.wrongTexture;
	if (!previousTexture)
		++draw.missingPreviousTexture;
	else if (!deformation[1 - fieldIndex] || previousTexture.get() != deformation[1 - fieldIndex]->srv.get())
		++draw.wrongPreviousTexture;
	if (!sampler)
		++draw.missingSampler;
	else if (sampler.get() != fieldSampler.get())
		++draw.wrongSampler;
	if (!shader)
		++draw.unidentifiedVertexShaders;
	else if (shader->inputs == UINT8_MAX)
		++draw.shaderReflectionFailures;
	else {
		++draw.collisionShaderChecks;
		if (shader->inputs != 15)
			++draw.missingCollisionShaderInputs;
	}
} catch (const std::exception& error) {
	RecordDiagnosticsFailure("draw bindings", error);
}

GrassCollision::DiagnosticShader* GrassCollision::GetDiagnosticShader(ID3D11VertexShader* vertex)
{
	if (!vertex)
		return nullptr;
	for (auto& shader : shaderDiagnostics)
		if (shader.vertex.get() == vertex)
			return &shader;
	if (shaderDiagnostics.size() >= 32)
		return nullptr;
	DiagnosticShader shader;
	std::vector<uint8_t> bytes;
	shader.vertex.copy_from(vertex);
	if (!globals::shaderCache->CopyDiagnosticVertexBytecode(RE::BSShader::Type::Grass, vertex, bytes, shader.descriptor))
		return nullptr;
	winrt::com_ptr<ID3D11ShaderReflection> reflection;
	D3D11_SHADER_DESC desc{};
	if (FAILED(D3DReflect(bytes.data(), bytes.size(), IID_PPV_ARGS(reflection.put()))) || FAILED(reflection->GetDesc(&desc)))
		shader.inputs = UINT8_MAX;
	else {
		for (UINT i = 0; i < desc.BoundResources; ++i) {
			D3D11_SHADER_INPUT_BIND_DESC binding{};
			if (FAILED(reflection->GetResourceBindingDesc(i, &binding))) {
				shader.inputs = UINT8_MAX;
				break;
			}
			if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 5)
				shader.inputs |= 1;
			if (binding.Type == D3D_SIT_TEXTURE && binding.BindPoint == 100)
				shader.inputs |= 2;
			if (binding.Type == D3D_SIT_TEXTURE && binding.BindPoint == 101)
				shader.inputs |= 4;
			if (binding.Type == D3D_SIT_SAMPLER && binding.BindPoint == 15)
				shader.inputs |= 8;
		}
	}
	shader.bytes = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
	shaderDiagnostics.push_back(std::move(shader));
	return &shaderDiagnostics.back();
}

void GrassCollision::ObserveVertexDraw(ID3D11DeviceContext* a_context, const GrassCollisionVertexProbe::Draw& a_draw,
	GrassCollisionVertexProbe::DirectDraw a_direct, GrassCollisionVertexProbe::IndirectDraw a_indirect)
{
	if (!loaded || vertexProbe.IsCapturing() || vertexProbeFailed.load(std::memory_order_acquire))
		return;
	const auto frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
	if (!DiagnosticsActive(frame) || !globals::state->currentShader ||
		globals::state->currentShader->shaderType != RE::BSShader::Type::Grass)
		return;
	winrt::com_ptr<ID3D11VertexShader> vertex;
	a_context->VSGetShader(vertex.put(), nullptr, nullptr);
	try {
		DiagnosticShader copy;
		{
			std::scoped_lock lock(diagnosticsMutex);
			auto* shader = GetDiagnosticShader(vertex.get());
			if (!shader || shader->inputs != 15)
				return;
			copy = *shader;
		}
		CSX::Api::SuppressAcceptedDraw suppress;
		vertexProbe.Capture(GetVertexProbeContext(), vertex.get(), copy.bytes->data(),
			copy.bytes->size(), copy.descriptor, frame, a_draw, a_direct, a_indirect);
	} catch (const std::exception& error) {
		vertexProbeFailed.store(true, std::memory_order_release);
		RecordDiagnosticsFailure("vertex capture", error);
	}
}

ID3D11DeviceContext1* GrassCollision::GetVertexProbeContext()
{
	if (!vertexProbeContext)
		DX::ThrowIfFailed(globals::d3d::context->QueryInterface(IID_PPV_ARGS(vertexProbeContext.put())));
	return vertexProbeContext.get();
}

void GrassCollision::PollVertexProbe(uint32_t a_frame)
{
	if (diagnosticsGeneration.load(std::memory_order_acquire) == 0 || vertexProbeFailed.load(std::memory_order_acquire))
		return;
	try {
		vertexProbe.Poll(GetVertexProbeContext(), a_frame);
	} catch (const std::exception& error) {
		vertexProbeFailed.store(true, std::memory_order_release);
		RecordDiagnosticsFailure("vertex readback", error);
	}
}

void GrassCollision::ObserveCollisionTexture(uint32_t a_frame, const PerFrame& a_constants)
{
	const auto requested = diagnosticsGeneration.load(std::memory_order_acquire);
	if (readbackPending && readbackRequestGeneration != requested)
		readbackPending = false;
	PollCollisionTexture(a_frame);
	if (!DiagnosticsActive(a_frame) || readbackRequestGeneration == requested)
		return;
	readbackRequestGeneration = requested;
	readbackFrame = a_frame;
	readbackOrigin = a_constants.ArrayOrigin;
	const auto* collisionTexture = deformation[fieldIndex].get();
	D3D11_TEXTURE2D_DESC desc{};
	collisionTexture->resource->GetDesc(&desc);
	HRESULT result = S_OK;
	if (desc.Width != kFieldSize || desc.Height != kFieldSize || desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) {
		result = E_INVALIDARG;
	} else if (!diagnosticReadback) {
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		result = globals::d3d::device->CreateTexture2D(&desc, nullptr, diagnosticReadback.put());
		if (SUCCEEDED(result))
			Util::SetResourceName(diagnosticReadback.get(), "GrassCollision::DiagnosticReadback");
	}
	if (SUCCEEDED(result)) {
		globals::d3d::context->CopyResource(diagnosticReadback.get(), collisionTexture->resource.get());
		readbackPending = true;
	}
	std::scoped_lock lock(diagnosticsMutex);
	if (diagnosticsGeneration.load(std::memory_order_acquire) == requested)
		textureDiagnostics = { { "status", SUCCEEDED(result) ? "pending" : "failed" },
			{ "capturedFrame", a_frame }, { "hresult", static_cast<uint32_t>(result) } };
}

void GrassCollision::PollCollisionTexture(uint32_t a_frame)
{
	if (!readbackPending)
		return;
	const auto publishFailure = [&](const char* a_status, HRESULT a_result) {
		readbackPending = false;
		std::scoped_lock lock(diagnosticsMutex);
		if (diagnosticsGeneration.load(std::memory_order_acquire) == readbackRequestGeneration)
			textureDiagnostics = { { "status", a_status }, { "capturedFrame", readbackFrame },
				{ "completedFrame", a_frame }, { "hresult", static_cast<uint32_t>(a_result) } };
	};
	if (uint32_t(a_frame - readbackFrame) > 180) {
		publishFailure("timed_out", DXGI_ERROR_WAS_STILL_DRAWING);
		return;
	}
	auto* context = globals::d3d::context;
	D3D11_MAPPED_SUBRESOURCE mapped{};
	const auto result = context->Map(diagnosticReadback.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
	if (result == DXGI_ERROR_WAS_STILL_DRAWING)
		return;
	if (FAILED(result)) {
		publishFailure("failed", result);
		return;
	}
	const SKSE::stl::scope_exit unmap([&]() noexcept { context->Unmap(diagnosticReadback.get(), 0); });
	size_t nonFinite = 0, nonzero = 0, nearNonzero = 0;
	float maxBend = 0, maxCompression = 0, nearBend = 0, nearCompression = 0;
	for (uint32_t y = 0; y < kFieldSize; ++y) {
		const auto* row = reinterpret_cast<const DirectX::PackedVector::HALF*>(
			static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch);
		for (uint32_t x = 0; x < kFieldSize; ++x) {
			const float bendX = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 4]);
			const float bendY = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 4 + 1]);
			const float compression = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 4 + 2]);
			if (!std::isfinite(bendX) || !std::isfinite(bendY) || !std::isfinite(compression)) {
				++nonFinite;
				continue;
			}
			const float bend = std::hypot(bendX, bendY);
			const bool active = bend > 1e-5f || compression > 1e-5f;
			maxBend = std::max(maxBend, bend);
			maxCompression = std::max(maxCompression, compression);
			nonzero += active;
			const auto localX = (x - readbackOrigin.x) & (kFieldSize - 1);
			const auto localY = (y - readbackOrigin.y) & (kFieldSize - 1);
			if (localX >= kFieldSize / 2 - 32 && localX < kFieldSize / 2 + 32 && localY >= kFieldSize / 2 - 32 && localY < kFieldSize / 2 + 32) {
				nearNonzero += active;
				nearBend = std::max(nearBend, bend);
				nearCompression = std::max(nearCompression, compression);
			}
		}
	}
	readbackPending = false;
	std::scoped_lock lock(diagnosticsMutex);
	if (diagnosticsGeneration.load(std::memory_order_acquire) == readbackRequestGeneration)
		textureDiagnostics = {
			{ "status", "completed" }, { "capturedFrame", readbackFrame }, { "completedFrame", a_frame },
			{ "width", kFieldSize }, { "height", kFieldSize }, { "worldSize", kWorldSize },
			{ "nonFiniteCells", nonFinite }, { "nonzeroCells", nonzero }, { "nearCameraNonzeroCells", nearNonzero },
			{ "maxBendRadians", maxBend }, { "maxCompression", maxCompression },
			{ "maxNearCameraBendRadians", nearBend }, { "maxNearCameraCompression", nearCompression }
		};
}
#endif

void GrassCollision::LoadSettings(json& o_json)
{
	settings = o_json;
	NormalizeSettings();
}

void GrassCollision::SaveSettings(json& o_json)
{
	auto saved = settings;
	const Settings defaults;
	for (const auto& parameter : parameters)
		saved.*parameter.member = Util::ClampFinite(saved.*parameter.member, parameter.minimum, parameter.maximum, defaults.*parameter.member);
	o_json = saved;
}

void GrassCollision::RestoreDefaultSettings()
{
	settings = {};
}

void GrassCollision::PostPostLoad()
{
	Hooks::Install();
}

void GrassCollision::SetupResources()
{
	ReleaseResources();
	if (!settings.EnableGrassCollision)
		return;
	try {
		perFrame = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<PerFrame>(), "GrassCollision::PerFrame");
		updateConstants = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<UpdateConstants>(), "GrassCollision::UpdateConstants");
		DX::ThrowIfFailed(globals::d3d::context->QueryInterface(IID_PPV_ARGS(computeContext.put())));
		for (size_t i = 0; i < deformation.size(); ++i) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = kFieldSize;
			desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
			desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			deformation[i] = eastl::make_unique<Texture2D>(desc, std::format("GrassCollision::Deformation{}", i).c_str());
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = desc.Format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = desc.Format;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			deformation[i]->CreateSRV(srv);
			deformation[i]->CreateUAV(uav);
			desc.Format = srv.Format = uav.Format = DXGI_FORMAT_R16G16_FLOAT;
			velocity[i] = eastl::make_unique<Texture2D>(desc, std::format("GrassCollision::Velocity{}", i).c_str());
			velocity[i]->CreateSRV(srv);
			velocity[i]->CreateUAV(uav);
		}
		D3D11_SAMPLER_DESC sampler{};
		sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampler.MaxAnisotropy = 1;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(globals::d3d::device->CreateSamplerState(&sampler, fieldSampler.put()));
		Util::SetResourceName(fieldSampler.get(), "GrassCollision::FieldSampler");
		const auto makeStructured = [](uint32_t stride, uint32_t count, const char* name) {
			D3D11_BUFFER_DESC desc{};
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = stride;
			desc.ByteWidth = stride * count;
			auto buffer = eastl::make_unique<Buffer>(desc, nullptr, name);
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_UNKNOWN;
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv.Buffer.NumElements = count;
			buffer->CreateSRV(srv);
			return buffer;
		};
		collisionBoundingBoxes = makeStructured(sizeof(BoundingBoxPacked), MAX_BOUNDING_BOXES, "GrassCollision::BoundingBoxes");
		collisionInstances = makeStructured(sizeof(CollisionShapePacked), MAX_COLLISIONS, "GrassCollision::Instances");
		fieldIndex = 0;
		fieldsCleared = false;
		resourceFailed = false;
		lastUpdateFrame = UINT32_MAX;
		ResetRuntimeStateAfterGameLoad();
		ClearFields();
		perFrame->Update(published);
#ifdef DEVBENCH_BRIDGE_ENABLED
		fieldResourcesResident.store(true, std::memory_order_relaxed);
#endif
	} catch (const std::exception& error) {
		resourceFailed = true;
		logger::error("Grass collision resources unavailable: {}", error.what());
		ReleaseResources();
	}
}

bool GrassCollision::HasShaderDefine(RE::BSShader::Type shaderType)
{
	switch (shaderType) {
	case RE::BSShader::Type::Grass:
		return true;
	default:
		return false;
	}
}

void GrassCollision::Hooks::MainUpdate_QueueCollisions::thunk()
{
	func();
	auto& collision = globals::features::grassCollision;
	try {
		collision.QueueCollisions();
	} catch (const std::exception& error) {
		collision.queuedBoundingBoxes.clear();
		collision.queuedCollisions.clear();
		collision.colliderHistory.clear();
		collision.resourceFailed = true;
		logger::error("Grass collision disabled after actor collection failure: {}", error.what());
	}
}

void GrassCollision::Hooks::BSGrassShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::features::grassCollision.Update();
	func(This, Pass, RenderFlags);
}

void GrassCollision::ClearShaderCache()
{
	collisionUpdateCS.Reset();
	resourceFailed.store(false, std::memory_order_relaxed);
}

ID3D11ComputeShader* GrassCollision::GetCollisionUpdateCS()
{
	return collisionUpdateCS.Get(
		L"Data\\Shaders\\GrassCollision\\CollisionUpdateCS.hlsl",
		{},
		"cs_5_0",
		"main",
		"GrassCollision::CollisionUpdateCS");
}

bool GrassCollision::UpdateCollisionTexture(const UpdateConstants& constants)
{
	if (!settings.EnableGrassCollision) {
		if (!fieldInitialized)
			return false;
		const auto next = 1 - fieldIndex;
		const float zero[4]{};
		globals::d3d::context->ClearUnorderedAccessViewFloat(deformation[next]->uav.get(), zero);
		globals::d3d::context->ClearUnorderedAccessViewFloat(velocity[next]->uav.get(), zero);
		fieldIndex = next;
		return false;
	}
	auto* shader = GetCollisionUpdateCS();
	if (!shader) {
		resourceFailed.store(true, std::memory_order_relaxed);
		logger::error("Grass collision disabled because its update shader is unavailable; clear shaders to retry");
		return false;
	}
	auto* context = computeContext.get();
	D3DState::ComputeState restore(context, 2, 4);
	const auto upload = [&](Buffer& buffer, const auto& values) {
		if (values.empty())
			return;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		DX::ThrowIfFailed(context->Map(buffer.resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		memcpy(mapped.pData, values.data(), values.size() * sizeof(values[0]));
		context->Unmap(buffer.resource.get(), 0);
	};
	upload(*collisionBoundingBoxes, queuedBoundingBoxes);
	upload(*collisionInstances, queuedCollisions);
	updateConstants->Update(constants);
	auto* cb = updateConstants->CB();
	context->CSSetConstantBuffers(0, 1, &cb);
	ID3D11ShaderResourceView* fields[]{ collisionBoundingBoxes->srv.get(), collisionInstances->srv.get(), deformation[fieldIndex]->srv.get(), velocity[fieldIndex]->srv.get() };
	context->CSSetShaderResources(0, 4, fields);
	const auto next = 1 - fieldIndex;
	ID3D11UnorderedAccessView* outputs[]{ deformation[next]->uav.get(), velocity[next]->uav.get() };
	context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
	context->CSSetShader(shader, nullptr, 0);
	CS_GPU_PASS("GrassCollision::CollisionUpdate");
	context->Dispatch(kFieldSize / 8, kFieldSize / 8, 1);
	fieldsCleared = false;
	fieldIndex = next;
	return true;
}
