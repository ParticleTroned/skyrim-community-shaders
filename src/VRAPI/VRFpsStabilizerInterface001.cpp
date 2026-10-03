#include "VRAPI/VRFpsStabilizerInterface001.h"

namespace VRFpsStabilizerPluginApi
{
	IVRFpsStabilizerInterface001* GetInterface()
	{
		if (!REL::Module::IsVR())
			return nullptr;
		const auto* messaging = SKSE::GetMessagingInterface();
		if (!messaging)
			return nullptr;
		VRFpsStabilizerMessage message;
		if (!messaging->Dispatch(VRFpsStabilizerMessage::kMessage_GetInterface,
				&message, sizeof(message), "VRFpsStabilizerPlugin") ||
			!message.GetApiFunction)
			return nullptr;
		return static_cast<IVRFpsStabilizerInterface001*>(message.GetApiFunction(1));
	}
}
