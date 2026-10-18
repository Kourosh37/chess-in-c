cmake_minimum_required(VERSION 3.21)

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
            "$ENV{ProgramW6432}/Inno Setup 7"
            "$ENV{ProgramFiles}/Inno Setup 7"
            "C:/Program Files (x86)/Inno Setup 7"
            "$ENV{ProgramW6432}/Inno Setup 6"
            "$ENV{ProgramFiles}/Inno Setup 6"
            "C:/Program Files (x86)/Inno Setup 6"
    )
endif()

if(NOT CHESS_ISCC_EXE OR CHESS_ISCC_EXE MATCHES "-NOTFOUND$" OR NOT EXISTS "${CHESS_ISCC_EXE}")
    message(FATAL_ERROR
        "iscc.exe (Inno Setup) was not found.\n"
        "Install Inno Setup 6.3 or newer and retry (example: choco install innosetup -y).")
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

if(NOT DEFINED CHESS_RELEASE_ARCH OR CHESS_RELEASE_ARCH STREQUAL "")
    message(FATAL_ERROR "CHESS_RELEASE_ARCH is required; build the chess_onefile target.")
endif()
if(CHESS_RELEASE_ARCH STREQUAL "x64")
    set(_architectures "ArchitecturesAllowed=x64compatible\nArchitecturesInstallIn64BitMode=x64compatible")
elseif(CHESS_RELEASE_ARCH STREQUAL "arm64")
    set(_architectures "ArchitecturesAllowed=arm64\nArchitecturesInstallIn64BitMode=arm64")
elseif(CHESS_RELEASE_ARCH STREQUAL "x86")
    set(_architectures "ArchitecturesAllowed=x86compatible")
else()
    message(FATAL_ERROR "Unsupported Windows release architecture: ${CHESS_RELEASE_ARCH}")
endif()
if(CHESS_COMPILER_ID STREQUAL "MSVC" AND NOT CHESS_PORTABLE_RUNTIME)
    message(FATAL_ERROR "Configure with CHESS_WINDOWS_PORTABLE_RUNTIME=ON before packaging an MSVC build.")
endif()
set(_output_base "chess-windows-${CHESS_RELEASE_ARCH}-v${_version_safe}")
set(_output_file "${_release_dir}/${_output_base}.exe")
set(_sha256_file "${_output_file}.sha256")
set(_latest_output_file "${_release_dir}/chess.exe")
set(_latest_sha256_file "${_latest_output_file}.sha256")

file(REMOVE_RECURSE "${_stage_dir}")
file(MAKE_DIRECTORY "${_release_dir}")
file(MAKE_DIRECTORY "${_stage_chess_dir}")

file(COPY "${CHESS_APP_EXE}" DESTINATION "${_stage_chess_dir}")
file(COPY "${CHESS_SOURCE_ASSETS}" DESTINATION "${_stage_chess_dir}")
file(COPY "${CHESS_SOURCE_DIR}/LICENSE" DESTINATION "${_stage_chess_dir}")

# Bundle the executable's actual non-system dependencies.
include("${CMAKE_CURRENT_LIST_DIR}/stage_windows_runtime.cmake")
chess_stage_windows_runtime("${CHESS_APP_EXE}" "${_stage_chess_dir}")
# Paths in Inno Setup are literal, not C string literals.
set(_stage_chess_dir_iss "${_stage_chess_dir}")
set(_release_dir_iss "${_release_dir}")
set(_app_name_iss "${_app_name}")

file(WRITE "${_iss_file}" [=[
[Setup]
AppId=ChessProject
AppName=Chess
AppVersion=@VERSION@
AppPublisher=ChessProject
DefaultDirName={autopf}\Chess
UsePreviousAppDir=yes
DefaultGroupName=Chess
DisableProgramGroupPage=yes
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
@ARCHITECTURES@
CloseApplications=yes
RestartApplications=no
OutputDir=@OUTDIR@
OutputBaseFilename=@OUTBASE@
UninstallDisplayIcon={app}\@APP_EXE@

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce

[InstallDelete]
Type: files; Name: "{app}\*.dll"

[Files]
Source: "@STAGE@\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Chess"; Filename: "{app}\@APP_EXE@"; WorkingDir: "{app}"
Name: "{autodesktop}\Chess"; Filename: "{app}\@APP_EXE@"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\@APP_EXE@"; Description: "Launch Chess"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent runasoriginaluser
]=])

file(READ "${_iss_file}" _iss_text)
string(REPLACE "@ARCHITECTURES@" "${_architectures}" _iss_text "${_iss_text}")
string(REPLACE "@VERSION@" "${_version_safe}" _iss_text "${_iss_text}")
string(REPLACE "@OUTDIR@" "${_release_dir_iss}" _iss_text "${_iss_text}")
string(REPLACE "@OUTBASE@" "${_output_base}" _iss_text "${_iss_text}")
string(REPLACE "@STAGE@" "${_stage_chess_dir_iss}" _iss_text "${_iss_text}")
string(REPLACE "@APP_EXE@" "${_app_name_iss}" _iss_text "${_iss_text}")
file(WRITE "${_iss_file}" "${_iss_text}")
file(WRITE "${_release_dir}/chess_installer.iss" "${_iss_text}")

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
