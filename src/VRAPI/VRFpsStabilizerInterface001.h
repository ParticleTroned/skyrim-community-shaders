#pragma once

#include <cstdint>

namespace VRFpsStabilizerPluginApi
{
	/** Revision 1 ABI supplied by the VR FPS Stabilizer author. Do not add virtual members. */
	class IVRFpsStabilizerInterface001
	{
	public:
		virtual unsigned int getBuildNumber() = 0;
		virtual void loadConfig() = 0;
		virtual void loadLocationConfig() = 0;
		virtual void ResetIniSettings() = 0;
	};

	struct VRFpsStabilizerMessage
	{
		static constexpr std::uint32_t kMessage_GetInterface = 0xF43A9D7C;
		void* (*GetApiFunction)(unsigned int revisionNumber) = nullptr;
	};

	/** Request the optional revision 1 interface during SKSE PostPostLoad, on VR only. */
	IVRFpsStabilizerInterface001* GetInterface();
}
