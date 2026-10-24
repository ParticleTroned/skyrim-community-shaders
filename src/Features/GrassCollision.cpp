#include "GrassCollision.h"
#include "Diagnostics/ISLDiagnostics.h"
#include "Menu/SettingsPage.h"
#include "Utils/UI.h"

#include "GpuPass.h"
#include "State.h"
#include "Utils/ActorUtils.h"
#include "Utils/D3D.h"
#include "Utils/Finite.h"

#include <cmath>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "Api/AcceptedDrawService.h"
#	include <DirectXPackedVector.h>
#	include <d3dcompiler.h>
#	include <d3d11shader.h>
#	include <limits>
#endif

static constexpr uint MAX_BOUNDING_BOXES = 64;
static constexpr uint MAX_COLLISIONS_PER_BOUNDING_BOX = 64;
static constexpr uint MAX_COLLISIONS = MAX_BOUNDING_BOXES * MAX_COLLISIONS_PER_BOUNDING_BOX;
static constexpr float MAX_ACTOR_DISTANCE = 2048.0f;
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
	CollisionStrength)

void GrassCollision::DrawSettings()
{
	MenuUI::SettingsPage page(GetShortName().c_str(), { { "bending", "Bending", "Adjust how strongly nearby actors bend grass.", "Grass collision strength", true, true, "Adjust grass bending" } });
	if (page.Is("bending")) {
		auto guard = Util::DisableGuard(!settings.EnableGrassCollision);
		DrawStrengthControl();
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
	Util::AddTooltip("Scale grass bending around the player and other actors. 1x keeps the original strength, 0 removes bending, and higher values make it more visible. Try 2x or 3x near your feet. This changes strength, not the collision area.");
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

json GrassCollision::CapturePerformanceSettingsState() const
{
	return {
		{ "EnableGrassCollision", settings.EnableGrassCollision },
		{ "CollisionStrength", GetCollisionStrength() }
	};
}

void GrassCollision::QueueCollisions()
{
	queuedBoundingBoxes.clear();
	queuedCollisions.clear();

	if (!settings.EnableGrassCollision) {
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
		if (DiagnosticsActive(frame)) {
			std::scoped_lock lock(diagnosticsMutex);
			queueDiagnostics = QueueDiagnostics{ .frame = frame };
		}
#endif
		return;
	}

	eastl::vector<GrassCollisionActorCandidate> actorCandidates{};
	RE::NiPoint3 cameraPosition = Util::GetEyePosition(0);
#ifdef DEVBENCH_BRIDGE_ENABLED
	QueueDiagnostics diagnostic;
	diagnostic.frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
	const bool observe = DiagnosticsActive(diagnostic.frame);
	diagnostic.cameraPosition = { cameraPosition.x, cameraPosition.y, cameraPosition.z };
#endif

	auto addActorCandidate = [&](RE::ActorHandle a_handle) {
		RE::ActorPtr actorPtr;
		if (!RE::Actor::LookupByHandle(a_handle.native_handle(), actorPtr)) {
			return;
		}

		if (actorPtr && actorPtr->Is3DLoaded()) {
			float sqDistance = cameraPosition.GetSquaredDistance(actorPtr->GetPosition());
			if (sqDistance <= MAX_ACTOR_SQ_DISTANCE)
				actorCandidates.push_back({ a_handle, sqDistance });
		}
	};

	// Actor query code from po3 under MIT
	// https://github.com/powerof3/PapyrusExtenderSSE/blob/7a73b47bc87331bec4e16f5f42f2dbc98b66c3a7/include/Papyrus/Functions/Faction.h#L24C7-L46
	if (const auto processLists = RE::ProcessLists::GetSingleton(); processLists) {
		// High actors are in combat or doing something interesting.
		for (auto& actorHandle : processLists->highActorHandles) {
			addActorCandidate(actorHandle);
		}
	}

	if (auto player = RE::PlayerCharacter::GetSingleton()) {
		addActorCandidate(player->GetHandle());
	}

	// Sort stable handle candidates by cached distance, not ActorPtr smart pointers.
	std::sort(actorCandidates.begin(), actorCandidates.end(), [](const GrassCollisionActorCandidate& a, const GrassCollisionActorCandidate& b) {
		return a.sqDistance < b.sqDistance;
	});

	eastl::vector<BoundingBoxPacked> boundingBoxData{};
	boundingBoxData.reserve(MAX_BOUNDING_BOXES);

	eastl::vector<float4> collisionsData{};
	collisionsData.reserve(MAX_COLLISIONS);

	uint collisionIndexExtent = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
	diagnostic.candidates = actorCandidates.size();
#endif

	for (const auto& actorCandidate : actorCandidates) {
		RE::ActorPtr actor;
		if (!RE::Actor::LookupByHandle(actorCandidate.handle.native_handle(), actor)) {
			continue;
		}

		if (actor && actor->Is3DLoaded()) {
			auto root = actor->Get3D(false);
			if (!root)
				continue;
#ifdef DEVBENCH_BRIDGE_ENABLED
			if (observe)
				++diagnostic.actorRoots;
#endif

			float distance = std::sqrt(actorCandidate.sqDistance);

			eastl::vector<float4> collisionShapes{};

			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_object) -> RE::BSVisit::BSVisitControl {
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (observe)
					++diagnostic.shapeVisits;
#endif
				RE::NiPoint3 centerPos;
				float radius;
				if (Util::GetShapeBound(a_object, centerPos, radius)) {
					// Cull extremely small collisions
					if (radius < distance * MIN_COLLISION_RADIUS_DISTANCE_SCALE) {
#ifdef DEVBENCH_BRIDGE_ENABLED
						if (observe)
							++diagnostic.radiusRejected;
#endif
						return RE::BSVisit::BSVisitControl::kContinue;
					}

					centerPos -= cameraPosition;

					float4 data{};
					data.x = centerPos.x;
					data.y = centerPos.y;
					data.z = centerPos.z;
					data.w = radius;

					collisionShapes.push_back(data);
#ifdef DEVBENCH_BRIDGE_ENABLED
				} else if (observe) {
					++diagnostic.boundFailures;
#endif
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});

			std::sort(collisionShapes.begin(), collisionShapes.end(), [](const float4& a, const float4& b) {
				return a.w > b.w;
			});

			BoundingBoxPacked boundingBox;

			boundingBox.IndexStart = collisionIndexExtent;
			boundingBox.IndexEnd = collisionIndexExtent;

			uint boundingBoxCollisions = 0;

			for (const auto& data : collisionShapes) {
				collisionsData.push_back(data);

				float2 pointMin(data.x - data.w, data.y - data.w);
				float2 pointMax(data.x + data.w, data.y + data.w);

				boundingBox.MinExtent.x = std::min(boundingBox.MinExtent.x, pointMin.x);
				boundingBox.MinExtent.y = std::min(boundingBox.MinExtent.y, pointMin.y);

				boundingBox.MaxExtent.x = std::max(boundingBox.MaxExtent.x, pointMax.x);
				boundingBox.MaxExtent.y = std::max(boundingBox.MaxExtent.y, pointMax.y);

				boundingBox.IndexEnd++;

				boundingBoxCollisions++;

				if (boundingBoxCollisions == MAX_COLLISIONS_PER_BOUNDING_BOX)
					break;
			}

			if (boundingBox.IndexStart != boundingBox.IndexEnd) {
#ifdef DEVBENCH_BRIDGE_ENABLED
				if (observe) {
					if (actor.get() == RE::PlayerCharacter::GetSingleton())
						diagnostic.playerShapes += boundingBoxCollisions;
					else
						diagnostic.otherActorShapes += boundingBoxCollisions;
				}
#endif
				boundingBoxData.push_back(boundingBox);
				collisionIndexExtent = boundingBox.IndexEnd;
				if (boundingBoxData.size() == MAX_BOUNDING_BOXES)
					break;
			}
		}
	}

	queuedBoundingBoxes = std::move(boundingBoxData);
	queuedCollisions = std::move(collisionsData);
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (observe) {
		diagnostic.boundingBoxes = queuedBoundingBoxes.size();
		diagnostic.shapes = queuedCollisions.size();
		diagnostic.shapeSamples.assign(queuedCollisions.begin(), queuedCollisions.begin() + std::min<size_t>(queuedCollisions.size(), 8));
		std::scoped_lock lock(diagnosticsMutex);
		queueDiagnostics = diagnostic;
	}
#endif
}

void GrassCollision::Update()
{
	static Util::FrameChecker frameChecker;
	if (frameChecker.IsNewFrame()) {
		PerFrame perFrameData{};

		perFrameData.BoundingBoxCount = 0;

		static float2 prevCellID = { 0, 0 };

		auto eyePosNI = Util::GetEyePosition(0);
		static auto prevEyePosNI = eyePosNI;

		auto eyePos = float2{ eyePosNI.x, eyePosNI.y };

		float worldSize = 4096.0f;
		uint textureArrayDims = 512;

		float cellSize = worldSize / textureArrayDims;

		auto cellID = eyePos / cellSize;
		cellID = { round(cellID.x), round(cellID.y) };
		auto cellOrigin = cellID * cellSize;

		float2 cellIDDiff = prevCellID - cellID;
		prevCellID = cellID;

		perFrameData.PosOffset = cellOrigin - eyePos;

		perFrameData.ArrayOrigin = {
			((int)cellID.x - textureArrayDims / 2) % textureArrayDims,
			((int)cellID.y - textureArrayDims / 2) % textureArrayDims
		};

		perFrameData.ValidMargin = { (int)cellIDDiff.x, (int)cellIDDiff.y };

		perFrameData.TimeDelta = *globals::game::deltaTime * !globals::game::ui->GameIsPaused();

		perFrameData.CameraHeightDelta = prevEyePosNI.z - eyePosNI.z;
		perFrameData.CollisionStrength = GetCollisionStrength();
		perFrameData.PreviousCollisionStrength = previousCollisionStrength;
		previousCollisionStrength = perFrameData.CollisionStrength;

		if (!settings.EnableGrassCollision) {
			queuedBoundingBoxes.clear();
			queuedCollisions.clear();
		}

		perFrameData.BoundingBoxCount = std::min((uint)queuedBoundingBoxes.size(), MAX_BOUNDING_BOXES);

		auto context = globals::d3d::context;

		const size_t collisionCount = std::min(queuedCollisions.size(), static_cast<size_t>(MAX_COLLISIONS));
		if (collisionCount > 0) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			DX::ThrowIfFailed(context->Map(collisionInstances->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			size_t bytes = sizeof(float4) * collisionCount;
			memcpy_s(mapped.pData, bytes, queuedCollisions.data(), bytes);
			context->Unmap(collisionInstances->resource.get(), 0);
		}

		if (perFrameData.BoundingBoxCount > 0) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			DX::ThrowIfFailed(context->Map(collisionBoundingBoxes->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			size_t bytes = sizeof(BoundingBoxPacked) * perFrameData.BoundingBoxCount;
			memcpy_s(mapped.pData, bytes, queuedBoundingBoxes.data(), bytes);
			context->Unmap(collisionBoundingBoxes->resource.get(), 0);
		}

		queuedBoundingBoxes.clear();
		queuedCollisions.clear();

		perFrame->Update(perFrameData);

		UpdateCollisionTexture();
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
		if (DiagnosticsActive(frame)) {
			std::scoped_lock lock(diagnosticsMutex);
			updateDiagnostics = UpdateDiagnostics{ frame, perFrameData.BoundingBoxCount, collisionCount,
				collisionUpdateCS.get() != nullptr, settings.EnableGrassCollision && collisionUpdateCS.get() != nullptr,
				perFrameData.TimeDelta, perFrameData.PosOffset, perFrameData.ArrayOrigin, perFrameData.ValidMargin, perFrameData.CameraHeightDelta,
				perFrameData.CollisionStrength, perFrameData.PreviousCollisionStrength };
		}
		ObserveCollisionTexture(frame, perFrameData);
		PollVertexProbe(frame);
#endif

		prevCellID = cellID;
		prevEyePosNI = eyePosNI;

		ID3D11Buffer* buffers[1];
		buffers[0] = perFrame->CB();
		context->VSSetConstantBuffers(5, ARRAYSIZE(buffers), buffers);

		ID3D11ShaderResourceView* srvs[] = { collisionTexture->srv.get() };
		context->VSSetShaderResources(100, ARRAYSIZE(srvs), srvs);
	}
}

#ifdef DEVBENCH_BRIDGE_ENABLED
bool GrassCollision::DiagnosticsActive(uint32_t a_frame) const
{
	const auto requested = diagnosticsRequestFrame.load(std::memory_order_acquire);
	return requested != UINT64_MAX && uint32_t(a_frame - uint32_t(requested)) < kDiagnosticsWindowFrames;
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
	diagnosticsRequestFrame.store(frame, std::memory_order_release);
	const auto generation = diagnosticsGeneration.fetch_add(1, std::memory_order_release) + 1;
	vertexProbeError.clear();
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
			shapes.push_back({ shape.x, shape.y, shape.z, shape.w });
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
			{ "cameraHeightDelta", item.cameraHeightDelta },
			{ "collisionStrength", item.collisionStrength }, { "previousCollisionStrength", item.previousCollisionStrength }
		};
	}
	const auto& draw = drawDiagnostics;
	const bool observed = draw.nativeChecks + draw.batchedChecks > 0;
	return {
		{ "schemaVersion", 3 }, { "requestGeneration", diagnosticsGeneration.load(std::memory_order_acquire) }, { "loaded", loaded }, { "enabled", settings.EnableGrassCollision },
		{ "requestedFrame", requested == UINT64_MAX ? json(nullptr) : json(requested) },
		{ "captureWindowFrames", kDiagnosticsWindowFrames }, { "queue", std::move(queue) }, { "update", std::move(update) },
		{ "texture", textureDiagnostics },
		{ "vertexProbe", vertexProbe.Read() }, { "vertexProbeError", vertexProbeError.empty() ? json(nullptr) : json(vertexProbeError) },
		{ "drawBindings", { { "observed", observed }, { "firstFrame", observed ? json(draw.firstFrame) : json(nullptr) },
							  { "lastFrame", observed ? json(draw.lastFrame) : json(nullptr) },
							  { "nativeChecks", draw.nativeChecks }, { "batchedChecks", draw.batchedChecks },
							  { "missingConstantBuffer", draw.missingConstantBuffer }, { "wrongConstantBuffer", draw.wrongConstantBuffer },
							  { "missingTexture", draw.missingTexture }, { "wrongTexture", draw.wrongTexture },
							  { "collisionShaderChecks", draw.collisionShaderChecks }, { "missingCollisionShaderInputs", draw.missingCollisionShaderInputs },
							  { "unidentifiedVertexShaders", draw.unidentifiedVertexShaders }, { "shaderReflectionFailures", draw.shaderReflectionFailures } } }
	};
}

void GrassCollision::ObserveDrawBindings(bool a_batched)
{
	const auto frame = globals::state->frameCountAtomic.load(std::memory_order_relaxed);
	if (!DiagnosticsActive(frame))
		return;
	auto* context = globals::d3d::context;
	winrt::com_ptr<ID3D11Buffer> buffer;
	winrt::com_ptr<ID3D11ShaderResourceView> texture;
	winrt::com_ptr<ID3D11VertexShader> vertex;
	context->VSGetShader(vertex.put(), nullptr, nullptr);
	context->VSGetConstantBuffers(5, 1, buffer.put());
	context->VSGetShaderResources(100, 1, texture.put());
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
	if (!buffer)
		++draw.missingConstantBuffer;
	else if (!perFrame || buffer.get() != perFrame->CB())
		++draw.wrongConstantBuffer;
	if (!texture)
		++draw.missingTexture;
	else if (!collisionTexture || texture.get() != collisionTexture->srv.get())
		++draw.wrongTexture;
	auto* engineVertex = globals::game::currentVertexShader ? *globals::game::currentVertexShader : nullptr;
	if (!vertex || !engineVertex || vertex.get() != reinterpret_cast<ID3D11VertexShader*>(engineVertex->shader)) {
		++draw.unidentifiedVertexShaders;
		return;
	}
	auto found = std::find_if(shaderDiagnostics.begin(), shaderDiagnostics.end(),
		[&](const auto& item) { return item.first.get() == vertex.get(); });
	if (found == shaderDiagnostics.end()) {
		uint8_t inputs = 0;
		winrt::com_ptr<ID3D11ShaderReflection> reflection;
		const auto* bytes = reinterpret_cast<const uint8_t*>(engineVertex) + sizeof(*engineVertex);
		D3D11_SHADER_DESC desc{};
		if (engineVertex->byteCodeSize == 0 || engineVertex->byteCodeSize > 16 * 1024 * 1024 ||
			FAILED(D3DReflect(bytes, engineVertex->byteCodeSize, IID_PPV_ARGS(reflection.put()))) ||
			FAILED(reflection->GetDesc(&desc))) {
			inputs = UINT8_MAX;
		} else {
			for (UINT i = 0; i < desc.BoundResources; ++i) {
				D3D11_SHADER_INPUT_BIND_DESC binding{};
				if (FAILED(reflection->GetResourceBindingDesc(i, &binding))) {
					inputs = UINT8_MAX;
					break;
				}
				if (binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 5)
					inputs |= 1;
				if (binding.Type == D3D_SIT_TEXTURE && binding.BindPoint == 100)
					inputs |= 2;
			}
		}
		shaderDiagnostics.emplace_back(vertex, inputs);
		found = std::prev(shaderDiagnostics.end());
	}
	if (found->second == UINT8_MAX)
		++draw.shaderReflectionFailures;
	else {
		++draw.collisionShaderChecks;
		if (found->second != 3)
			++draw.missingCollisionShaderInputs;
	}
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
	auto* engine = globals::game::currentVertexShader ? *globals::game::currentVertexShader : nullptr;
	if (!vertex || !engine || vertex.get() != reinterpret_cast<ID3D11VertexShader*>(engine->shader))
		return;
	try {
		CSX::Api::SuppressAcceptedDraw suppress;
		vertexProbe.Capture(GetVertexProbeContext(), vertex.get(), reinterpret_cast<const uint8_t*>(engine) + sizeof(*engine),
			engine->byteCodeSize, engine->id, frame, a_draw, a_direct, a_indirect);
	} catch (const std::exception& error) {
		std::scoped_lock lock(diagnosticsMutex);
		vertexProbeError = error.what();
		vertexProbeFailed.store(true, std::memory_order_release);
		logger::error("Grass collision vertex probe failed: {}", vertexProbeError);
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
		std::scoped_lock lock(diagnosticsMutex);
		vertexProbeError = error.what();
		vertexProbeFailed.store(true, std::memory_order_release);
		logger::error("Grass collision vertex readback failed: {}", vertexProbeError);
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
	D3D11_TEXTURE2D_DESC desc{};
	collisionTexture->resource->GetDesc(&desc);
	HRESULT result = S_OK;
	if (desc.Width != 512 || desc.Height != 512 || desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) {
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
	float minimum = std::numeric_limits<float>::infinity(), maximum = -minimum, nearMaximum = 0;
	for (uint32_t y = 0; y < 512; ++y) {
		const auto* row = reinterpret_cast<const DirectX::PackedVector::HALF*>(
			static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch);
		for (uint32_t x = 0; x < 512; ++x) {
			const float value = DirectX::PackedVector::XMConvertHalfToFloat(row[x * 4]);
			if (!std::isfinite(value)) {
				++nonFinite;
				continue;
			}
			minimum = std::min(minimum, value);
			maximum = std::max(maximum, value);
			nonzero += value > 0.00001f;
			const auto localX = (x - readbackOrigin.x) & 511u;
			const auto localY = (y - readbackOrigin.y) & 511u;
			if (localX >= 224 && localX < 288 && localY >= 224 && localY < 288) {
				nearNonzero += value > 0.00001f;
				nearMaximum = std::max(nearMaximum, value);
			}
		}
	}
	readbackPending = false;
	std::scoped_lock lock(diagnosticsMutex);
	if (diagnosticsGeneration.load(std::memory_order_acquire) == readbackRequestGeneration)
		textureDiagnostics = {
			{ "status", "completed" }, { "capturedFrame", readbackFrame }, { "completedFrame", a_frame },
			{ "width", 512 }, { "height", 512 }, { "nonFiniteCells", nonFinite }, { "nonzeroCells", nonzero },
			{ "nearCameraNonzeroCells", nearNonzero }, { "maxNormalizedNearCamera", nearMaximum },
			{ "minNormalizedHeight", std::isfinite(minimum) ? json(minimum) : json(nullptr) },
			{ "maxNormalizedHeight", std::isfinite(maximum) ? json(maximum) : json(nullptr) }
		};
}
#endif

void GrassCollision::LoadSettings(json& o_json)
{
	settings = o_json;
	settings.CollisionStrength = GetCollisionStrength();
}

void GrassCollision::SaveSettings(json& o_json)
{
	auto saved = settings;
	saved.CollisionStrength = GetCollisionStrength();
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
	perFrame = new ConstantBuffer(ConstantBufferDesc<PerFrame>());

	{
		D3D11_TEXTURE2D_DESC texDesc = {
			.Width = 512,
			.Height = 512,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { .Count = 1 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		};

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		collisionTexture = new Texture2D(texDesc);
		collisionTexture->CreateSRV(srvDesc);
		collisionTexture->CreateUAV(uavDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(BoundingBoxPacked);
		sbDesc.ByteWidth = sizeof(BoundingBoxPacked) * MAX_BOUNDING_BOXES;
		collisionBoundingBoxes = eastl::make_unique<Buffer>(sbDesc, nullptr, "GrassCollision::BoundingBoxes");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_BOUNDING_BOXES;
		collisionBoundingBoxes->CreateSRV(srvDesc);
	}

	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(float4);
		sbDesc.ByteWidth = sizeof(float4) * MAX_COLLISIONS;
		collisionInstances = eastl::make_unique<Buffer>(sbDesc, nullptr, "GrassCollision::Instances");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_COLLISIONS;
		collisionInstances->CreateSRV(srvDesc);
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
	CS_ISL_BROAD(native, QueueCollisionsNative);
	func();
	CS_ISL_END(native);
	CS_ISL_BROAD(csx, QueueCollisionsCSX);
	globals::features::grassCollision.QueueCollisions();
}

void GrassCollision::Hooks::BSGrassShader_SetupGeometry::thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
{
	globals::features::grassCollision.Update();
	func(This, Pass, RenderFlags);
}

void GrassCollision::ClearShaderCache()
{
	collisionUpdateCS.Reset();
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

void GrassCollision::UpdateCollisionTexture()
{
	auto context = globals::d3d::context;

	if (!settings.EnableGrassCollision) {
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearUnorderedAccessViewFloat(collisionTexture->uav.get(), clearColor);
		return;
	}

	{
		ID3D11Buffer* buffers[1] = { *globals::game::perFrame };
		ID3D11Buffer* vrBuffer = nullptr;

		if (REL::Module::IsVR()) {
			static REL::Relocation<ID3D11Buffer**> VRValues{ REL::Offset(0x3180688) };
			vrBuffer = *VRValues.get();
		}
		if (vrBuffer) {
			context->CSSetConstantBuffers(12, 1, buffers);
			context->CSSetConstantBuffers(13, 1, &vrBuffer);
		} else {
			context->CSSetConstantBuffers(12, 1, buffers);
		}
	}

	{
		ID3D11Buffer* buffers[1] = { perFrame->CB() };
		context->CSSetConstantBuffers(0, 1, buffers);

		ID3D11ShaderResourceView* srvs[] = {
			collisionBoundingBoxes->srv.get(),
			collisionInstances->srv.get(),
		};

		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { collisionTexture->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		if (auto* shader = GetCollisionUpdateCS()) {
			context->CSSetShader(shader, nullptr, 0);
			CS_GPU_PASS("GrassCollision::CollisionUpdate");
			context->Dispatch(512 / 8, 512 / 8, 1);
		} else {
			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			context->ClearUnorderedAccessViewFloat(uavs[0], clearColor);
		}
	}

	context->CSSetShader(nullptr, nullptr, 0);

	ID3D11Buffer* null_buffer = nullptr;
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	ID3D11UnorderedAccessView* null_uavs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);
}
