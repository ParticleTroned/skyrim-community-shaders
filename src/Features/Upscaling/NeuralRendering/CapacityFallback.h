#pragma once

#include <cstdint>

namespace NeuralRendering
{
	enum class CapacityFailure
	{
		Unsafe,
		Pressure,
		Unsupported
	};

#ifdef DEVBENCH_BRIDGE_ENABLED
	/** A rejected experimental count stays reduced until explicit fenced reset. */
	struct CapacityFallback
	{
		bool rejected = false;
		CapacityFailure reason = CapacityFailure::Unsafe;
		std::uint64_t recoveries = 0;

		/** Retirement must prove both API completion tails before any retry. */
		template <class Apply, class Classify, class Retire, class Fallback>
		bool ApplyBatch(bool higherCount, Apply apply, Classify classify, Retire retire, Fallback fallback)
		{
			if (rejected && higherCount)
				return fallback();
			if (apply())
				return true;
			if (!higherCount)
				return false;
			const auto failure = classify();
			if (failure == CapacityFailure::Unsafe)
				return false;
			rejected = true;
			reason = failure;
			if (!retire())
				return false;
			++recoveries;
			return fallback();
		}
	};
#endif
}
