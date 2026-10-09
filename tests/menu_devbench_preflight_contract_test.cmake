if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_bridge_path "${PROJECT_ROOT}/src/MenuDevBenchBridge.cpp")
file(READ "${_bridge_path}" _bridge)

string(REGEX MATCH
    "R\"\\((\\{\"description\":\"[^\r\n]*\\})\\)\""
    _descriptor_match
    "${_bridge}"
)
if(NOT _descriptor_match)
    message(FATAL_ERROR "Menu DevBench descriptor was not found")
endif()
set(_descriptor "${CMAKE_MATCH_1}")
string(JSON _preparation_ok_type GET "${_descriptor}" outputSchema properties ok type)
if(NOT _preparation_ok_type STREQUAL "boolean")
    message(FATAL_ERROR "Preparation success marker must be a boolean")
endif()
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
    [[{ "ok", ready }]]
    [[{ "ok", false }]]
    ".neuralRenderingEnabled = settings.neuralRenderingEnabled"
    "nr_must_be_disabled_for_taa_fixture"
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
        "depth_culling_snapshot"
        "set_depth_culling_method"
        "set_depth_culling_matched_diagnostics_enabled"
        "set_depth_culling_source_refinement_enabled"
        "set_depth_culling_direct_intersection_enabled"
        "set_depth_culling_far_clip_enabled"
        "set_depth_culling_traversal_diagnostics_enabled"
)
    string(FIND "${_bridge}" "${_removed_surface}" _removed_position)
    if(NOT _removed_position EQUAL -1)
        message(FATAL_ERROR "Removed depth-culling mode remains exposed: ${_removed_surface}")
    endif()
endforeach()

file(
    GLOB _retired_scene_paths
    "${PROJECT_ROOT}/src/Features/VRHybridCulling*"
    "${PROJECT_ROOT}/package/Shaders/VRHybridCulling*"
)
list(
    APPEND _retired_scene_paths
    "${PROJECT_ROOT}/package/Shaders/Common/DepthOrder.hlsli"
    "${PROJECT_ROOT}/src/Features/VRDepthCullingTelemetry.h"
    "${PROJECT_ROOT}/src/Features/VRNativeVisibilityTelemetry.h"
    "${PROJECT_ROOT}/src/MenuDepthCullingDiagnostics.h"
)
foreach(_retired_path IN LISTS _retired_scene_paths)
    if(EXISTS "${_retired_path}")
        message(
            FATAL_ERROR
            "Retired scene-culling file remains: ${_retired_path}"
        )
    endif()
endforeach()

file(
    GLOB_RECURSE _runtime_sources
    LIST_DIRECTORIES FALSE
    "${PROJECT_ROOT}/src/*.cpp"
    "${PROJECT_ROOT}/src/*.h"
    "${PROJECT_ROOT}/include/*.h"
    "${PROJECT_ROOT}/package/*.hlsl"
    "${PROJECT_ROOT}/package/*.hlsli"
    "${PROJECT_ROOT}/features/*.hlsl"
    "${PROJECT_ROOT}/features/*.hlsli"
)
list(APPEND _runtime_sources "${PROJECT_ROOT}/CMakeLists.txt")
foreach(_source_path IN LISTS _runtime_sources)
    file(READ "${_source_path}" _source)
    foreach(
        _retired_surface
        IN
        ITEMS
            VRHybridCulling
            VRNativeVisibilityTelemetry
            MenuDepthCullingDiagnostics
            VRDepthCullingTelemetry.h
            DepthOrder.hlsli
            DepthCullingMethod
            kSceneHiZMode
            IsGrassHiZAvailable
            OcclusionAllowed
    )
        string(FIND "${_source}" "${_retired_surface}" _retired_position)
        if(NOT _retired_position EQUAL -1)
            message(
                FATAL_ERROR
                "Retired scene-culling reference in ${_source_path}: ${_retired_surface}"
            )
        endif()
    endforeach()
endforeach()

string(
    JSON _scene_method
    ERROR_VARIABLE _scene_method_error
    TYPE "${_descriptor}"
    inputSchema
    properties
    method
)
if(NOT _scene_method_error)
    message(FATAL_ERROR "Retired scene-culling method remains in the schema")
endif()

string(
    JSON _grass_schema
    GET "${_descriptor}"
    inputSchema
    properties
    grassOptimizations
)
string(
    JSON _grass_hiz_type
    ERROR_VARIABLE _grass_hiz_error
    GET "${_grass_schema}"
    properties
    EnableOcclusionCulling
    type
)
if(_grass_hiz_error OR NOT _grass_hiz_type STREQUAL "boolean")
    message(FATAL_ERROR "Grass Hi-Z enable schema must remain boolean")
endif()
string(
    JSON _grass_bias_type
    GET "${_grass_schema}"
    properties
    OcclusionBias
    type
)
string(
    JSON _grass_bias_minimum
    GET "${_grass_schema}"
    properties
    OcclusionBias
    minimum
)
string(
    JSON _grass_bias_maximum
    GET "${_grass_schema}"
    properties
    OcclusionBias
    maximum
)
if(
    NOT _grass_bias_type STREQUAL "number"
    OR NOT _grass_bias_minimum EQUAL 0
    OR NOT _grass_bias_maximum EQUAL 0.05
)
    message(FATAL_ERROR "Grass Hi-Z bias schema must retain its 0-0.05 range")
