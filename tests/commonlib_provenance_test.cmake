cmake_minimum_required(VERSION 4.2)

foreach(
    _required_variable
    IN ITEMS
        PROVENANCE_UPDATER
        PROVENANCE_HEADER_TEMPLATE
        PROVENANCE_RESOURCE_TEMPLATE
        TEST_GIT_EXECUTABLE
        TEST_PARENT_ROOT
        TEST_ROOT
)
    if(NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        message(FATAL_ERROR "Missing required test variable ${_required_variable}.")
    endif()
endforeach()

cmake_path(
    ABSOLUTE_PATH TEST_PARENT_ROOT
    NORMALIZE
    OUTPUT_VARIABLE _normalized_test_parent
)
cmake_path(
    ABSOLUTE_PATH TEST_ROOT
    BASE_DIRECTORY "${_normalized_test_parent}"
    NORMALIZE
    OUTPUT_VARIABLE _normalized_test_root
)
if(WIN32)
    string(TOLOWER "${_normalized_test_parent}" _normalized_test_parent)
    string(TOLOWER "${_normalized_test_root}" _normalized_test_root)
endif()
cmake_path(
    IS_PREFIX _normalized_test_parent
    "${_normalized_test_root}"
    NORMALIZE
    _test_root_is_descendant
)
if(
    NOT _test_root_is_descendant
    OR _normalized_test_root STREQUAL _normalized_test_parent
)
    message(
        FATAL_ERROR
        "TEST_ROOT must be a strict descendant of TEST_PARENT_ROOT."
    )
endif()
set(TEST_ROOT "${_normalized_test_root}")

function(run_git working_directory)
    execute_process(
        COMMAND
            "${TEST_GIT_EXECUTABLE}" -c user.name=CSX-Test
            -c user.email=csx-test@example.invalid -c commit.gpgsign=false
            ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE _git_result
        OUTPUT_VARIABLE _git_output
        ERROR_VARIABLE _git_error
    )
    if(NOT _git_result EQUAL 0)
        message(
            FATAL_ERROR
            "Git command failed in ${working_directory}: git ${ARGN}\n${_git_output}${_git_error}"
        )
    endif()
endfunction()

function(read_git_revision working_directory output_variable)
    execute_process(
        COMMAND "${TEST_GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE _git_result
        OUTPUT_VARIABLE _git_output
        ERROR_VARIABLE _git_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _git_result EQUAL 0)
        message(FATAL_ERROR "Could not read test revision: ${_git_error}")
    endif()
    set(${output_variable} "${_git_output}" PARENT_SCOPE)
endfunction()

function(assert_file_contains file_path expected_text)
    file(READ "${file_path}" _file_content)
    string(FIND "${_file_content}" "${expected_text}" _match_index)
    if(_match_index EQUAL -1)
        message(
            FATAL_ERROR
            "${file_path} does not contain expected text:\n${expected_text}\nActual content:\n${_file_content}"
        )
    endif()
endfunction()

function(assert_file_excludes file_path rejected_text)
    file(READ "${file_path}" _file_content)
    string(FIND "${_file_content}" "${rejected_text}" _match_index)
    if(NOT _match_index EQUAL -1)
        message(
            FATAL_ERROR
            "${file_path} unexpectedly contains ${rejected_text}."
        )
    endif()
endfunction()

function(
    run_provenance
    label
    source_root
    commonlib_root
    policy
    linkage
    archive_revision
    expect_success
)
    set(_output_directory "${TEST_ROOT}/generated/${label}")
    set(_header_output "${_output_directory}/CommonLibProvenance.h")
    set(_resource_output "${_output_directory}/CommonLibProvenance.rc")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}"
            "-DCSX_SOURCE_ROOT:PATH=${source_root}"
            "-DCSX_COMMONLIB_SOURCE_DIR:PATH=${commonlib_root}"
            "-DCSX_COMMONLIB_RELATIVE_PATH:STRING=extern/CommonLibSSE-NG"
            "-DCSX_COMMONLIB_PROVENANCE_POLICY:STRING=${policy}"
            "-DCSX_COMMONLIB_ARCHIVE_REVISION:STRING=${archive_revision}"
            "-DCSX_COMMONLIB_LINKAGE:STRING=${linkage}"
            "-DCSX_GIT_EXECUTABLE:FILEPATH=${TEST_GIT_EXECUTABLE}"
            "-DCSX_PROVENANCE_HEADER_TEMPLATE:FILEPATH=${PROVENANCE_HEADER_TEMPLATE}"
            "-DCSX_PROVENANCE_RESOURCE_TEMPLATE:FILEPATH=${PROVENANCE_RESOURCE_TEMPLATE}"
            "-DCSX_PROVENANCE_HEADER_OUTPUT:FILEPATH=${_header_output}"
            "-DCSX_PROVENANCE_RESOURCE_OUTPUT:FILEPATH=${_resource_output}"
            -P "${PROVENANCE_UPDATER}"
        RESULT_VARIABLE _provenance_result
        OUTPUT_VARIABLE _provenance_output
        ERROR_VARIABLE _provenance_error
    )
    if(expect_success AND NOT _provenance_result EQUAL 0)
        message(
            FATAL_ERROR
            "Provenance case ${label} unexpectedly failed:\n${_provenance_output}${_provenance_error}"
        )
    elseif(NOT expect_success AND _provenance_result EQUAL 0)
        message(FATAL_ERROR "Provenance case ${label} unexpectedly succeeded.")
    endif()
    if(expect_success)
        set(LAST_PROVENANCE_HEADER "${_header_output}" PARENT_SCOPE)
        set(LAST_PROVENANCE_RESOURCE "${_resource_output}" PARENT_SCOPE)
    endif()
