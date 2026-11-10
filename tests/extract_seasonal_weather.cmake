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
    "${_source}" "void SeasonalWeather::Apply" "\nvoid SeasonalWeather::LoadSettings"
    "seasonal_weather_apply_under_test.h"
)
