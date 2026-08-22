#pragma once

#include "VRAPI/CSweatherapi.h"

namespace CSX::Api
{
	void InitializeWeatherService();
	const WeatherAPI::Interface001* GetWeatherService001();
}