endfunction()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

set(_superproject "${TEST_ROOT}/superproject")
set(_commonlib "${_superproject}/extern/CommonLibSSE-NG")
file(MAKE_DIRECTORY "${_commonlib}")
run_git("${_commonlib}" init --quiet)
file(WRITE "${_commonlib}/tracked.txt" "initial CommonLib fixture\n")
run_git("${_commonlib}" add tracked.txt)
run_git("${_commonlib}" commit --quiet -m "initial CommonLib fixture")

run_git("${_superproject}" init --quiet)
file(WRITE "${_superproject}/tracked.txt" "initial superproject fixture\n")
run_git("${_superproject}" add tracked.txt extern/CommonLibSSE-NG)
run_git("${_superproject}" commit --quiet -m "pin CommonLib fixture")
read_git_revision("${_commonlib}" _commonlib_revision)

run_provenance(
    clean
    "${_superproject}"
    "${_commonlib}"
    STRICT
    source
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_EXPECTED_REVISION = \"${_commonlib_revision}\""
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_CHECKOUT_REVISION = \"${_commonlib_revision}\""
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"clean-match\""
)
assert_file_contains(
    "${LAST_PROVENANCE_RESOURCE}"
    "#define CSX_COMMONLIB_EXPECTED_REVISION \"${_commonlib_revision}\""
)
assert_file_contains(
    "${LAST_PROVENANCE_RESOURCE}"
    "#define CSX_COMMONLIB_CHECKOUT_REVISION \"${_commonlib_revision}\""
)
assert_file_contains(
    "${LAST_PROVENANCE_RESOURCE}"
    "#define CSX_COMMONLIB_CHECKOUT_STATE \"clean\""
)
assert_file_contains(
    "${LAST_PROVENANCE_RESOURCE}"
    "#define CSX_COMMONLIB_PROVENANCE_STATUS \"clean-match\""
)
assert_file_contains(
    "${LAST_PROVENANCE_RESOURCE}"
    "#define CSX_COMMONLIB_LINKAGE \"source\""
)
assert_file_excludes("${LAST_PROVENANCE_HEADER}" "@CSX_")
assert_file_excludes("${LAST_PROVENANCE_RESOURCE}" "@CSX_")

file(TIMESTAMP "${LAST_PROVENANCE_HEADER}" _timestamp_before "%s" UTC)
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
run_provenance(
    clean
    "${_superproject}"
    "${_commonlib}"
    STRICT
    source
    ""
    TRUE
)
file(TIMESTAMP "${LAST_PROVENANCE_HEADER}" _timestamp_after "%s" UTC)
if(NOT _timestamp_before STREQUAL _timestamp_after)
    message(FATAL_ERROR "Unchanged provenance rewrote its generated header.")
