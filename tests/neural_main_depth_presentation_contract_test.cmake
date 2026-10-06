file(READ "${PROJECT_ROOT}/src/Features/Upscaling.cpp" _source)
string(FIND "${_source}" "void Upscaling::UpscaleDepth()" _start)
string(SUBSTRING "${_source}" ${_start} -1 _tail)
string(FIND "${_tail}" "\n}\n" _length)
string(SUBSTRING "${_tail}" 0 ${_length} _upscale)
string(FIND "${_upscale}" "mainFinalLdrDepthProof = {};" _reset)
string(FIND "${_upscale}" "const bool depthUpscaleActive" _validation)
if(_reset LESS 0 OR _validation LESS _reset)
    message(FATAL_ERROR "Depth producer must invalidate proof before any early exit")
endif()
if(NOT _upscale MATCHES "context->PSSetShader\\(depthUpscalePS, nullptr, 0\\);[\n\r\t ]+context->Draw\\(3, 0\\);[\n\r\t ]+if \\(neuralDepthProofRequested\\)[\n\r\t ]+mainFinalLdrDepthProof = \\{ state->frameCount, GetCOMIdentityAddress\\(depth.texture\\) \\};")
    message(FATAL_ERROR "Depth proof must follow the actual depth reconstruction draw")
endif()
string(FIND "${_source}" "void Upscaling::FinalizeMainFinalLdrNeuralPresentation()" _start)
string(SUBSTRING "${_source}" ${_start} -1 _tail)
string(FIND "${_tail}" "\n}\n" _length)
string(SUBSTRING "${_tail}" 0 ${_length} _finalize)
foreach(_required IN ITEMS
    "mainFinalLdrDepthProof.SupportsOutputLayout("
    "pending.frame, GetCOMIdentityAddress(depth.texture)"
    "depthDesc.Width != outputLayout.width"
    "depthDesc.Height != outputLayout.height"
    "depth.depthSRV, pending.outputWidthPerEye,"
    "pending.outputHeight, outputLayout.eyes[eye].minX,")
    string(FIND "${_finalize}" "${_required}" _found)
    if(_found LESS 0)
        message(FATAL_ERROR "Final depth repair is missing ${_required}")
    endif()
endforeach()
if(_finalize MATCHES "inputLayout|depth.depthSRV[^,]*, pending.inputWidthPerEye")
    message(FATAL_ERROR "Final depth repair must not use the pre-upscale input grid")
endif()
