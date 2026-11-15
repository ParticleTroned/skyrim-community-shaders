#include "ShaderCache.h"

#include "Globals.h"
#include "Utils/FileSystem.h"

#include <fstream>

namespace
{
	struct DiscoveryHistory
	{
		std::once_flag initialized;
		std::mutex mutex;
		Util::ShaderSpecializationRequests requests;
		std::vector<Util::ShaderSpecializationRequest> startup;
		std::filesystem::path path;
		std::string runtime;
		uint64_t revision = 0, savedRevision = 0;
		bool limitReported = false;
		std::atomic_size_t count{ 0 };
		std::atomic_bool persistenceFailed{ false };

		void Initialize()
		{
			std::call_once(initialized, [&] {
				try {
					runtime = globals::game::isVR ? "VR" : "SE";
					path = Util::PathHelpers::GetCommunityShaderPath() / ("ShaderSpecializations-" + runtime + ".json");
					std::error_code error;
					if (!std::filesystem::exists(path, error)) {
						if (error)
							throw std::system_error(error);
						return;
					}
					std::ifstream stream(path, std::ios::binary);
					if (!stream)
						throw std::runtime_error("cannot open specialization history");
					std::string text(Util::ShaderSpecializationRequests::kMaximumFileBytes + 1, '\0');
					stream.read(text.data(), static_cast<std::streamsize>(text.size()));
					if (stream.bad())
						throw std::runtime_error("cannot read specialization history");
					text.resize(static_cast<size_t>(stream.gcount()));
					requests.Load(text, runtime);
					startup = requests.Snapshot();
					count.store(requests.Size(), std::memory_order_relaxed);
					logger::info("[ShaderSpecialization] discovered_startup={} runtime={}", startup.size(), runtime);
				} catch (const std::exception& error) {
					try {
						logger::warn("[ShaderSpecialization] Ignoring unreadable discovery history: {}", error.what());
					} catch (...) {
						OutputDebugStringA("[ShaderSpecialization] Ignoring unreadable discovery history.\n");
					}
				}
			});
		}

		void Flush() noexcept
		{
			try {
				std::string text;
				uint64_t capturedRevision;
				{
					std::lock_guard lock(mutex);
					if (revision == savedRevision)
						return;
					capturedRevision = revision;
					text = requests.Serialize(runtime);
				}
				std::string error;
				if (!Util::FileHelpers::WriteTextFileAtomic(path, text, error, false))
					throw std::runtime_error(error);
				std::lock_guard lock(mutex);
				savedRevision = capturedRevision;
				persistenceFailed.store(false, std::memory_order_relaxed);
			} catch (const std::exception& error) {
				persistenceFailed.store(true, std::memory_order_relaxed);
				try {
					logger::warn("[ShaderSpecialization] Could not save discoveries for next startup: {}", error.what());
				} catch (...) {
					OutputDebugStringA("[ShaderSpecialization] Discovery persistence failed; retaining general shading.\n");
				}
			} catch (...) {
				persistenceFailed.store(true, std::memory_order_relaxed);
				OutputDebugStringA("[ShaderSpecialization] Discovery persistence failed; retaining general shading.\n");
			}
		}
	};

	DiscoveryHistory history;
}

namespace SIE
{
	bool ShaderCache::CanCompileOptionalShaderSources() const
	{
		return ShaderCompilationSchedulingPolicy::CanCompileOptionalSources(
			startupCompilationComplete.load(std::memory_order_acquire), menuLoaded.load(std::memory_order_acquire),
			backgroundCompilation.load(std::memory_order_acquire));
	}

	void ShaderCache::RegisterSpecializationBase(RE::BSShader::Type type, uint32_t descriptor)
	{
		const auto mask = GetPixelSpecializationMask(type);
		if (!mask || !CanCompileOptionalShaderSources())
			return;
		std::lock_guard lock(specializationBasesMutex);
		specializationBases[type == RE::BSShader::Type::Lighting ? 0 : 1].insert(descriptor & ~mask);
	}

	std::vector<Util::ShaderSpecializationRequest> ShaderCache::GetStartupShaderSpecializations()
	{
		history.Initialize();
		return history.startup;
	}

	void ShaderCache::PrewarmDiscoveredPixelShaders(const RE::BSShader& shader)
	{
		const auto type = shader.shaderType.get();
		const auto mask = GetPixelSpecializationMask(type);
		if (!mask)
			return;
		if (!CanCompileOptionalShaderSources() || !IsDiskCacheActive())
			return;
		const auto requests = GetStartupShaderSpecializations();
		const bool lighting = type == RE::BSShader::Type::Lighting;
		const auto family = lighting ? Util::SpecializationFamily::Lighting : Util::SpecializationFamily::Effect;
		std::unordered_set<uint32_t> bases;
		{
			std::lock_guard lock(specializationBasesMutex);
			bases.swap(specializationBases[lighting ? 0 : 1]);
		}
		size_t queued = 0;
		for (const auto& request : requests) {
			if (request.family == family && (request.descriptor & mask) && bases.contains(request.descriptor & ~mask)) {
				compilationSet.Add({ ShaderClass::Pixel, shader, request.descriptor, true });
				++queued;
			}
		}
		logger::info("[ShaderSpecialization] startup_family={} discovered={} admitted={}", lighting ? "Lighting" : "Effect", requests.size(), queued);
	}

	void ShaderCache::RememberShaderSpecialization(Util::ShaderSpecializationRequest request) noexcept
	try {
		history.Initialize();
		{
			std::lock_guard lock(history.mutex);
			if (history.requests.Add(request)) {
				++history.revision;
				history.count.store(history.requests.Size(), std::memory_order_relaxed);
			} else if (history.requests.Size() >= Util::ShaderSpecializationRequests::kMaximumRequests && !history.limitReported) {
				history.limitReported = true;
				logger::warn("[ShaderSpecialization] Discovery history reached its {}-request limit", Util::ShaderSpecializationRequests::kMaximumRequests);
			}
			if (history.savedRevision == history.revision)
				return;
		}
		QueueSpecializationHistoryFlush();
	} catch (...) {
		history.persistenceFailed.store(true, std::memory_order_relaxed);
		OutputDebugStringA("[ShaderSpecialization] Could not record discovery; retaining general shading.\n");
	}

	void ShaderCache::QueueSpecializationHistoryFlush()
	{
		{
			std::lock_guard lock(deferredDiskWritesMutex);
			deferredSpecializationFlushPending = true;
		}
		deferredDiskWritesCV.notify_one();
	}

	void ShaderCache::FlushSpecializationHistory()
	{
		history.Flush();
	}

	nlohmann::json ShaderCache::GetSpecializationPreparationStatus() const
	{
		return { { "sourceCompilationAllowed", CanCompileOptionalShaderSources() },
			{ "gameplayPolicy", ResidentOnlyLightingSpecializations() ? "resident_only" : "cache_only" },
			{ "discoveredRequests", history.count.load(std::memory_order_relaxed) },
			{ "discoveryPersistenceFailed", history.persistenceFailed.load(std::memory_order_relaxed) },
			{ "pendingComposites", pendingCompositePreparations.load(std::memory_order_acquire) } };
	}

	void ShaderCache::EndCompositePreparation()
	{
		pendingCompositePreparations.fetch_sub(1, std::memory_order_acq_rel);
		TryCompleteStartupCompilationPhase();
	}
}
