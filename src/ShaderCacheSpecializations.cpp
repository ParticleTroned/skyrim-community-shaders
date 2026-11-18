#include "ShaderCache.h"

#include "Deferred.h"
#include "Globals.h"
#include "State.h"
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
		std::atomic_bool limitWarningPending{ false };

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
				if (limitWarningPending.exchange(false, std::memory_order_acq_rel)) {
					try {
						logger::warn("[ShaderSpecialization] Discovery history reached its {}-request limit", Util::ShaderSpecializationRequests::kMaximumRequests);
					} catch (...) {
						OutputDebugStringA("Shader discovery history reached its request limit.\n");
					}
				}
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

	class PreparationEvents final :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
		public RE::BSTEventSink<RE::TESSleepStartEvent>,
		public RE::BSTEventSink<RE::TESSleepStopEvent>,
		public RE::BSTEventSink<RE::TESWaitStartEvent>,
		public RE::BSTEventSink<RE::TESWaitStopEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (event && event->menuName == RE::LoadingMenu::MENU_NAME)
				globals::shaderCache->SetShaderPreparationTransition(1, event->opening);
			if (event && event->menuName == RE::MainMenu::MENU_NAME)
				globals::shaderCache->SetShaderPreparationMainMenu(event->opening);
			return RE::BSEventNotifyControl::kContinue;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStartEvent* event, RE::BSTEventSource<RE::TESSleepStartEvent>*) override
		{
			if (event)
				globals::shaderCache->SetShaderPreparationTransition(2, true);
			return RE::BSEventNotifyControl::kContinue;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStopEvent* event, RE::BSTEventSource<RE::TESSleepStopEvent>*) override
		{
			if (event)
				globals::shaderCache->SetShaderPreparationTransition(2, false);
			return RE::BSEventNotifyControl::kContinue;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESWaitStartEvent* event, RE::BSTEventSource<RE::TESWaitStartEvent>*) override
		{
			if (event)
				globals::shaderCache->SetShaderPreparationTransition(4, true);
			return RE::BSEventNotifyControl::kContinue;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESWaitStopEvent* event, RE::BSTEventSource<RE::TESWaitStopEvent>*) override
		{
			if (event)
				globals::shaderCache->SetShaderPreparationTransition(4, false);
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

namespace SIE
{
	bool ShaderCache::IsBlockingShaderStartup() const
	{
		return ShaderCompilationSchedulingPolicy::IsBlockingStartup(
			startupCompilationComplete.load(std::memory_order_acquire), menuLoaded.load(std::memory_order_acquire),
			backgroundCompilation.load(std::memory_order_acquire));
	}

	bool ShaderCache::OptionalPreparationAllowedLocked() const
	{
		return ShaderCompilationSchedulingPolicy::CanPrepareOptional(GetPreparationMode(), IsBlockingShaderStartup(),
			menuLoaded.load(std::memory_order_acquire) && optionalWorldReady && !optionalMainMenu, optionalTransitions != 0);
	}

	void ShaderCache::RegisterShaderPreparationEvents()
	{
		history.Initialize();
		static PreparationEvents events;
		if (auto* ui = globals::game::ui)
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&events);
		if (auto* source = RE::ScriptEventSourceHolder::GetSingleton()) {
			source->AddEventSink<RE::TESSleepStartEvent>(&events);
			source->AddEventSink<RE::TESSleepStopEvent>(&events);
			source->AddEventSink<RE::TESWaitStartEvent>(&events);
			source->AddEventSink<RE::TESWaitStopEvent>(&events);
		}
	}

	void ShaderCache::SetPreparationMode(ShaderCompilationSchedulingPolicy::PreparationMode mode)
	{
		{
			std::lock_guard lock(compilationSet.compilationMutex);
			preparationMode.store(ShaderCompilationSchedulingPolicy::ValidatePreparationMode(static_cast<int64_t>(mode)), std::memory_order_release);
			++optionalWindow;
		}
		compilationSet.conditionVariable.notify_all();
	}

	bool ShaderCache::TryAdmitOptionalShaderStage(OptionalStage stage)
	{
		std::lock_guard lock(compilationSet.compilationMutex);
		if (!OptionalPreparationAllowedLocked())
			return false;
		// Workers own immutable inputs and a concurrent device; world objects are
		// only consulted by the owner when publishing after lifecycle protection.
#ifdef DEVBENCH_BRIDGE_ENABLED
		switch (stage) {
		case OptionalStage::Cache:
			++optionalCacheStages;
			break;
		case OptionalStage::Source:
			++optionalSourceStages;
			break;
		case OptionalStage::Device:
			++optionalDeviceStages;
			break;
		}
#else
		(void)stage;
#endif
		return true;
	}

	void ShaderCache::SetShaderPreparationTransition(uint8_t kind, bool active)
	{
		{
			std::lock_guard lock(compilationSet.compilationMutex);
			const auto next = active ? optionalTransitions | kind : optionalTransitions & ~kind;
			if (next == optionalTransitions)
				return;
			optionalTransitions = static_cast<uint8_t>(next);
			++optionalWindow;
		}
		compilationSet.conditionVariable.notify_all();
	}

	ShaderCache::OptionalPreparationIdentity ShaderCache::CaptureOptionalPreparationIdentity(uint64_t ownerGeneration) const
	{
		return { GetGraphicsShaderRevision(), GetShaderPreparationInputGeneration(), ownerGeneration };
	}

	bool ShaderCache::CanQueueOptionalShaderPreparation(uint64_t key, OptionalPreparationIdentity identity)
	{
		std::lock_guard lock(compilationSet.compilationMutex);
		const auto found = optionalPreparations.find(key);
		if (found != optionalPreparations.end() && found->second->identity == identity)
			return false;
		return found != optionalPreparations.end() || optionalPreparations.size() < Util::ShaderSpecializationRequests::kMaximumRequests;
	}

	void ShaderCache::SetShaderPreparationMainMenu(bool active)
	{
		{
			std::lock_guard lock(compilationSet.compilationMutex);
			optionalMainMenu = active;
			if (active) {
				optionalWorldReady = false;
				optionalTransitions = 0;
			}
			++optionalWindow;
		}
		compilationSet.conditionVariable.notify_one();
	}

	bool ShaderCache::QueueOptionalShaderPreparation(uint64_t key, OptionalPreparationIdentity identity, std::function<bool()> run, std::function<void()> publish)
	{
		{
			std::lock_guard lock(compilationSet.compilationMutex);
			if (identity.revision != GetGraphicsShaderRevision() || identity.inputs != GetShaderPreparationInputGeneration())
				return false;
			if (const auto found = optionalPreparations.find(key); found != optionalPreparations.end()) {
				if (found->second->identity == identity)
					return false;
				if (found->second->startupCounted && !found->second->inFlight)
					pendingCompositePreparations.fetch_sub(1, std::memory_order_acq_rel);
				optionalPreparations.erase(found);
			}
			if (optionalPreparations.size() >= Util::ShaderSpecializationRequests::kMaximumRequests)
				return false;
			if (optionalReady.size() >= Util::ShaderSpecializationRequests::kMaximumRequests) {
				std::erase_if(optionalReady, [&](const auto& task) {
					const auto owner = optionalPreparations.find(task->key);
					return owner == optionalPreparations.end() || owner->second != task;
				});
			}
			auto task = std::make_shared<OptionalPreparation>();
			task->key = key;
			task->identity = identity;
			task->run = std::move(run);
			task->publish = std::move(publish);
			optionalPreparations.emplace(key, task);
			try {
				optionalReady.push_back(task);
			} catch (...) {
				optionalPreparations.erase(key);
				throw;
			}
			task->queued = true;
			task->startupCounted = IsBlockingShaderStartup();
			if (task->startupCounted)
				BeginCompositePreparation();
		}
		compilationSet.conditionVariable.notify_one();
		return true;
	}

	bool ShaderCache::DispatchOptionalPreparationLocked()
	{
		const auto capacity = ShaderCompilationSchedulingPolicy::DispatchLimit(compilationThreadCount, backgroundCompilationThreadCount,
			backgroundCompilation.load(std::memory_order_relaxed), compilationPool.get_thread_count());
		if (!OptionalPreparationAllowedLocked() ||
			!compilationSet.availableTasks.empty() || compilationSet.dispatchedTasksInFlight.load(std::memory_order_relaxed) >= capacity ||
			optionalInFlight >= static_cast<uint32_t>(std::max(backgroundCompilationThreadCount, 1)))
			return false;
		if (optionalDispatchWindow != optionalWindow) {
			try {
				for (auto& [key, task] : optionalPreparations) {
					if (!task->done && !task->inFlight && !task->queued && IsOptionalPreparationCurrent(*task)) {
						optionalReady.push_back(task);
						task->queued = true;
					}
				}
				optionalDispatchWindow = optionalWindow;
			} catch (...) {
				OutputDebugStringA("Optional shader backlog admission ran out of storage.\n");
			}
		}
		while (!optionalReady.empty()) {
			auto task = std::move(optionalReady.front());
			optionalReady.pop_front();
			task->queued = false;
			const auto owner = optionalPreparations.find(task->key);
			if (owner == optionalPreparations.end() || owner->second != task || task->done || task->inFlight || task->lastAttempt == optionalWindow || !IsOptionalPreparationCurrent(*task))
				continue;
			task->inFlight = true;
			task->lastAttempt = optionalWindow;
			++optionalInFlight;
			++compilationSet.dispatchedTasksInFlight;
			try {
				compilationPool.detach_task([this, task] {
					const auto thread = GetCurrentThread();
					const auto priority = GetThreadPriority(thread);
					const SKSE::stl::scope_exit restorePriority([&] { SetThreadPriority(thread, priority); });
					Util::SetCurrentThreadCooperativeBackgroundPriority();
					bool done = true;
					const SKSE::stl::scope_exit finishTask([&]() noexcept {
						{
							std::lock_guard lock(compilationSet.compilationMutex);
							task->done = done;
							task->inFlight = false;
							if (!done && task->lastAttempt != optionalWindow)
								optionalDispatchWindow = UINT64_MAX;
							if (done) {
								const auto owner = optionalPreparations.find(task->key);
								if (owner != optionalPreparations.end() && owner->second == task) {
									try {
										if (optionalCompleted.size() < Util::ShaderSpecializationRequests::kMaximumRequests)
											optionalCompleted.push_back(task);
										else
											optionalPublicationSweep = true;
									} catch (...) {
										optionalPublicationSweep = true;
									}
								}
							}
#ifdef DEVBENCH_BRIDGE_ENABLED
							if (task->lastAttempt != optionalWindow)
								++optionalPostCloseCompletions;
#endif
							--optionalInFlight;
							--compilationSet.dispatchedTasksInFlight;
							if (task->startupCounted) {
								task->startupCounted = false;
								pendingCompositePreparations.fetch_sub(1, std::memory_order_acq_rel);
							}
						}
						compilationSet.conditionVariable.notify_one();
						try {
							TryCompleteStartupCompilationPhase();
						} catch (...) {
							OutputDebugStringA("Optional shader startup completion logging failed.\n");
						}
					});
					try {
						done = task->run();
					} catch (const std::exception& error) {
						try {
							logger::error("Optional shader preparation failed: {}", error.what());
						} catch (...) {
							OutputDebugStringA("Optional shader preparation and failure logging failed.\n");
						}
					} catch (...) {
						OutputDebugStringA("Optional shader preparation failed; retaining general shading.\n");
					}
				});
			} catch (...) {
				task->inFlight = false;
				task->done = true;
				optionalPublicationSweep = true;
				--optionalInFlight;
				--compilationSet.dispatchedTasksInFlight;
				if (task->startupCounted) {
					task->startupCounted = false;
					pendingCompositePreparations.fetch_sub(1, std::memory_order_acq_rel);
				}
				OutputDebugStringA("Optional shader dispatch failed; retaining general shading.\n");
			}
			return true;
		}
		return false;
	}

	uint64_t ShaderCache::GetShaderPreparationWindowGeneration()
	{
		std::lock_guard lock(compilationSet.compilationMutex);
		return optionalWindow;
	}

	void ShaderCache::TickOptionalShaderPreparation(bool loading, bool mainMenu, bool safeMode, uint64_t observedWindow)
	{
		std::vector<std::function<void()>> publications;
		bool wake = false;
		try {
			std::lock_guard lock(compilationSet.compilationMutex);
			if (observedWindow == optionalWindow) {
				if (mainMenu) {
					optionalWorldReady = false;
					optionalTransitions = 0;
				} else if (!loading && !safeMode) {
					optionalWorldReady = true;
				}
				const auto transitions = loading ? optionalTransitions : optionalTransitions & ~1u;
				if (optionalMainMenu != mainMenu || optionalSafeMode != safeMode || transitions != optionalTransitions) {
					optionalMainMenu = mainMenu;
					optionalSafeMode = safeMode;
					optionalTransitions = static_cast<uint8_t>(transitions);
					++optionalWindow;
					wake = true;
				}
			}
			const auto revision = GetGraphicsShaderRevision();
			const auto inputs = GetShaderPreparationInputGeneration();
			const bool changed = revision != optionalMaintenanceRevision || inputs != optionalMaintenanceInputs;
			const auto stagePublication = [&](const std::shared_ptr<OptionalPreparation>& task) {
				if (task->publish && !task->publicationQueued) {
					if (optionalPublications.size() >= Util::ShaderSpecializationRequests::kMaximumRequests) {
						std::erase_if(optionalPublications, [&](const auto& old) {
							const auto owner = optionalPreparations.find(old->key);
							return owner == optionalPreparations.end() || owner->second != old;
						});
					}
					optionalPublications.push_back(task);
					task->publicationQueued = true;
				}
			};
			if (changed || optionalPublicationSweep) {
				for (auto it = optionalPreparations.begin(); it != optionalPreparations.end();) {
					auto& task = it->second;
					if (!task->inFlight && !IsOptionalPreparationCurrent(*task)) {
						if (task->startupCounted) {
							task->startupCounted = false;
							pendingCompositePreparations.fetch_sub(1, std::memory_order_acq_rel);
						}
						if (task->publish)
							publications.push_back(std::move(task->publish));
						it = optionalPreparations.erase(it);
						continue;
					}
					if (task->done)
						stagePublication(task);
					++it;
				}
				if (changed) {
					std::erase_if(optionalReady, [&](const auto& task) { return !IsOptionalPreparationCurrent(*task); });
					std::erase_if(optionalPublications, [&](const auto& task) { return !task->publish; });
					optionalDispatchWindow = UINT64_MAX;
					wake = true;
				}
				optionalMaintenanceRevision = revision;
				optionalMaintenanceInputs = inputs;
				optionalPublicationSweep = false;
			}
			while (!optionalCompleted.empty()) {
				auto task = std::move(optionalCompleted.front());
				optionalCompleted.pop_front();
				const auto owner = optionalPreparations.find(task->key);
				if (owner == optionalPreparations.end() || owner->second != task)
					continue;
				if (!IsOptionalPreparationCurrent(*task)) {
					if (task->publish)
						publications.push_back(std::move(task->publish));
					optionalPreparations.erase(owner);
				} else {
					stagePublication(task);
				}
			}
			if (!safeMode && !optionalSafeMode && !optionalMainMenu) {
				auto budget = std::max(backgroundCompilationThreadCount, 1);
				while (budget-- > 0 && !optionalPublications.empty()) {
					auto task = std::move(optionalPublications.front());
					optionalPublications.pop_front();
					task->publicationQueued = false;
					const auto owner = optionalPreparations.find(task->key);
					if (owner == optionalPreparations.end() || owner->second != task)
						continue;
					if (task->publish)
						publications.push_back(std::move(task->publish));
					task->run = {};
				}
			}
			wake |= optionalDispatchWindow != optionalWindow && OptionalPreparationAllowedLocked();
		} catch (...) {
			std::lock_guard lock(compilationSet.compilationMutex);
			optionalPublicationSweep = true;
			OutputDebugStringA("Optional shader publication storage is unavailable; retrying on a later frame.\n");
		}
		for (auto& publish : publications) {
			try {
				publish();
			} catch (...) {
				OutputDebugStringA("Optional shader publication failed; retaining general shading.\n");
			}
		}
		if (!safeMode && globals::deferred)
			globals::deferred->PrewarmDiscoveredComposites();
		if (wake)
			compilationSet.conditionVariable.notify_one();
	}

	void ShaderCache::RegisterSpecializationBase(RE::BSShader::Type type, uint32_t descriptor)
	{
		const auto mask = GetPixelSpecializationMask(type);
		if (!mask || !IsBlockingShaderStartup())
			return;
		std::lock_guard lock(specializationBasesMutex);
		specializationBases[type == RE::BSShader::Type::Lighting ? 0 : 1].insert(descriptor & ~mask);
	}

	std::vector<Util::ShaderSpecializationRequest> ShaderCache::GetStartupShaderSpecializations()
	{
		history.Initialize();
		return history.startup;
	}

	std::vector<Util::ShaderSpecializationRequest> ShaderCache::GetDiscoveredShaderSpecializations()
	{
		std::lock_guard lock(history.mutex);
		return history.requests.Snapshot();
	}

	size_t ShaderCache::GetShaderSpecializationRequestCount() const
	{
		return history.count.load(std::memory_order_relaxed);
	}

	void ShaderCache::RecordOptionalShaderPreparation(bool cacheHit, uint64_t sourceCompiles)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::lock_guard lock(compilationSet.compilationMutex);
		optionalCacheHits += cacheHit;
		optionalSourceCompiles += sourceCompiles;
#else
		(void)cacheHit;
		(void)sourceCompiles;
#endif
	}

	void ShaderCache::PrewarmDiscoveredPixelShaders(const RE::BSShader& shader)
	{
		const auto type = shader.shaderType.get();
		const auto mask = GetPixelSpecializationMask(type);
		if (!mask)
			return;
		if (!IsBlockingShaderStartup())
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
				history.limitWarningPending.store(true, std::memory_order_release);
			}
			if (history.savedRevision == history.revision && !history.limitWarningPending.load(std::memory_order_acquire))
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

