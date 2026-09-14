#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <latch>
#include <mutex>
#include <semaphore>
#include <stdexcept>
#include <thread>
#include <utility>

namespace RE
{
	struct BGSSaveLoadGame
	{
		static inline BGSSaveLoadGame* instance = nullptr;
		bool loading = false;
		bool saving = false;
		bool initingForms = false;
		bool deferInitForms = false;
		bool positioningPlayer = false;
		std::function<void()> onSampleSaving;
		static BGSSaveLoadGame* GetSingleton() { return instance; }
		bool GetSaveGameLoading() const { return loading; }
		bool GetSaveGameSaving()
		{
			if (onSampleSaving) {
				auto callback = std::move(onSampleSaving);
				callback();
			}
			return saving;
		}
		bool GetInitingForms() const { return initingForms; }
		bool GetDeferInitForms() const { return deferInitForms; }
		bool GetPositioningPlayerCharacter() const { return positioningPlayer; }
	};
}

struct ShaderCache
{
	std::atomic_bool blocked{ false };
	std::function<void()> onUnblock;
	void SetSaveLoadDiskPersistenceBlocked(bool a_blocked)
	{
		blocked = a_blocked;
		if (!a_blocked && onUnblock) {
			auto callback = std::move(onUnblock);
			callback();
		}
	}
};

namespace globals
{
	inline ShaderCache* shaderCache = nullptr;
}
namespace logger
{
	inline uint32_t warningCount = 0;
	inline std::function<void()> onWarning;
	template <class... Args>
	void warn(const char*, Args...)
	{
		++warningCount;
		if (onWarning) {
			auto callback = std::move(onWarning);
			callback();
		}
	}
}

#include "ordinary_save_state_under_test.h"

namespace
{
	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	struct Fixture
	{
		RE::BGSSaveLoadGame engine;
		ShaderCache cache;
		State state;
		Fixture()
		{
			RE::BGSSaveLoadGame::instance = &engine;
			globals::shaderCache = &cache;
			Tick(1);
		}
		~Fixture()
		{
			RE::BGSSaveLoadGame::instance = nullptr;
			globals::shaderCache = nullptr;
		}
		void Tick(uint32_t a_frame)
		{
			state.frameCount = a_frame;
			state.frameCountAtomic.store(a_frame);
			state.UpdateSaveLoadSafeMode();
		}
		void Save(uint32_t a_frame)
		{
			state.NotifyOrdinarySave(a_frame);
			state.ExtendPersistentMutationBlock(a_frame);
		}
	};

	void TestNotificationOnlyAndMutationGrace()
	{
		Fixture f;
		f.Save(10);
		const uint64_t token = f.state.GetOrdinarySaveRenderRecoveryToken();
		Check(token != 0, "A completed save notification must admit stereo qualification");
		Check(f.state.saveLoadSafeModeEndFrame == 130, "Saving must retain 120-frame mutation grace");
		for (uint32_t frame = 11; frame < 130; ++frame) {
			f.Tick(frame);
			Check(f.state.GetOrdinarySaveRenderRecoveryToken() == token,
				"Idle frames must preserve the ordinary-save token");
			Check(f.state.IsSaveLoadSafeModeActive() && f.state.IsPersistentMutationBlocked() && f.cache.blocked,
				"Early rendering eligibility must preserve all mutation and disk protection");
		}
		f.Tick(130);
		Check(!f.state.IsSaveLoadSafeModeActive() && !f.state.IsPersistentMutationBlocked() && !f.cache.blocked,
			"The original mutation grace must expire at its original frame");
		Check(f.state.GetOrdinarySaveRenderRecoveryToken() == 0, "Completed guard must clear save provenance");
	}

