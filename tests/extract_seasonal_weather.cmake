file(READ "${SOURCE_FILE}" _source)
include("${CMAKE_CURRENT_LIST_DIR}/extract_source_region.cmake")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(READ "${CMAKE_CURRENT_LIST_DIR}/../extern/CommonLibSSE-NG/include/RE/B/BSSimpleList.h" _list)
string(REPLACE "#include \"RE/M/MemoryManager.h\"" "" _list "${_list}")
file(WRITE "${OUTPUT_DIRECTORY}/seasonal_weather_native_list_under_test.h" "${_list}")
extract_between(
    "${_source}" "\tRE::FormID ResolveFormKey" "\n}\n\nstruct SeasonalWeather::Plan"
    "seasonal_weather_list_under_test.h"
)
extract_between(
    "${_source}" "RE::TESWeather* SeasonalWeather::SelectWeather" "\nvoid SeasonalWeather::Apply"
    "seasonal_weather_selection_under_test.h"
)
extract_between(
    "${_source}" "void SeasonalWeather::DataLoaded" "\nRE::FormID SeasonalWeather::ResolveConfiguredForm"
    "seasonal_weather_detection_under_test.h"
)
extract_between(
    "${_source}" "void SeasonalWeather::Apply" "\nvoid SeasonalWeather::LoadSettings"
    "seasonal_weather_apply_under_test.h"
)
extract_between(
    "${_source}" "\tCategory Classify" "\n\tRE::FormID ResolveFormKey"
    "seasonal_weather_plan_helpers_under_test.h"
)
extract_between(
    "${_source}" "struct SeasonalWeather::Plan" "\nstd::pair<std::string"
    "seasonal_weather_plan_type_under_test.h"
)
extract_between(
    "${_source}" "RE::FormID SeasonalWeather::ResolveConfiguredForm" "\nRE::TESWeather* SeasonalWeather::SelectWeather"
    "seasonal_weather_plan_under_test.h"
)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../src/Features/SnowCover/Environment.cpp" _environment)
extract_between(
    "${_environment}" "\tfloat GetFractionalGameMonth" "\n\tfloat GetPrecipitationIntensity"
    "seasonal_weather_calendar_under_test.h"
)
