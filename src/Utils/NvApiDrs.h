#pragma once

namespace Util::NvApiDrs
{
	/**
	 * @brief Clears the NVIDIA profile override that blocks DLSS-G for Skyrim SE.
	 *
	 * This must run before the first graphics device is created because the driver
	 * latches the profile state during device initialization.
	 */
	void EnsureSkyrimSEDLSSGAllowed();
}
