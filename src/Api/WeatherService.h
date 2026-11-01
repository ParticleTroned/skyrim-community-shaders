#pragma once

#include "CSXAPI/CSweatherapi.h"

namespace CSX::Api
{
	void InitializeWeatherService();
	const WeatherAPI::Interface001* GetWeatherService001();
}