	void TestEngineSavingAndRepeatedNotifications()
	{
		Fixture f;
		f.Save(10);
		const uint64_t firstToken = f.state.GetOrdinarySaveRenderRecoveryToken();
		f.Save(11);
		const uint64_t secondToken = f.state.GetOrdinarySaveRenderRecoveryToken();
		Check(firstToken != secondToken && secondToken != 0,
			"Every save notification must invalidate earlier stereo qualification");
		f.engine.saving = true;
		f.Tick(12);
		Check(f.state.GetOrdinarySaveRenderRecoveryToken() == 0 && f.state.IsEngineSaveLoadActivityActive(),
			"Live engine saving must block shortened recovery");
		f.Tick(13);
		f.engine.saving = false;
		f.Tick(14);
		const uint64_t thirdToken = f.state.GetOrdinarySaveRenderRecoveryToken();
		Check(thirdToken != 0 && thirdToken != secondToken,
			"Engine saving must require a new coherent stereo sequence");
		Check(f.state.saveLoadSafeModeEndFrame == 133, "Actual saving must retain the full trailing grace");
		f.engine.saving = true;
		f.Tick(15);
		f.engine.saving = false;
		f.Tick(16);
		Check(f.state.GetOrdinarySaveRenderRecoveryToken() != thirdToken,
			"A second engine saving edge must be detected without an SKSE notification");
	}

	void TestLoadAndInitializationRevokeOrdinarySave()
	{
		for (int hazard = 0; hazard < 8; ++hazard) {
			Fixture f;
			f.Save(10);
			switch (hazard) {
			case 0:
				f.engine.loading = true;
				break;
			case 1:
				f.engine.initingForms = true;
				break;
			case 2:
				f.engine.deferInitForms = true;
				break;
			case 3:
				f.engine.positioningPlayer = true;
				break;
			case 4:
				f.state.isLoadingMenuOpen = true;
				break;
			case 5:
				f.state.isMainMenuOpen = true;
				break;
			case 6:
				f.state.pendingPostLoadRuntimeReset = true;
				break;
			case 7:
				RE::BGSSaveLoadGame::instance = nullptr;
				break;
			}
			f.Tick(11);
			Check(f.state.GetOrdinarySaveRenderRecoveryToken() == 0,
				"Load, initialization, menus and unknown state must revoke ordinary-save eligibility");
			f.engine = {};
			RE::BGSSaveLoadGame::instance = &f.engine;
			f.state.isLoadingMenuOpen = false;
			f.state.isMainMenuOpen = false;
			f.state.pendingPostLoadRuntimeReset = false;
			f.Tick(12);
			f.Save(13);
			Check(f.state.GetOrdinarySaveRenderRecoveryToken() == 0,
				"Saving during a load or unknown grace must never downgrade its protection");
			f.Tick(133);
			f.Save(134);
			Check(f.state.GetOrdinarySaveRenderRecoveryToken() != 0,
				"An independent later save may qualify after the old guard has expired");
		}
	}

	void TestLoadNotificationsAndFallback()
	{
		for (bool preLoad : { false, true }) {
			Fixture f;
			if (preLoad)
				f.state.BeginSaveLoadSafeMode(10);
			else
				f.state.ExtendSaveLoadSafeMode(10);
			f.Save(11);
			Check(f.state.GetOrdinarySaveRenderRecoveryToken() == 0,
				"Preload and postload/new-game notification provenance must survive an overlapping save");
			f.Tick(131);
			f.Save(132);
			Check(f.state.GetOrdinarySaveRenderRecoveryToken() != 0,
				"Expired load notification protection must permit later ordinary saves");
		}
		Fixture f;
		const uint32_t warningsBefore = logger::warningCount;
		f.state.BeginSaveLoadSafeMode(10);
		f.Tick(10 + State::kSaveLoadSafeModeFallbackFrames);
		Check(logger::warningCount == warningsBefore + 1 && !f.state.IsSaveLoadSafeModeActive(),
			"The existing no-completion fallback must remain unchanged");
		f.Save(f.state.frameCount + 1);
		Check(f.state.GetOrdinarySaveRenderRecoveryToken() != 0,
			"Expired fallback must clear old load provenance");
	}

