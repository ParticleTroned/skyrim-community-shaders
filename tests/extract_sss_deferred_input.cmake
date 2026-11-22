if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "PROJECT_ROOT and OUTPUT_DIRECTORY are required")
endif()

function(extract_between file start end output)
    file(READ "${PROJECT_ROOT}/${file}" source)
    string(FIND "${source}" "${start}" first)
    if(first EQUAL -1)
        message(FATAL_ERROR "Cannot find ${start} in ${file}")
    endif()
    string(SUBSTRING "${source}" ${first} -1 remaining)
    string(FIND "${remaining}" "${end}" last)
    if(last EQUAL -1)
        message(FATAL_ERROR "Cannot find ${end} in ${file}")
    endif()
    string(SUBSTRING "${remaining}" 0 ${last} extracted)
    set(${output} "${extracted}" PARENT_SCOPE)
endfunction()

# Exercise the real allocation/upload methods without the game-facing renderer.
extract_between("src/Deferred.h" "\tstd::unique_ptr<ConstantBuffer> diffuseInputCB;" "\tID3D11ComputeShader* GetComputeMainComposite(bool" members)
extract_between("src/Deferred.cpp" "void Deferred::SetupDiffuseInputBuffer()" "void Deferred::SetupResources()" methods)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_DIRECTORY}/sss_deferred_upload_under_test.h"
    "#pragma once\nstruct Deferred {\n${members}};\n${methods}"
)
