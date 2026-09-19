#pragma once

namespace NeuralRendering
{
	/** Tracks changes between source and neural input in completed DLSS history. */
	class PreDlssInputHistory
	{
	public:
		/** Requests a reset when this input differs from the last successful DLSS input. */
		[[nodiscard]] constexpr bool NeedsReset(bool a_enhancedInput) const noexcept
		{
			return enhancedInput_ != a_enhancedInput;
		}

		/** Failed DLSS calls must not advance the remembered input history. */
		constexpr void Complete(bool a_enhancedInput, bool a_dlssSucceeded) noexcept
		{
			if (a_dlssSucceeded)
				enhancedInput_ = a_enhancedInput;
		}

	private:
		bool enhancedInput_ = false;
	};
}