endif()

file(APPEND "${_commonlib}/tracked.txt" "tracked dirty state\n")
run_provenance(
    clean
    "${_superproject}"
    "${_commonlib}"
    WARN
    source
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"dirty-match\""
)
run_provenance(
    tracked-dirty-strict
    "${_superproject}"
    "${_commonlib}"
    STRICT
    source
    ""
    FALSE
)
run_git("${_commonlib}" restore --worktree tracked.txt)
run_provenance(
    clean
    "${_superproject}"
    "${_commonlib}"
    STRICT
    source
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"clean-match\""
)

file(WRITE "${_commonlib}/untracked.txt" "untracked dirty state\n")
run_provenance(
    untracked-dirty
    "${_superproject}"
    "${_commonlib}"
    WARN
    source
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"dirty-match\""
)
file(REMOVE "${_commonlib}/untracked.txt")

set(_uninitialized_superproject "${TEST_ROOT}/uninitialized-superproject")
set(
    _uninitialized_commonlib
    "${_uninitialized_superproject}/extern/CommonLibSSE-NG"
)
file(MAKE_DIRECTORY "${_uninitialized_commonlib}")
run_git("${_uninitialized_superproject}" init --quiet)
file(WRITE "${_uninitialized_superproject}/tracked.txt" "uninitialized fixture\n")
run_git("${_uninitialized_superproject}" add tracked.txt)
run_git(
    "${_uninitialized_superproject}"
    update-index --add --cacheinfo
    "160000,${_commonlib_revision},extern/CommonLibSSE-NG"
)
run_git(
    "${_uninitialized_superproject}"
    commit --quiet -m "pin uninitialized CommonLib fixture"
)
run_provenance(
    uninitialized-commonlib
    "${_uninitialized_superproject}"
    "${_uninitialized_commonlib}"
    WARN
    source
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_CHECKOUT_REVISION = \"unavailable\""
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"unverified\""
)
run_provenance(
    uninitialized-commonlib-strict
    "${_uninitialized_superproject}"
    "${_uninitialized_commonlib}"
    STRICT
    source
    ""
    FALSE
)

set(_openvr "${_commonlib}/extern/openvr")
file(MAKE_DIRECTORY "${_openvr}")
run_git("${_openvr}" init --quiet)
file(WRITE "${_openvr}/tracked.txt" "initial nested fixture\n")
run_git("${_openvr}" add tracked.txt)
run_git("${_openvr}" commit --quiet -m "initial nested fixture")
run_git("${_commonlib}" add extern/openvr)
run_git("${_commonlib}" commit --quiet -m "pin nested fixture")
run_git("${_superproject}" add extern/CommonLibSSE-NG)
run_git("${_superproject}" commit --quiet -m "update CommonLib fixture")

