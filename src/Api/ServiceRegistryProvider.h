#pragma once

#include <SKSE/SKSE.h>

namespace CSX::Api
{
	void InitializeServiceRegistryProvider();
	void HandleServiceRegistryMessage(SKSE::MessagingInterface::Message* a_message);
}
