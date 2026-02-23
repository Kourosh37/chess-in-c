if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "chess_linux_release packaging is only supported on Linux.")
endif()

if(NOT DEFINED CHESS_APP_EXE OR CHESS_APP_EXE STREQUAL "")
    message(FATAL_ERROR "CHESS_APP_EXE is required.")
endif()
if(NOT EXISTS "${CHESS_APP_EXE}")
    message(FATAL_ERROR "CHESS_APP_EXE does not exist: ${CHESS_APP_EXE}")
endif()

if(NOT DEFINED CHESS_SOURCE_ASSETS OR CHESS_SOURCE_ASSETS STREQUAL "")
    message(FATAL_ERROR "CHESS_SOURCE_ASSETS is required.")
endif()
if(NOT EXISTS "${CHESS_SOURCE_ASSETS}")
    message(FATAL_ERROR "assets directory was not found: ${CHESS_SOURCE_ASSETS}")
endif()

if(NOT DEFINED CHESS_OUT_DIR OR CHESS_OUT_DIR STREQUAL "")
    message(FATAL_ERROR "CHESS_OUT_DIR is required.")
endif()

if(NOT DEFINED CHESS_RELEASE_ARCH OR CHESS_RELEASE_ARCH STREQUAL "")
    set(CHESS_RELEASE_ARCH "x64")
endif()

set(_version "${CHESS_RELEASE_VERSION}")
if(DEFINED _version)
    string(STRIP "${_version}" _version)
endif()

string(REGEX REPLACE "[^0-9A-Za-z._-]" "_" _arch_safe "${CHESS_RELEASE_ARCH}")
if(_arch_safe STREQUAL "")
    set(_arch_safe "x64")
endif()

set(_release_prefix "chess-linux-${_arch_safe}")
if(NOT _version STREQUAL "")
    string(REGEX REPLACE "[^0-9A-Za-z._-]" "_" _version_safe "${_version}")
    set(_versioned_prefix "${_release_prefix}-v${_version_safe}")
else()
    set(_versioned_prefix "${_release_prefix}")
endif()

set(_release_dir "${CHESS_OUT_DIR}")
set(_stage_dir "${_release_dir}/_linux_stage")
set(_stage_chess_dir "${_stage_dir}/chess")
set(_tar_file "${_release_dir}/${_versioned_prefix}.tar.gz")
set(_run_file "${_release_dir}/${_versioned_prefix}.run")
set(_tar_sha_file "${_tar_file}.sha256")
set(_run_sha_file "${_run_file}.sha256")

set(_latest_tar_file "${_release_dir}/${_release_prefix}.tar.gz")
set(_latest_run_file "${_release_dir}/${_release_prefix}.run")
set(_latest_tar_sha_file "${_latest_tar_file}.sha256")
set(_latest_run_sha_file "${_latest_run_file}.sha256")

file(REMOVE_RECURSE "${_stage_dir}")
file(MAKE_DIRECTORY "${_release_dir}")
file(MAKE_DIRECTORY "${_stage_chess_dir}")

file(COPY "${CHESS_APP_EXE}" DESTINATION "${_stage_chess_dir}")
file(COPY "${CHESS_SOURCE_ASSETS}" DESTINATION "${_stage_chess_dir}")

set(_launcher_file "${_stage_chess_dir}/run.sh")
file(WRITE "${_launcher_file}" [=[
#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"
exec ./chess_app "$@"
]=])

file(CHMOD "${_stage_chess_dir}/chess_app"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
file(CHMOD "${_launcher_file}"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)

find_program(CHESS_TAR_EXE NAMES tar REQUIRED)

execute_process(
    COMMAND "${CHESS_TAR_EXE}" -czf "${_tar_file}" chess
    WORKING_DIRECTORY "${_stage_dir}"
    RESULT_VARIABLE _tar_result
    OUTPUT_VARIABLE _tar_stdout
    ERROR_VARIABLE _tar_stderr
)

if(NOT _tar_result EQUAL 0)
    message(FATAL_ERROR
        "tar failed while creating Linux archive.\n"
        "stdout:\n${_tar_stdout}\n"
        "stderr:\n${_tar_stderr}")
endif()

set(_run_stub_file "${_release_dir}/_linux_run_stub.sh")
file(WRITE "${_run_stub_file}" [=[
#!/bin/sh
set -eu

if [ "${1:-}" = "--help" ]; then
    echo "Usage: $0 [target_directory]"
    echo "Extracts chess bundle into target_directory/chess (default: current directory)."
    exit 0
fi

DEST_DIR="${1:-$(pwd)}"
mkdir -p "$DEST_DIR"

PAYLOAD_LINE=$(awk '/^__CHESS_PAYLOAD_BELOW__$/ { print NR + 1; exit 0; }' "$0")
if [ -z "${PAYLOAD_LINE:-}" ]; then
    echo "Payload marker not found." >&2
    exit 1
fi

tail -n +"$PAYLOAD_LINE" "$0" | tar -xz -C "$DEST_DIR"
echo "Extracted to: $DEST_DIR/chess"
echo "Run with: $DEST_DIR/chess/chess_app"
exit 0
__CHESS_PAYLOAD_BELOW__
]=])

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E cat "${_run_stub_file}" "${_tar_file}"
    OUTPUT_FILE "${_run_file}"
    RESULT_VARIABLE _cat_result
    ERROR_VARIABLE _cat_stderr
)

if(NOT _cat_result EQUAL 0 OR NOT EXISTS "${_run_file}")
    message(FATAL_ERROR
        "Failed to create Linux .run package.\n"
        "stderr:\n${_cat_stderr}")
endif()

file(CHMOD "${_run_file}"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)

file(SHA256 "${_run_file}" _run_sha256)
file(SHA256 "${_tar_file}" _tar_sha256)

get_filename_component(_run_name "${_run_file}" NAME)
get_filename_component(_tar_name "${_tar_file}" NAME)
file(WRITE "${_run_sha_file}" "${_run_sha256} *${_run_name}\n")
file(WRITE "${_tar_sha_file}" "${_tar_sha256} *${_tar_name}\n")

if(NOT _run_file STREQUAL _latest_run_file)
    file(COPY_FILE "${_run_file}" "${_latest_run_file}" ONLY_IF_DIFFERENT)
    get_filename_component(_latest_run_name "${_latest_run_file}" NAME)
    file(WRITE "${_latest_run_sha_file}" "${_run_sha256} *${_latest_run_name}\n")
endif()
if(NOT _tar_file STREQUAL _latest_tar_file)
    file(COPY_FILE "${_tar_file}" "${_latest_tar_file}" ONLY_IF_DIFFERENT)
    get_filename_component(_latest_tar_name "${_latest_tar_file}" NAME)
    file(WRITE "${_latest_tar_sha_file}" "${_tar_sha256} *${_latest_tar_name}\n")
endif()

file(REMOVE_RECURSE "${_stage_dir}")
file(REMOVE "${_run_stub_file}")

message(STATUS
    "Linux release packages created:\n"
    "  ${_run_file}\n"
    "  ${_run_sha_file}\n"
    "  ${_tar_file}\n"
    "  ${_tar_sha_file}")
