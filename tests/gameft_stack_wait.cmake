add_controller_test(devbench_cpu_snapshot_test DevBenchCpuSnapshot tests/devbench_cpu_snapshot_test.cpp)
target_compile_definitions(
    devbench_cpu_snapshot_test
    PRIVATE DEVBENCH_BRIDGE_ENABLED
)
target_include_directories(
    devbench_cpu_snapshot_test
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(
    devbench_cpu_snapshot_test
    PRIVATE nlohmann_json::nlohmann_json
)
target_sources(
    devbench_cpu_snapshot_test
    PRIVATE src/Api/AcceptedDrawRegistry.cpp
)
add_controller_test(devbench_cpu_snapshot_off_test DevBenchCpuSnapshotOff tests/devbench_cpu_snapshot_off_test.cpp)
target_sources(
    devbench_cpu_snapshot_off_test
    PRIVATE src/ProfilerDevBenchBridge.cpp src/Api/AcceptedDrawRegistry.cpp
)
target_include_directories(
    devbench_cpu_snapshot_off_test
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
add_test(
    NAME DevBenchCpuSnapshotContract
    COMMAND
        pwsh -NoProfile -File
        "${PROJECT_SOURCE_DIR}/tests/devbench_cpu_snapshot_contract_test.ps1"
)