	void TestConcurrentNotificationDuringExpiry()
	{
		for (bool ordinarySave : { false, true }) {
			Fixture f;
			f.Save(10);
			const uint64_t previousToken = f.state.GetOrdinarySaveRenderRecoveryToken();
			std::thread notification;
			std::latch notificationStarted{ 1 };
			f.cache.onUnblock = [&] {
				notification = std::thread([&] {
					notificationStarted.count_down();
					if (ordinarySave)
						f.Save(130);
					else
						f.state.BeginSaveLoadSafeMode(130);
				});
				notificationStarted.wait();
			};
			f.Tick(130);
			notification.join();
			Check(f.state.IsSaveLoadSafeModeActive() && f.cache.blocked,
				"A new event during expiry must preserve its newly armed mutation guard");
			if (ordinarySave) {
				const uint64_t token = f.state.GetOrdinarySaveRenderRecoveryToken();
				Check(token != 0 && token != previousToken,
					"Expiry must not erase a concurrent ordinary-save generation");
			} else {
				f.Save(131);
				Check(f.state.GetOrdinarySaveRenderRecoveryToken() == 0,
					"Expiry must not erase a concurrent load disqualifier");
			}
		}
	}

	void TestConcurrentNotificationBeforeGuardPublication()
	{
		for (bool expiring : { false, true }) {
			for (bool ordinarySave : { false, true }) {
				Fixture f;
				const uint32_t frame = expiring ? 10 + State::kSaveLoadSafeModeFallbackFrames : 10;
				std::binary_semaphore updatePaused{ 0 };
				std::binary_semaphore notificationFinished{ 0 };
				auto pauseUpdate = [&] {
					updatePaused.release();
					// Permit a lock-free notifier to expose lost publication, or let a serialized writer wait.
					(void)notificationFinished.try_acquire_for(std::chrono::milliseconds(50));
				};
				if (expiring) {
					f.state.BeginSaveLoadSafeMode(10);
					logger::onWarning = pauseUpdate;
				} else {
					f.engine.onSampleSaving = pauseUpdate;
				}
				std::thread updating([&] { f.Tick(frame); });
				updatePaused.acquire();
				if (ordinarySave)
					f.state.NotifyOrdinarySave(frame);
				else
					f.state.BeginSaveLoadSafeMode(frame);
				notificationFinished.release();
				updating.join();
				Check(f.state.IsSaveLoadSafeModeActive() && f.state.IsPersistentMutationBlocked() && f.cache.blocked,
					"A notification concurrent with guard publication must retain every mutation guard");
				if (ordinarySave) {
					Check(f.state.saveLoadSafeModeEndFrame == frame + State::kSaveLoadSafeModeGraceFrames &&
							  f.state.GetOrdinarySaveRenderRecoveryToken() != 0,
						"Concurrent ordinary saves must retain their full deadline and current source");
				} else {
					Check(f.state.saveLoadSafeModeStartFrame == frame && f.state.saveLoadSafeModeEndFrame == 0 &&
							  f.state.GetOrdinarySaveRenderRecoveryToken() == 0,
						"Concurrent loads must retain their start deadline and load source");
				}
			}
		}
	}

	void TestConcurrentSaveAndLoadNotifications()
	{
		for (uint32_t repeat = 0; repeat < 64; ++repeat) {
			Fixture f;
			std::latch start{ 1 };
			std::thread saving([&] {
				start.wait();
				f.state.NotifyOrdinarySave(10);
			});
			std::thread loading([&] {
				start.wait();
				f.state.BeginSaveLoadSafeMode(10);
			});
			start.count_down();
			saving.join();
			loading.join();
			f.Save(11);
			Check(f.state.GetOrdinarySaveRenderRecoveryToken() == 0,
				"Concurrent save and load notifications must retain load provenance");
		}
	}
}

int main()
{
	try {
		TestNotificationOnlyAndMutationGrace();
		TestEngineSavingAndRepeatedNotifications();
		TestLoadAndInitializationRevokeOrdinarySave();
		TestLoadNotificationsAndFallback();
		TestConcurrentNotificationDuringExpiry();
		TestConcurrentNotificationBeforeGuardPublication();
		TestConcurrentSaveAndLoadNotifications();
		std::cout << "Ordinary save state: provenance, mutation grace, engine activity, overlap and expiry passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