endif()
message(
    STATUS
    "Scene-culling removal and preserved Grass Hi-Z schema are coherent"
)

foreach(
    _required_action
    IN
    ITEMS
        set_depth_culling_settings
        set_depth_culling_legacy_mode
        set_grass_optimizations_enabled
        set_grass_optimizations_settings
        set_grass_hiz_enabled
        set_grass_optimizations_diagnostics_enabled
        set_pbr_grass_enabled
        set_pbr_grass_diagnostics_enabled
)
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

string(REGEX MATCHALL "action != \"[a-z_]+\"" _action_guards "${_bridge}")
set(_accepted_actions)
foreach(_guard IN LISTS _action_guards)
    string(REGEX REPLACE "action != \"([a-z_]+)\"" "\\1" _action "${_guard}")
    list(APPEND _accepted_actions "${_action}")
endforeach()
string(
    REGEX MATCH "\"supported\", json::array\\(\\{ ([^}]+) \\}\\)"
    _supported_match
    "${_bridge}"
)
if(NOT _supported_match)
    message(FATAL_ERROR "Menu DevBench supported-action response was not found")
endif()
string(REGEX MATCHALL "\"[a-z_]+\"" _supported_literals "${CMAKE_MATCH_1}")
set(_supported_actions)
foreach(_literal IN LISTS _supported_literals)
    string(REPLACE "\"" "" _action "${_literal}")
    list(APPEND _supported_actions "${_action}")
endforeach()
set(_schema_actions)
foreach(_index RANGE 0 ${_action_last})
    string(
        JSON _action
        GET "${_descriptor}"
        inputSchema
        properties
        action
        enum
        ${_index}
    )
    list(APPEND _schema_actions "${_action}")
endforeach()
list(REMOVE_DUPLICATES _accepted_actions)
list(SORT _accepted_actions)
list(SORT _supported_actions)
list(SORT _schema_actions)
if(
    NOT _accepted_actions STREQUAL _schema_actions
    OR NOT _supported_actions STREQUAL _schema_actions
)
    message(
        FATAL_ERROR
        "Menu DevBench accepted, supported and schema actions differ"
    )
endif()

string(
    FIND "${_bridge}"
    "} else if (action == \"set_depth_culling_legacy_mode\") {"
    _legacy_start
)
if(_legacy_start LESS 0)
    message(FATAL_ERROR "Native Legacy setter was not found")
endif()
string(SUBSTRING "${_bridge}" ${_legacy_start} -1 _legacy_tail)
string(
    FIND "${_legacy_tail}"
    "} else if (action == \"set_adaptive_balance_enabled\")"
    _legacy_end
)
if(_legacy_end LESS 0)
    message(FATAL_ERROR "Native Legacy setter boundary was not found")
endif()
string(SUBSTRING "${_legacy_tail}" 0 ${_legacy_end} _legacy_setter)
foreach(
    _response_field
    IN
    ITEMS
        "return {"
        "{ \"action\", action }"
        "{ \"enabled\", enabled }"
        "{ \"method\", VRDepthCullingTemporal::GetModeName(globals::features::vr.GetDepthCullingMode()) }"
        "{ \"persisted\", false }"
        "{ \"status\", BuildStatus() }"
)
    string(FIND "${_legacy_setter}" "${_response_field}" _response_position)
    if(_response_position LESS 0)
        message(
            FATAL_ERROR
            "Native Legacy setter lost response contract: ${_response_field}"
        )
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
string(FIND "${_bridge}" "return RunOnMainThread([action, path, enabled, resolution," _culling_dispatch)
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

# Clients need input bounds and viewport acknowledgements for bounded UI scrolling.
foreach(_bound IN ITEMS minimum maximum)
    string(JSON _value GET "${_descriptor}" inputSchema properties scrollRatio ${_bound})
    if((_bound STREQUAL "minimum" AND NOT _value EQUAL 0) OR
       (_bound STREQUAL "maximum" AND NOT _value EQUAL 1))
        message(FATAL_ERROR "Menu scroll ratio schema must stay within [0,1]")
    endif()
endforeach()
foreach(_field IN ITEMS fresh pending)
    string(JSON _type GET "${_descriptor}" outputSchema properties viewport properties ${_field} type)
    if(NOT _type STREQUAL "boolean")
        message(FATAL_ERROR "Menu viewport state must be boolean: ${_field}")
    endif()
endforeach()
foreach(_field IN ITEMS requestedGeneration appliedGeneration frame)
    string(JSON _type GET "${_descriptor}" outputSchema properties viewport properties ${_field} type)
    if(NOT _type STREQUAL "integer")
        message(FATAL_ERROR "Menu viewport acknowledgement must be an integer: ${_field}")
    endif()
endforeach()
foreach(_field IN ITEMS scrollY scrollMaxY height)
    string(JSON _type GET "${_descriptor}" outputSchema properties viewport properties ${_field} type)
    if(NOT _type STREQUAL "number")
        message(FATAL_ERROR "Menu viewport geometry must be numeric: ${_field}")
    endif()
endforeach()
