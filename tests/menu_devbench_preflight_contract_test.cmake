if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_bridge_path "${PROJECT_ROOT}/src/MenuDevBenchBridge.cpp")
file(READ "${_bridge_path}" _bridge)

string(REGEX MATCH
    "R\"\\((\\{\"description\":\"Inspect and control the CSX VR menu[^\r\n]*\\})\\)\""
    _descriptor_match
    "${_bridge}"
)
if(NOT _descriptor_match)
    message(FATAL_ERROR "Menu DevBench descriptor was not found")
endif()
set(_descriptor "${CMAKE_MATCH_1}")
string(JSON _descriptor_type ERROR_VARIABLE _descriptor_error TYPE "${_descriptor}")
if(_descriptor_error OR NOT _descriptor_type STREQUAL "OBJECT")
    message(FATAL_ERROR "Invalid menu DevBench descriptor JSON: ${_descriptor_error}")
endif()

string(JSON _action_count LENGTH
    "${_descriptor}" inputSchema properties action enum
)
set(_prepare_coc_found FALSE)
set(_prepare_tuning_found FALSE)
set(_set_layout_unlocked_found FALSE)
set(_depth_culling_telemetry_enabled_found FALSE)
set(_depth_culling_telemetry_reset_found FALSE)
set(_adaptive_balance_enabled_found FALSE)
set(_foliage_lighting_enabled_found FALSE)
set(_terrain_variation_mesh_found FALSE)
set(_truepbr_verbose_found FALSE)
set(_dynamic_cubemap_resolution_found FALSE)
math(EXPR _action_last "${_action_count} - 1")
foreach(_index RANGE 0 ${_action_last})
    string(JSON _action GET
        "${_descriptor}" inputSchema properties action enum ${_index}
    )
    if(_action STREQUAL "prepare_coc")
        set(_prepare_coc_found TRUE)
    elseif(_action STREQUAL "prepare_tuning")
        set(_prepare_tuning_found TRUE)
    elseif(_action STREQUAL "set_depth_culling_telemetry_enabled")
        set(_depth_culling_telemetry_enabled_found TRUE)
    elseif(_action STREQUAL "reset_depth_culling_telemetry")
        set(_depth_culling_telemetry_reset_found TRUE)
    elseif(_action STREQUAL "set_adaptive_balance_enabled")
        set(_adaptive_balance_enabled_found TRUE)
    elseif(_action STREQUAL "set_foliage_lighting_enabled")
        set(_foliage_lighting_enabled_found TRUE)
    elseif(_action STREQUAL "set_terrain_variation_mesh_enabled")
        set(_terrain_variation_mesh_found TRUE)
    elseif(_action STREQUAL "set_truepbr_verbose_json_logging")
        set(_truepbr_verbose_found TRUE)
    elseif(_action STREQUAL "set_dynamic_cubemap_resolution")
        set(_dynamic_cubemap_resolution_found TRUE)
    endif()
    if(_action STREQUAL "set_layout_unlocked")
        set(_set_layout_unlocked_found TRUE)
    endif()
endforeach()
if(NOT _prepare_coc_found)
    message(FATAL_ERROR "Menu DevBench schema is missing prepare_coc")
endif()
if(NOT _prepare_tuning_found)
    message(FATAL_ERROR "Menu DevBench schema is missing prepare_tuning")
endif()
if(NOT _set_layout_unlocked_found)
    message(FATAL_ERROR "Menu DevBench schema is missing set_layout_unlocked")
endif()
if(NOT _depth_culling_telemetry_enabled_found)
    message(FATAL_ERROR
        "Menu DevBench schema is missing set_depth_culling_telemetry_enabled"
    )
endif()
if(NOT _depth_culling_telemetry_reset_found)
    message(FATAL_ERROR
        "Menu DevBench schema is missing reset_depth_culling_telemetry"
    )
endif()
if(NOT _adaptive_balance_enabled_found)
    message(FATAL_ERROR "Menu DevBench schema is missing set_adaptive_balance_enabled")
endif()
if(NOT _foliage_lighting_enabled_found)
    message(FATAL_ERROR
        "Menu DevBench schema is missing set_foliage_lighting_enabled"
    )
endif()
if(NOT _terrain_variation_mesh_found)
    message(FATAL_ERROR "Menu DevBench schema is missing set_terrain_variation_mesh_enabled")
endif()
if(NOT _truepbr_verbose_found)
    message(FATAL_ERROR
        "Menu DevBench schema is missing set_truepbr_verbose_json_logging"
    )
endif()
if(NOT _dynamic_cubemap_resolution_found)
    message(FATAL_ERROR
        "Menu DevBench schema is missing set_dynamic_cubemap_resolution"
    )
