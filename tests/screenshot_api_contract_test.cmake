if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

set(_json_files
    "docs/development/schemas/screenshot-request-v1.schema.json"
    "docs/development/schemas/screenshot-response-v1.schema.json"
    "docs/development/schemas/screenshot-event-v1.schema.json"
    "docs/development/schemas/screenshot-sequence-manifest-v1.schema.json"
    "tests/data/screenshot-api/capture-request-v1.json"
    "tests/data/screenshot-api/sequence-request-v1.json"
    "tests/data/screenshot-api/sequence-manifest-v1.json"
    "docs/development/unified-preset-policy.json"
)

foreach(_relative IN LISTS _json_files)
    set(_path "${PROJECT_ROOT}/${_relative}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing screenshot contract file: ${_relative}")
    endif()
    file(READ "${_path}" _contents)
    string(JSON _type ERROR_VARIABLE _error TYPE "${_contents}")
    if(_error)
        message(FATAL_ERROR "Invalid JSON in ${_relative}: ${_error}")
    endif()
    if(NOT _type STREQUAL "OBJECT")
        message(FATAL_ERROR "Expected a JSON object in ${_relative}")
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/tests/data/screenshot-api/sequence-manifest-v1.json" _manifest_fixture)
foreach(_manifest_member IN ITEMS
    producer client acceptedUtc completedUtc requested effective actual counts
    children warnings errors packaging terminalOutcome
)
    string(JSON _manifest_member_type ERROR_VARIABLE _manifest_error TYPE "${_manifest_fixture}" "${_manifest_member}")
    if(_manifest_error)
        message(FATAL_ERROR "Sequence manifest fixture is missing ${_manifest_member}: ${_manifest_error}")
    endif()
endforeach()
string(JSON _fixture_cancelled GET "${_manifest_fixture}" counts cancelled)
string(JSON _fixture_fallback GET "${_manifest_fixture}" actual fallbacksPresent)
string(JSON _fixture_view GET "${_manifest_fixture}" children 0 artifacts 0 actual view)
if(NOT _fixture_cancelled EQUAL 0 OR NOT _fixture_fallback OR NOT _fixture_view STREQUAL "source_native")
    message(FATAL_ERROR "Sequence manifest fixture does not preserve counters, fallback, and artifact provenance")
endif()

file(READ "${PROJECT_ROOT}/docs/development/unified-preset-policy.json" _policy)
foreach(_legacy IN ITEMS
    SequenceFrameCount
    SequenceFrameInterval
    SequencePreviewFramesPerSecond
    SequenceSaveSeparateEyes
    SequenceWritePreviewVideo
)
    string(FIND "${_policy}" "${_legacy}" _legacy_position)
    if(NOT _legacy_position EQUAL -1)
        message(FATAL_ERROR "Legacy flat screenshot setting remains in unified preset policy: ${_legacy}")
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/src/Features/ScreenshotApi.cpp" _implementation)
foreach(_action IN ITEMS
    capabilities status settings_get settings_validate settings_apply capture
    sequence_start sequence_stop request_get request_list request_cancel
    events_poll acknowledge
)
    string(FIND "${_implementation}" "\"${_action}\"" _action_position)
    if(_action_position EQUAL -1)
        message(FATAL_ERROR "Screenshot API action is not implemented: ${_action}")
    endif()
endforeach()

foreach(_event IN ITEMS
    request.accepted source.waiting source.fallback artifact.queued
    artifact.encoding artifact.written artifact.failed sequence.frame_scheduled
    sequence.stop_requested sequence.finalizing request.terminal
)
    string(FIND "${_implementation}" "\"${_event}\"" _event_position)
    if(_event_position EQUAL -1)
        message(FATAL_ERROR "Screenshot API event is not implemented: ${_event}")
    endif()
endforeach()

foreach(_required_contract_text IN ITEMS
    runtime_session persistent_user settings_default file_reference
    maximumOutputsPerFrame retentionSeconds manifest_failed
	DescribeCommittedArtifact BuildProvenance::GetProducer artifact_hash_failed
	terminalOutcome completedUtc fallbacksPresent cancelled
)
    string(FIND "${_implementation}" "${_required_contract_text}" _contract_position)
    if(_contract_position EQUAL -1)
        message(FATAL_ERROR "Screenshot API implementation is missing contract behavior: ${_required_contract_text}")
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/docs/development/schemas/screenshot-request-v1.schema.json" _request_schema)
foreach(_required_schema_text IN ITEMS runtime_session persistent_user settings_apply frameManifest previewVideo)
    string(FIND "${_request_schema}" "${_required_schema_text}" _schema_position)
    if(_schema_position EQUAL -1)
        message(FATAL_ERROR "Screenshot request schema is missing: ${_required_schema_text}")
    endif()
