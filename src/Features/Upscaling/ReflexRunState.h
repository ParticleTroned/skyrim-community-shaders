#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <atomic>
#	include <cstdint>

namespace ReflexDiagnostics::Suite
{
	/** Export completion and deferred restoration independently retain test ownership. */
	class RunState
	{
	public:
		bool TryStart()
		{
			uint8_t expected = 0;
			return flags.compare_exchange_strong(expected, kWorker);
		}
		bool Busy() const { return flags.load() != 0; }
		bool RestorationPending() const { return (flags.load() & kRestore) != 0; }
		void RequireRestoration() { flags.fetch_or(kRestore); }
		void RestorationFinished() { flags.fetch_and(static_cast<uint8_t>(~kRestore)); }
		void WorkerFinished() { flags.fetch_and(static_cast<uint8_t>(~kWorker)); }

	private:
		static constexpr uint8_t kWorker = 1, kRestore = 2;
		std::atomic_uint8_t flags{ 0 };
	};
}
#endif
