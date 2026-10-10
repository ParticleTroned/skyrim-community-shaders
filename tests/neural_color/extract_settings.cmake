# Compile actual parser, transactions and serializers without game/UI dependencies.
file(READ "${PROJECT_ROOT}/src/Features/NeuralRenderingFeature.cpp" _colour)
file(READ "${PROJECT_ROOT}/src/Features/Upscaling/NeuralRendering/ColorPipeline.cpp" _pipeline)
function(extract source start end output)
    string(FIND "${${source}}" "${start}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "NR settings test cannot find ${start}")
    endif()
    string(SUBSTRING "${${source}}" ${_start} -1 _remaining)
    string(FIND "${_remaining}" "${end}" _end)
    if(_end EQUAL -1)
        message(FATAL_ERROR "NR settings test cannot find ${end}")
    endif()
    string(SUBSTRING "${_remaining}" 0 ${_end} _region)
    set(${output} "${_region}" PARENT_SCOPE)
endfunction()
extract(_colour "namespace\n{" "\tJson CaptureJson()" _helpers)
extract(_colour "\tJson ObservationJson(" "\tJson StatusJson()" _observations)
extract(_colour "\tvoid Handler(" "\tJson Descriptor()" _handler)
extract(_colour "namespace NeuralRendering::Color\n{" "\nNeuralRenderingFeature& NeuralRenderingFeature::Instance()" _serializers)
extract(_colour "void NeuralRenderingFeature::LoadSettings(" "void NeuralRenderingFeature::EarlyPrepass()" _persistence)
extract(_pipeline "\tRegistry& Registry::Instance()" "\tStatus Registry::GetStatus()" _snapshot)
extract(_pipeline "\tbool Registry::Configure(" "\tMeasurementBatchHistory<Measurement>::Lease Registry::PinMeasurementBatch" _configure)
extract(_pipeline "\t\twork.observation = std::move(observation);" "\t\tstd::uint64_t pixelBytes" _latch)
file(WRITE "${OUTPUT_DIRECTORY}/settings_under_test.h"
    "namespace NeuralRendering::Color {\n${_snapshot}\n${_configure}\n}\n${_helpers}\n${_observations}\n"
    "Json StatusJson() { auto c = Registry::Instance().Snapshot(); return {{\"ok\", true}, {\"settings\", SettingsJson(c.settings)}, {\"revision\", c.revision}}; }\n"
    "Json AssetsJson() { return {}; }\n${_handler}\n}\n${_serializers}\n${_persistence}\n"
    "void Latch(Work& work, const Configuration& config, Observation observation) { uint64_t measurementOrder_ = 0;\n${_latch}\n}\n")