#ifdef DEVBENCH_BRIDGE_ENABLED
	nlohmann::json ShaderCache::GetSpecializationPreparationStatus() const
	{
		std::lock_guard lock(compilationSet.compilationMutex);
		size_t queued = 0, deferred = 0, awaitingPublication = 0;
		for (const auto& [key, task] : optionalPreparations) {
			awaitingPublication += task->done && bool(task->publish);
			if (!task->done && !task->inFlight) {
				++queued;
				deferred += task->lastAttempt != UINT64_MAX;
			}
		}
		return { { "sourceCompilationAllowed", OptionalPreparationAllowedLocked() },
			{ "mode", static_cast<int>(GetPreparationMode()) },
			{ "admissionReason", IsBlockingShaderStartup() ? "blocking_startup" : !OptionalPreparationAllowedLocked() ? "policy_or_initial_world" :
																			  optionalTransitions                     ? "transition" :
																														"gameplay" },
			{ "transitionKinds", optionalTransitions }, { "windowGeneration", optionalWindow },
			{ "queued", queued }, { "deferred", deferred }, { "inFlight", optionalInFlight },
			{ "awaitingPublication", awaitingPublication },
			{ "activeBudget", std::min(static_cast<size_t>(std::max(backgroundCompilationThreadCount, 1)),
								  ShaderCompilationSchedulingPolicy::DispatchLimit(compilationThreadCount, backgroundCompilationThreadCount,
									  backgroundCompilation.load(std::memory_order_relaxed), compilationPool.get_thread_count())) },
			{ "cacheStages", optionalCacheStages }, { "sourceStages", optionalSourceStages }, { "deviceStages", optionalDeviceStages },
			{ "cacheHits", optionalCacheHits }, { "sourceCompiles", optionalSourceCompiles },
			{ "postWindowCompletions", optionalPostCloseCompletions },
			{ "discoveredRequests", history.count.load(std::memory_order_relaxed) },
			{ "discoveryPersistenceFailed", history.persistenceFailed.load(std::memory_order_relaxed) },
			{ "pendingComposites", pendingCompositePreparations.load(std::memory_order_acquire) } };
	}

#endif
}