endif()

string(JSON _resolution_count LENGTH
    "${_descriptor}" inputSchema properties resolution enum
)
if(NOT _resolution_count EQUAL 2)
    message(FATAL_ERROR "Dynamic cubemap resolution schema must have two values")
endif()
string(JSON _performance_resolution GET
    "${_descriptor}" inputSchema properties resolution enum 0
)
string(JSON _quality_resolution GET
    "${_descriptor}" inputSchema properties resolution enum 1
)
if(NOT _performance_resolution EQUAL 128 OR NOT _quality_resolution EQUAL 256)
    message(FATAL_ERROR "Dynamic cubemap resolution schema must expose 128 and 256")
endif()

foreach(_required_behavior IN ITEMS
    "return CSX::Api::RunDevBenchMainThreadTask(SKSE::GetTaskInterface(), std::move(a_run));"
    "if (action == \"prepare_coc\")"
    "if (action == \"prepare_tuning\")"
    "PrepareRuntimePreflight(MenuDevBenchPreflightPolicy::Preparation::Coc)"
    "PrepareRuntimePreflight(MenuDevBenchPreflightPolicy::Preparation::Tuning)"
    "CaptureCocPreflightSnapshot"
    "GetVRFpsStabilizerSessionConfig()"
    "IsVRFpsStabilizerSyncActive()"
    "CanApplyRuntimeSettings(before.state, a_preparation)"
    "SetLogLevel(spdlog::level::debug)"
    "settings.foveatedVendorDispatch = true"
    "settings.periphery_taa_enable = true"
    "kFoveatedCenterArea"
    "kPeripheryTAACenterArea"
    "kPeripheryTAAOuterScale"
    "{ \"foliageLightingEnabled\", globals::features::foliageLighting.IsEnabled() }"
    "{ \"foliageLightingActive\", globals::features::foliageLighting.IsRuntimeEnabled() }"
    "globals::features::adaptiveBrightness.SetEnabled(enabled)"
    "{ \"adaptiveBalanceEnabled\", globals::features::adaptiveBrightness.settings.enabled }"
    "{ \"adaptiveBalanceActive\", globals::features::adaptiveBrightness.IsRuntimeEnabled() }"
    "globals::features::foliageLighting.SetEnabled(enabled)"
    "{ \"truePbrVerboseJsonLogging\", globals::features::truePBR.enableVerboseJsonLogging }"
    "globals::features::truePBR.enableVerboseJsonLogging = enabled"
    "{ \"configuredResolution\", dynamicCubemaps.settings.CubemapResolution }"
    "{ \"activeResolution\", dynamicCubemaps.GetActiveCubemapResolution() }"
    "{ \"restartRequired\", dynamicCubemaps.IsCubemapResolutionRestartRequired() }"
    "globals::features::dynamicCubemaps.SetCubemapResolution(resolution)"
    "menu->RequestSettingsDirtyCheck()"
    "{ \"persisted\", false }"
    "{ \"promptRequired\", true }"
    "{ \"menuLayoutUnlocked\", vr.settings.UnlockMenuPositionAndSize }"
    "{ \"savedUnlockedFixedWorldPositionInitialized\", vr.savedUnlockedFixedWorldOverlayPosition.initialized }"
    "{ \"menuScale\", vr.GetEffectiveMenuScale() }"
    "{ \"savedMenuScale\", vr.settings.VRMenuScale }"
    "globals::features::vr.SetMenuLayoutUnlocked(enabled)"
    "VRDepthCullingTemporal::SetTelemetryEnabled(enabled)"
    "VRDepthCullingTemporal::TryResetStatus()"
    "depth_culling_telemetry_busy"
    "\"durationHistogramNanoseconds\""
)
    string(FIND "${_bridge}" "${_required_behavior}" _behavior_position)
    if(_behavior_position EQUAL -1)
        message(FATAL_ERROR
            "Menu COC preflight behavior is missing: ${_required_behavior}"
        )
    endif()
endforeach()

string(FIND
    "${_bridge}"
    "CanApplyRuntimeSettings(before.state, a_preparation)"
    _mutation_guard_position
)
string(FIND
    "${_bridge}"
    "SetLogLevel(spdlog::level::debug)"
    _first_mutation_position
)
if(_mutation_guard_position GREATER _first_mutation_position)
    message(FATAL_ERROR
        "COC preflight mutates runtime settings before checking prerequisites"
    )
endif()

