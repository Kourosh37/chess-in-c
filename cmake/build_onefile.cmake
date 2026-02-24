if(NOT WIN32)
    message(FATAL_ERROR "chess_onefile packaging is only supported on Windows.")
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

if(NOT DEFINED CHESS_7Z_EXE OR CHESS_7Z_EXE STREQUAL "" OR CHESS_7Z_EXE MATCHES "-NOTFOUND$")
    find_program(CHESS_7Z_EXE
        NAMES 7z 7z.exe
        HINTS
            "$ENV{ProgramFiles}/7-Zip"
            "$ENV{ProgramW6432}/7-Zip"
    )
endif()

if(NOT CHESS_7Z_EXE OR CHESS_7Z_EXE MATCHES "-NOTFOUND$" OR NOT EXISTS "${CHESS_7Z_EXE}")
    message(FATAL_ERROR
        "7z.exe was not found.\n"
        "Install 7-Zip and retry (example: winget install --id 7zip.7zip).")
endif()

get_filename_component(_app_name "${CHESS_APP_EXE}" NAME)
get_filename_component(_sevenzip_dir "${CHESS_7Z_EXE}" DIRECTORY)

set(_sfx_module "")
foreach(_candidate IN ITEMS "${_sevenzip_dir}/7z.sfx")
    if(EXISTS "${_candidate}")
        set(_sfx_module "${_candidate}")
        break()
    endif()
endforeach()

if(_sfx_module STREQUAL "")
    message(FATAL_ERROR
        "Could not find 7z.sfx next to 7z.exe.\n"
        "Install full 7-Zip package from 7-zip.org and retry.")
endif()

set(_release_dir "${CHESS_OUT_DIR}")
set(_stage_dir "${_release_dir}/_onefile_stage")
set(_stage_chess_dir "${_stage_dir}/chess")
set(_payload_file "${_release_dir}/chess_payload.7z")
set(_sfx_config_file "${_release_dir}/chess_sfx_config.txt")
set(_installer_cmd "${_stage_dir}/install_chess.cmd")
set(_installer_ps1 "${_stage_dir}/install_chess.ps1")
set(_latest_output_file "${_release_dir}/chess.exe")
set(_latest_sha256_file "${_latest_output_file}.sha256")
set(_version "${CHESS_RELEASE_VERSION}")

if(DEFINED _version)
    string(STRIP "${_version}" _version)
endif()

if(NOT _version STREQUAL "")
    string(REGEX REPLACE "[^0-9A-Za-z._-]" "_" _version_safe "${_version}")
    set(_output_file "${_release_dir}/chess-windows-x64-v${_version_safe}.exe")
else()
    set(_output_file "${_latest_output_file}")
endif()
set(_sha256_file "${_output_file}.sha256")

file(REMOVE_RECURSE "${_stage_dir}")
file(MAKE_DIRECTORY "${_release_dir}")
file(MAKE_DIRECTORY "${_stage_chess_dir}")

file(COPY "${CHESS_APP_EXE}" DESTINATION "${_stage_chess_dir}")
file(COPY "${CHESS_SOURCE_ASSETS}" DESTINATION "${_stage_chess_dir}")

# Bundle user-provided runtime DLLs placed at project root.
file(GLOB _project_root_dlls "${CHESS_SOURCE_DIR}/*.dll")
foreach(_project_dll IN LISTS _project_root_dlls)
    if(EXISTS "${_project_dll}")
        file(COPY "${_project_dll}" DESTINATION "${_stage_chess_dir}")
    endif()
endforeach()

# Installer helper script: copy runtime to user-local install path and create Desktop shortcut.
file(WRITE "${_installer_ps1}" [=[
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceDir = Join-Path $scriptDir "chess"
$installDir = Join-Path $env:LOCALAPPDATA "Chess"
$targetExe = Join-Path $installDir "chess_app.exe"

if (-not (Test-Path -LiteralPath $sourceDir)) {
    throw "Missing extracted payload folder: $sourceDir"
}

if (-not (Test-Path -LiteralPath $installDir)) {
    New-Item -ItemType Directory -Path $installDir | Out-Null
}

& robocopy $sourceDir $installDir /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NC /NS | Out-Null
if ($LASTEXITCODE -gt 7) {
    throw "Install copy failed with robocopy exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $targetExe)) {
    throw "Installed executable not found: $targetExe"
}

$desktopDir = [Environment]::GetFolderPath("Desktop")
$shortcutPath = Join-Path $desktopDir "Chess.lnk"
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $targetExe
$shortcut.WorkingDirectory = $installDir
$shortcut.IconLocation = "$targetExe,0"
$shortcut.Description = "Chess"
$shortcut.Save()

Start-Process -FilePath $targetExe -WorkingDirectory $installDir
]=])

file(WRITE "${_installer_cmd}" [=[
@echo off
setlocal
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0install_chess.ps1"
endlocal
]=])

