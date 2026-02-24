if(NOT WIN32)
    message(FATAL_ERROR "Windows installer packaging is only supported on Windows.")
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

if(NOT DEFINED CHESS_SOURCE_DIR OR CHESS_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "CHESS_SOURCE_DIR is required.")
endif()
if(NOT EXISTS "${CHESS_SOURCE_DIR}")
    message(FATAL_ERROR "CHESS_SOURCE_DIR does not exist: ${CHESS_SOURCE_DIR}")
endif()

if(NOT DEFINED CHESS_OUT_DIR OR CHESS_OUT_DIR STREQUAL "")
    message(FATAL_ERROR "CHESS_OUT_DIR is required.")
endif()

if(NOT DEFINED CHESS_ISCC_EXE OR CHESS_ISCC_EXE STREQUAL "" OR CHESS_ISCC_EXE MATCHES "-NOTFOUND$")
    find_program(CHESS_ISCC_EXE
        NAMES iscc iscc.exe
        HINTS
            "$ENV{ProgramW6432}/Inno Setup 6"
            "$ENV{ProgramFiles}/Inno Setup 6"
            "C:/Program Files (x86)/Inno Setup 6"
    )
endif()

if(NOT CHESS_ISCC_EXE OR CHESS_ISCC_EXE MATCHES "-NOTFOUND$" OR NOT EXISTS "${CHESS_ISCC_EXE}")
    message(FATAL_ERROR
        "iscc.exe (Inno Setup) was not found.\n"
        "Install Inno Setup 6 and retry (example: choco install innosetup -y).")
endif()

get_filename_component(_app_name "${CHESS_APP_EXE}" NAME)
set(_release_dir "${CHESS_OUT_DIR}")
set(_stage_dir "${_release_dir}/_installer_stage")
set(_stage_chess_dir "${_stage_dir}/chess")
set(_iss_file "${_stage_dir}/chess_installer.iss")
set(_version "${CHESS_RELEASE_VERSION}")

if(DEFINED _version)
    string(STRIP "${_version}" _version)
endif()
if(_version STREQUAL "")
    set(_version "dev")
endif()
string(REGEX REPLACE "[^0-9A-Za-z._-]" "_" _version_safe "${_version}")

set(_output_base "chess-windows-x64-v${_version_safe}")
set(_output_file "${_release_dir}/${_output_base}.exe")
set(_sha256_file "${_output_file}.sha256")
set(_latest_output_file "${_release_dir}/chess.exe")
set(_latest_sha256_file "${_latest_output_file}.sha256")

file(REMOVE_RECURSE "${_stage_dir}")
file(MAKE_DIRECTORY "${_release_dir}")
file(MAKE_DIRECTORY "${_stage_chess_dir}")

file(COPY "${CHESS_APP_EXE}" DESTINATION "${_stage_chess_dir}")
file(COPY "${CHESS_SOURCE_ASSETS}" DESTINATION "${_stage_chess_dir}")

# Always include user-provided project DLLs next to chess_app.exe.
# Put optional native dependencies in repository root or runtime/windows/.
file(GLOB _project_runtime_dlls
    "${CHESS_SOURCE_DIR}/*.dll"
    "${CHESS_SOURCE_DIR}/runtime/windows/*.dll"
)
foreach(_dll IN LISTS _project_runtime_dlls)
    if(EXISTS "${_dll}")
        file(COPY "${_dll}" DESTINATION "${_stage_chess_dir}")
    endif()
endforeach()

# For GNU toolchains, include non-system runtime DLLs.
# For MSVC release builds we intentionally avoid dependency scanning to prevent
# bundling Windows system DLLs, which can break startup on user machines.
set(_stage_app_exe "${_stage_chess_dir}/${_app_name}")
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES "${_stage_app_exe}"
        RESOLVED_DEPENDENCIES_VAR _runtime_deps
        UNRESOLVED_DEPENDENCIES_VAR _runtime_unresolved
        PRE_EXCLUDE_REGEXES
            "api-ms-.*"
            "ext-ms-.*"
    )

    foreach(_dep IN LISTS _runtime_deps)
        if(NOT EXISTS "${_dep}")
            continue()
        endif()

        file(TO_CMAKE_PATH "${_dep}" _dep_norm)
        string(TOLOWER "${_dep_norm}" _dep_norm_lower)

        if(_dep_norm_lower MATCHES "^[a-z]:/windows/(system32|syswow64|winsxs)/")
            continue()
        endif()

        file(COPY "${_dep}" DESTINATION "${_stage_chess_dir}")
    endforeach()

    if(_runtime_unresolved)
        message(STATUS "Unresolved runtime dependencies (usually system-provided): ${_runtime_unresolved}")
    endif()