foreach(_forbidden_behavior IN ITEMS
    "SaveVRFpsStabilizerConfig"
    "SaveSettings"
)
    string(FIND "${_bridge}" "${_forbidden_behavior}" _forbidden_position)
    if(NOT _forbidden_position EQUAL -1)
        message(FATAL_ERROR
            "COC preflight bridge contains a persistence path: ${_forbidden_behavior}"
        )
    endif()
endforeach()

message(STATUS "Menu DevBench COC preflight contract is coherent")

foreach(_removed_surface IN ITEMS
    "set_depth_culling_performance_mode"
    "DepthCullingPerformanceMode"
)
    string(FIND "${_bridge}" "${_removed_surface}" _removed_position)
    if(NOT _removed_position EQUAL -1)
        message(FATAL_ERROR "Removed depth-culling mode remains exposed: ${_removed_surface}")
    endif()
endforeach()

foreach(_required_action IN ITEMS set_depth_culling_settings set_depth_culling_legacy_mode)
    set(_found FALSE)
    foreach(_index RANGE 0 ${_action_last})
        string(JSON _action GET "${_descriptor}" inputSchema properties action enum ${_index})
        if(_action STREQUAL _required_action)
            set(_found TRUE)
        endif()
    endforeach()
    if(NOT _found)
        message(FATAL_ERROR "Depth-culling action missing from schema: ${_required_action}")
    endif()
endforeach()

string(JSON _culling_schema GET "${_descriptor}" inputSchema properties depthCulling)
string(JSON _minimum_fields GET "${_culling_schema}" minProperties)
string(JSON _additional_fields GET "${_culling_schema}" additionalProperties)
string(JSON _field_count LENGTH "${_culling_schema}" properties)
if(NOT _minimum_fields EQUAL 1 OR _additional_fields OR NOT _field_count EQUAL 4)
    message(FATAL_ERROR "Depth-culling update must be nonempty and reject unknown fields")
endif()
foreach(_enable_field IN ITEMS exteriorEnabled interiorEnabled)
    string(JSON _field_type GET "${_culling_schema}" properties ${_enable_field} type)
    if(NOT _field_type STREQUAL "boolean")
        message(FATAL_ERROR "Depth-culling enable must be boolean: ${_enable_field}")
    endif()
endforeach()
foreach(_extent_field IN ITEMS exteriorMinExtent interiorMinExtent)
    string(JSON _field_type GET "${_culling_schema}" properties ${_extent_field} type)
    string(JSON _minimum GET "${_culling_schema}" properties ${_extent_field} minimum)
    string(JSON _maximum GET "${_culling_schema}" properties ${_extent_field} maximum)
    if(NOT _field_type STREQUAL "number" OR NOT _minimum EQUAL 0 OR NOT _maximum EQUAL 1000)
        message(FATAL_ERROR "Depth-culling extent schema does not match slider bounds: ${_extent_field}")
    endif()
endforeach()
foreach(_status_field IN ITEMS
    "depthCullingExteriorEnabled"
    "depthCullingInteriorEnabled"
    "depthCullingExteriorMinExtent"
    "depthCullingInteriorMinExtent"
    "depthCullingLegacyMode"
    "depthCullingConfiguredPolicy"
)
    string(FIND "${_bridge}" "${_status_field}" _status_position)
    if(_status_position EQUAL -1)
        message(FATAL_ERROR "Depth-culling status is incomplete: ${_status_field}")
    endif()
endforeach()

string(FIND "${_bridge}" "MenuDepthCullingSettingsPolicy::TryParse(" _culling_validate)
string(FIND "${_bridge}" "return RunOnMainThread([action," _culling_dispatch)
string(FIND "${_bridge}" "MenuDepthCullingSettingsPolicy::Apply(" _culling_apply)
if(_culling_validate EQUAL -1 OR _culling_dispatch EQUAL -1 OR _culling_apply EQUAL -1 OR
    _culling_validate GREATER _culling_dispatch OR _culling_dispatch GREATER _culling_apply)
    message(FATAL_ERROR "Depth-culling mutation must follow complete validation and main-thread dispatch")
endif()
message(STATUS "Independent depth-culling DevBench settings contract is coherent")

string(JSON _ambient_schema GET "${_descriptor}" inputSchema properties visuals properties ambient)
string(JSON _ambient_type GET "${_ambient_schema}" type)
string(JSON _ambient_min GET "${_ambient_schema}" minimum)
string(JSON _ambient_max GET "${_ambient_schema}" maximum)
if(NOT _ambient_type STREQUAL "number" OR NOT _ambient_min EQUAL 0 OR NOT _ambient_max EQUAL 5)
    message(FATAL_ERROR "Adaptive Balance Ambient schema must match its 0-5 slider")
endif()