file(APPEND "${_openvr}/tracked.txt" "new nested revision\n")
run_git("${_openvr}" add tracked.txt)
run_git("${_openvr}" commit --quiet -m "advance nested fixture")
run_provenance(
    nested-dirty
    "${_superproject}"
    "${_commonlib}"
    WARN
    source
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"dirty-match\""
)
execute_process(
    COMMAND
        "${TEST_GIT_EXECUTABLE}" -C "${_commonlib}" ls-tree HEAD -- extern/openvr
    OUTPUT_VARIABLE _nested_entry
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
if(NOT _nested_entry MATCHES "^160000[ \t]+commit[ \t]+([0-9A-Fa-f]+)[ \t]")
    message(FATAL_ERROR "Could not parse nested fixture gitlink.")
endif()
set(_expected_openvr_revision "${CMAKE_MATCH_1}")
run_git("${_openvr}" switch --quiet --detach "${_expected_openvr_revision}")

file(APPEND "${_commonlib}/tracked.txt" "new CommonLib revision\n")
run_git("${_commonlib}" add tracked.txt)
run_git("${_commonlib}" commit --quiet -m "advance CommonLib fixture")
read_git_revision("${_commonlib}" _advanced_commonlib_revision)
run_provenance(
    clean-mismatch
    "${_superproject}"
    "${_commonlib}"
    WARN
    source
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_CHECKOUT_REVISION = \"${_advanced_commonlib_revision}\""
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"clean-mismatch\""
)
run_provenance(
    clean-mismatch-strict
    "${_superproject}"
    "${_commonlib}"
    STRICT
    source
    ""
    FALSE
)

file(WRITE "${_commonlib}/mismatch-untracked.txt" "dirty mismatch\n")
run_provenance(
    dirty-mismatch
    "${_superproject}"
    "${_commonlib}"
    WARN
    source
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"dirty-mismatch\""
)
file(REMOVE "${_commonlib}/mismatch-untracked.txt")

run_git("${_superproject}" add extern/CommonLibSSE-NG)
run_git("${_superproject}" commit --quiet -m "repin advanced CommonLib fixture")
run_provenance(
    prebuilt
    "${_superproject}"
    "${_commonlib}"
    WARN
    prebuilt
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"prebuilt-unverified\""
)
run_provenance(
    prebuilt-strict
    "${_superproject}"
    "${_commonlib}"
    STRICT
    prebuilt
    ""
    FALSE
)

set(_linked_superproject "${TEST_ROOT}/linked-superproject")
run_git("${_superproject}" worktree add --quiet --detach "${_linked_superproject}" HEAD)
file(REMOVE_RECURSE "${_linked_superproject}/extern/CommonLibSSE-NG")
file(MAKE_DIRECTORY "${_linked_superproject}/extern")
set(_linked_commonlib "${_linked_superproject}/extern/CommonLibSSE-NG")
run_git("${_commonlib}" worktree add --quiet --detach "${_linked_commonlib}" HEAD)
run_provenance(
    linked-worktrees
    "${_linked_superproject}"
    "${_linked_commonlib}"
    STRICT
    source
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"clean-match\""
)

set(_archive_parent "${TEST_ROOT}/archive-parent")
set(_archive_root "${_archive_parent}/source")
set(_archive_commonlib "${_archive_root}/extern/CommonLibSSE-NG")
file(MAKE_DIRECTORY "${_archive_commonlib}")
run_git("${_archive_parent}" init --quiet)
file(WRITE "${_archive_parent}/tracked.txt" "unrelated parent repository\n")
run_git("${_archive_parent}" add tracked.txt)
run_git("${_archive_parent}" commit --quiet -m "unrelated parent fixture")
run_provenance(
    parent-repository-rejected
    "${_archive_root}"
    "${_archive_commonlib}"
    WARN
    source
    ""
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_EXPECTED_REVISION = \"unavailable\""
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_CHECKOUT_REVISION = \"unavailable\""
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"unverified\""
)
run_provenance(
    archive-unverified-strict
    "${_archive_root}"
    "${_archive_commonlib}"
    STRICT
    source
    ""
    FALSE
)
run_provenance(
    archive-declared
    "${_archive_root}"
    "${_archive_commonlib}"
    WARN
    source
    "${_advanced_commonlib_revision}"
    TRUE
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_CHECKOUT_REVISION = \"${_advanced_commonlib_revision}\""
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_CHECKOUT_STATE = \"declared\""
)
assert_file_contains(
    "${LAST_PROVENANCE_HEADER}"
    "COMMONLIB_PROVENANCE_STATUS = \"archive-declared\""
)
run_provenance(
    archive-declared-strict
    "${_archive_root}"
    "${_archive_commonlib}"
    STRICT
    source
    "${_advanced_commonlib_revision}"
    FALSE
)
run_provenance(
    archive-invalid-revision
    "${_archive_root}"
    "${_archive_commonlib}"
    WARN
    source
    not-a-revision
    FALSE
)
run_provenance(
    archive-override-with-git
    "${_superproject}"
    "${_commonlib}"
    WARN
    source
    "${_advanced_commonlib_revision}"
    FALSE
)

message(STATUS "CommonLib provenance regression cases passed.")