endforeach()

file(READ "${PROJECT_ROOT}/src/ScreenshotDevBenchBridge.cpp" _bridge)
string(FIND "${_bridge}" "communityshaders.screenshot" _tool_position)
if(_tool_position EQUAL -1)
    message(FATAL_ERROR "communityshaders.screenshot is not registered")
endif()

file(READ "${PROJECT_ROOT}/include/VRAPI/CSscreenshotapi.h" _native_header)
string(FIND "${_native_header}" "ServiceName[] = \"csx.screenshot\"" _native_name_position)
string(FIND "${_native_header}" "Status (*Dispatch)" _native_dispatch_position)
if(_native_name_position EQUAL -1 OR _native_dispatch_position EQUAL -1)
    message(FATAL_ERROR "Screenshot API native CSXR contract is missing")
endif()

file(READ "${PROJECT_ROOT}/src/Api/ServiceRegistryProvider.cpp" _registry_provider)
string(FIND "${_registry_provider}" "InitializeScreenshotService();" _native_registration_position)
if(_native_registration_position EQUAL -1)
    message(FATAL_ERROR "Screenshot API is not registered with CSXR")
endif()

file(READ "${PROJECT_ROOT}/src/Features/ScreenshotFeature.cpp" _feature_controls)
string(FIND "${_feature_controls}" "void ScreenshotFeature::RequestUiCapture()" _ui_adapter_position)
string(FIND "${_feature_controls}" "RequestApiCapture(\"ui\")" _ui_v1_position)
string(FIND "${_feature_controls}" "DispatchScreenshotServiceRequest" _control_dispatch_position)
if(_ui_adapter_position EQUAL -1 OR _ui_v1_position EQUAL -1 OR _control_dispatch_position EQUAL -1)
    message(FATAL_ERROR "Native screenshot UI must submit through the public contract-v1 screenshot service")
endif()

file(READ "${PROJECT_ROOT}/src/Menu.cpp" _menu)
string(FIND "${_menu}" "screenshotFeature.RequestUiCapture()" _hotkey_v1_position)
if(_hotkey_v1_position EQUAL -1)
    message(FATAL_ERROR "Screenshot hotkey does not use the contract-v1 UI adapter")
endif()

file(READ "${PROJECT_ROOT}/src/MenuDevBenchBridge.cpp" _menu_bridge)
foreach(_obsolete_contract_text IN ITEMS
    "communityshaders.menu screenshot is obsolete"
    "\"obsolete\", true"
    "\"tool\", \"communityshaders.screenshot\""
    "\"contractMajor\", 1"
)
    string(FIND "${_menu_bridge}" "${_obsolete_contract_text}" _obsolete_position)
    if(_obsolete_position EQUAL -1)
        message(FATAL_ERROR "Obsolete menu screenshot adapter is missing migration metadata: ${_obsolete_contract_text}")
    endif()
endforeach()
string(FIND "${_bridge}" "#ifdef DEVBENCH_BRIDGE_ENABLED" _bridge_guard_position)
if(_bridge_guard_position EQUAL -1)
    message(FATAL_ERROR "Screenshot bridge does not use the project's DevBench compile guard")
endif()
string(FIND "${_bridge}" "if (g_registered.load" _registered_first_position)
string(FIND "${_bridge}" "if (g_installAttempted.exchange" _retry_latch_position)
string(FIND "${_bridge}" "GetDevBenchInterface001" _host_probe_position)
if(_registered_first_position EQUAL -1 OR _retry_latch_position EQUAL -1 OR _host_probe_position EQUAL -1 OR
   _retry_latch_position LESS _host_probe_position)
    message(FATAL_ERROR "Screenshot bridge must preserve its PostPostLoad retry when DevBench is absent at PostLoad")
endif()

file(READ "${PROJECT_ROOT}/src/XSEPlugin.cpp" _plugin_lifecycle)
string(FIND "${_plugin_lifecycle}" "case SKSE::MessagingInterface::kPostLoad:" _postload_position)
string(FIND "${_plugin_lifecycle}" "ScreenshotDevBenchBridge::Install();" _early_install_position)
if(_postload_position EQUAL -1 OR _early_install_position LESS _postload_position)
    message(FATAL_ERROR "Screenshot DevBench discovery must be attempted during PostLoad")
endif()

message(STATUS "Screenshot API contract, schemas, goldens, migration, actions, and journal events are coherent")