endif()

# Extra safety for MinGW builds: bundle runtime DLLs from the active compiler directory.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    get_filename_component(_c_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    foreach(_rt_name IN ITEMS
        libgcc_s_seh-1.dll
        libgcc_s_dw2-1.dll
        libwinpthread-1.dll
        libstdc++-6.dll
    )
        set(_rt_path "${_c_compiler_dir}/${_rt_name}")
        if(EXISTS "${_rt_path}")
            file(COPY "${_rt_path}" DESTINATION "${_stage_chess_dir}")
        endif()
    endforeach()
endif()

string(REPLACE "\\" "\\\\" _stage_chess_dir_iss "${_stage_chess_dir}")
string(REPLACE "\\" "\\\\" _release_dir_iss "${_release_dir}")
string(REPLACE "\\" "\\\\" _app_name_iss "${_app_name}")

file(WRITE "${_iss_file}" [=[
[Setup]
AppId=ChessProject
AppName=Chess
AppVersion=@VERSION@
AppPublisher=ChessProject
DefaultDirName={autopf}\Chess
UsePreviousAppDir=no
DefaultGroupName=Chess
DisableProgramGroupPage=yes
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesInstallIn64BitMode=x64
OutputDir=@OUTDIR@
OutputBaseFilename=@OUTBASE@
UninstallDisplayIcon={app}\@APP_EXE@

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce

[InstallDelete]
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\libgcc_s_seh-1.dll"
Type: files; Name: "{app}\libgcc_s_dw2-1.dll"
Type: files; Name: "{app}\libwinpthread-1.dll"
Type: files; Name: "{app}\libstdc++-6.dll"

[Files]
Source: "@STAGE@\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Chess"; Filename: "{app}\@APP_EXE@"; WorkingDir: "{app}"
Name: "{autodesktop}\Chess"; Filename: "{app}\@APP_EXE@"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\@APP_EXE@"; Description: "Launch Chess"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
]=])

file(READ "${_iss_file}" _iss_text)
string(REPLACE "@VERSION@" "${_version_safe}" _iss_text "${_iss_text}")
string(REPLACE "@OUTDIR@" "${_release_dir_iss}" _iss_text "${_iss_text}")
string(REPLACE "@OUTBASE@" "${_output_base}" _iss_text "${_iss_text}")
string(REPLACE "@STAGE@" "${_stage_chess_dir_iss}" _iss_text "${_iss_text}")
string(REPLACE "@APP_EXE@" "${_app_name_iss}" _iss_text "${_iss_text}")
file(WRITE "${_iss_file}" "${_iss_text}")

execute_process(
    COMMAND "${CHESS_ISCC_EXE}" "${_iss_file}"
    WORKING_DIRECTORY "${_stage_dir}"
    RESULT_VARIABLE _iscc_result
    OUTPUT_VARIABLE _iscc_stdout
    ERROR_VARIABLE _iscc_stderr
)

if(NOT _iscc_result EQUAL 0 OR NOT EXISTS "${_output_file}")
    message(FATAL_ERROR
        "Inno Setup failed while creating installer.\n"
        "stdout:\n${_iscc_stdout}\n"
        "stderr:\n${_iscc_stderr}")
endif()

file(COPY_FILE "${_output_file}" "${_latest_output_file}" ONLY_IF_DIFFERENT)

file(SHA256 "${_output_file}" _output_sha256)
get_filename_component(_output_name "${_output_file}" NAME)
file(WRITE "${_sha256_file}" "${_output_sha256} *${_output_name}\n")
get_filename_component(_latest_output_name "${_latest_output_file}" NAME)
file(WRITE "${_latest_sha256_file}" "${_output_sha256} *${_latest_output_name}\n")

file(REMOVE_RECURSE "${_stage_dir}")

message(STATUS
    "Windows installer created: ${_output_file} (latest alias: ${_latest_output_file})\n"
    "SHA256 file created: ${_sha256_file}")
