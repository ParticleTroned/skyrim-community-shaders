#pragma once

#include <cstdint>

namespace Util::NvidiaBindingWorkaround
{
	struct Snapshot
	{
		bool compiled{}, installed{}, enabled{}, refresh{ true }, locked{};
		const char* reason = "not_compiled";
		std::uint64_t candidates{}, applied{}, rejected{};
		std::uint64_t skipped{};
		const char* lastSkipped = "none";
		bool recordsLimited{};
		std::uint64_t loggingFailures{};
		long installationError{};
	};

#ifdef CSX_NVIDIA_BINDING_WORKAROUND
	/** Installs once for an exact verified driver binary, after device binding and before rendering. */
	void Initialize() noexcept;
	/** Stops further intervention without removing a potentially executing hook. */
	void Disable(const char* reason) noexcept;
	/** Selects an installed hook's matched control until the first eligible replacement. */
	bool Select(bool refresh) noexcept;
	/** Returns bounded CPU-side status; it does not attest GPU execution or mapping. */
	Snapshot Status() noexcept;
#else
	inline void Initialize() noexcept {}
	inline void Disable(const char*) noexcept {}
	inline bool Select(bool) noexcept { return false; }
	inline Snapshot Status() noexcept { return {}; }
#endif
}
