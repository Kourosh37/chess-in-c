cmake_minimum_required(VERSION 3.21)

if(CHESS_RUNTIME_STAGE)
    # Leave SystemRoot pointing at the real OS; WINDIR models an additional
    # system directory, including spaces and regex metacharacters in its path.
    set(ENV{WINDIR} "${CHESS_TEST_ROOT}/system [fixture]")
    set(CHESS_COMPILER "${CHESS_TEST_ROOT}/system [fixture]/System32/compiler.exe")
    set(CHESS_SOURCE_DIR "${CHESS_TEST_ROOT}/application")
    include("${CMAKE_CURRENT_LIST_DIR}/../cmake/stage_windows_runtime.cmake")
    chess_stage_windows_runtime("${CHESS_TEST_EXE}" "${CHESS_OUT_DIR}")
    return()
endif()

if(NOT WIN32 OR NOT IS_ABSOLUTE "${CHESS_TEST_ROOT}" OR
   NOT EXISTS "${CHESS_TEST_EXE}" OR NOT EXISTS "${CHESS_TEST_SYSTEM_DLL}" OR
   NOT EXISTS "${CHESS_TEST_APP_DLL}")
    message(FATAL_ERROR "Windows runtime tests require built PE fixtures and an absolute test root.")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _id)
set(_root "${CHESS_TEST_ROOT}/${_id}")
file(MAKE_DIRECTORY "${_root}/application" "${_root}/system [fixture]/System32")
file(COPY "${CHESS_TEST_EXE}" "${CHESS_TEST_APP_DLL}" DESTINATION "${_root}/application")
file(COPY "${CHESS_TEST_SYSTEM_DLL}" DESTINATION "${_root}/system [fixture]/System32")
get_filename_component(_exe_name "${CHESS_TEST_EXE}" NAME)
get_filename_component(_app_name "${CHESS_TEST_APP_DLL}" NAME)
get_filename_component(_system_name "${CHESS_TEST_SYSTEM_DLL}" NAME)
# The optional DLL imported by the fake system DLL is deliberately absent.

function(stage name expect_success)
    file(MAKE_DIRECTORY "${_root}/${name}")
    execute_process(COMMAND "${CMAKE_COMMAND}"
        -DCHESS_RUNTIME_STAGE=ON
        "-DCHESS_TEST_ROOT=${_root}"
        "-DCHESS_TEST_EXE=${_root}/application/${_exe_name}"
        "-DCHESS_OUT_DIR=${_root}/${name}"
        "-DCHESS_COMPILER_ID=${CHESS_COMPILER_ID}"
        "-DCHESS_OBJDUMP=${CHESS_OBJDUMP}"
        -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(expect_success AND NOT _result EQUAL 0)
        message(FATAL_ERROR "System DLL traversal was not excluded:\n${_out}\n${_err}\nFixtures: ${_root}")
    elseif(NOT expect_success AND (_result EQUAL 0 OR NOT _err MATCHES "Unresolved DLLs:.*${_app_name}"))
        message(FATAL_ERROR "Missing application DLL was not rejected:\n${_out}\n${_err}")
    endif()
endfunction()

stage(success TRUE)
if(NOT EXISTS "${_root}/success/${_app_name}" OR EXISTS "${_root}/success/${_system_name}")
    message(FATAL_ERROR "Staging must include the application DLL and exclude the system DLL.")
endif()
file(READ "${_root}/success/runtime_manifest.txt" _manifest)
if(NOT _manifest MATCHES "${_app_name}" OR _manifest MATCHES "${_system_name}")
    message(FATAL_ERROR "Runtime manifest contains the wrong dependencies.")
endif()
file(RENAME "${_root}/application/${_app_name}" "${_root}/application/${_app_name}.hidden")
stage(missing-application FALSE)
message(STATUS "System DLLs were excluded before recursion; application DLLs were staged and missing ones rejected.")