# Bundle non-system runtime DLLs when present (toolchain/runtime dependent).
set(_stage_app_exe "${_stage_chess_dir}/${_app_name}")
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES "${_stage_app_exe}"
        RESOLVED_DEPENDENCIES_VAR _runtime_deps
        UNRESOLVED_DEPENDENCIES_VAR _runtime_unresolved
        PRE_EXCLUDE_REGEXES
            "api-ms-.*"
            "ext-ms-.*"
        POST_EXCLUDE_REGEXES
            ".*/Windows/System32/.*"
            ".*/Windows/SysWOW64/.*"
    )

    foreach(_dep IN LISTS _runtime_deps)
        if(EXISTS "${_dep}")
            file(COPY "${_dep}" DESTINATION "${_stage_chess_dir}")
        endif()
    endforeach()

    if(_runtime_unresolved)
        message(STATUS "Unresolved runtime dependencies (usually system-provided): ${_runtime_unresolved}")
    endif()
endif()

execute_process(
    COMMAND "${CHESS_7Z_EXE}" a -t7z -mx=9 -bd -y "${_payload_file}" "chess"
    WORKING_DIRECTORY "${_stage_dir}"
    RESULT_VARIABLE _pack_result
    OUTPUT_VARIABLE _pack_stdout
    ERROR_VARIABLE _pack_stderr
)

if(NOT _pack_result EQUAL 0)
    message(FATAL_ERROR
        "7z failed while creating payload archive.\n"
        "stdout:\n${_pack_stdout}\n"
        "stderr:\n${_pack_stderr}")
endif()

file(WRITE "${_sfx_config_file}" [=[;!@Install@!UTF-8!
Title="Chess Installer"
BeginPrompt="Install Chess to your user profile and create a Desktop shortcut?"
ExtractTitle="Installing Chess"
ExtractDialogText="Installing files..."
GUIMode="1"
RunProgram="install_chess.cmd"
;!@InstallEnd@!
]=])

execute_process(
    COMMAND "${CHESS_7Z_EXE}" a -t7z -mx=9 -bd -y "${_payload_file}" "install_chess.cmd" "install_chess.ps1"
    WORKING_DIRECTORY "${_stage_dir}"
    RESULT_VARIABLE _pack_installer_result
    OUTPUT_VARIABLE _pack_installer_stdout
    ERROR_VARIABLE _pack_installer_stderr
)

if(NOT _pack_installer_result EQUAL 0)
    message(FATAL_ERROR
        "7z failed while adding installer scripts.\n"
        "stdout:\n${_pack_installer_stdout}\n"
        "stderr:\n${_pack_installer_stderr}")
endif()

set(_concat_ps1 "${_release_dir}/concat_onefile.ps1")
file(WRITE "${_concat_ps1}" [=[
param(
    [Parameter(Mandatory = $true)][string]$Sfx,
    [Parameter(Mandatory = $true)][string]$Config,
    [Parameter(Mandatory = $true)][string]$Payload,
    [Parameter(Mandatory = $true)][string]$Out
)

$ErrorActionPreference = 'Stop'

$outDir = Split-Path -Parent $Out
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$dst = [System.IO.File]::Open($Out, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
try {
    foreach ($src in @($Sfx, $Config, $Payload)) {
        $data = [System.IO.File]::ReadAllBytes($src)
        $dst.Write($data, 0, $data.Length)
    }
}
finally {
    $dst.Dispose()
}
]=])

execute_process(
    COMMAND powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -File "${_concat_ps1}"
        -Sfx "${_sfx_module}"
        -Config "${_sfx_config_file}"
        -Payload "${_payload_file}"
        -Out "${_output_file}"
    RESULT_VARIABLE _copy_result
    OUTPUT_VARIABLE _copy_stdout
    ERROR_VARIABLE _copy_stderr
)

if(NOT _copy_result EQUAL 0 OR NOT EXISTS "${_output_file}")
    message(FATAL_ERROR
        "Failed to create one-file executable.\n"
        "stdout:\n${_copy_stdout}\n"
        "stderr:\n${_copy_stderr}")
endif()

if(NOT _output_file STREQUAL _latest_output_file)
    file(COPY_FILE "${_output_file}" "${_latest_output_file}" ONLY_IF_DIFFERENT)
endif()

file(SHA256 "${_output_file}" _output_sha256)
get_filename_component(_output_name "${_output_file}" NAME)
file(WRITE "${_sha256_file}" "${_output_sha256} *${_output_name}\n")
if(NOT _output_file STREQUAL _latest_output_file)
    get_filename_component(_latest_output_name "${_latest_output_file}" NAME)
    file(WRITE "${_latest_sha256_file}" "${_output_sha256} *${_latest_output_name}\n")
endif()

file(REMOVE_RECURSE "${_stage_dir}")
file(REMOVE "${_payload_file}")
file(REMOVE "${_sfx_config_file}")
file(REMOVE "${_concat_ps1}")

if(NOT _version STREQUAL "")
    message(STATUS
        "One-file package created: ${_output_file} (latest alias: ${_latest_output_file})\n"
        "SHA256 file created: ${_sha256_file}")
else()
    message(STATUS "One-file package created: ${_output_file}\nSHA256 file created: ${_sha256_file}")
endif()
