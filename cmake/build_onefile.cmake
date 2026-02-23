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
foreach(_candidate IN ITEMS
    "${_sevenzip_dir}/7zSD.sfx"
    "${_sevenzip_dir}/7zS2.sfx"
    "${_sevenzip_dir}/7zS.sfx"
)
    if(EXISTS "${_candidate}")
        set(_sfx_module "${_candidate}")
        break()
    endif()
endforeach()

if(_sfx_module STREQUAL "")
    set(_sfx_cache_dir "${CHESS_OUT_DIR}/_sfx_cache")
    set(_sfx_lzma_archive "${_sfx_cache_dir}/lzma_sdk.7z")
    set(_sfx_extract_dir "${_sfx_cache_dir}/lzma_sdk")
    set(_sfx_download_url "https://7-zip.org/a/lzma2501.7z")

    file(MAKE_DIRECTORY "${_sfx_cache_dir}")

    if(NOT EXISTS "${_sfx_lzma_archive}")
        file(DOWNLOAD
            "${_sfx_download_url}"
            "${_sfx_lzma_archive}"
            SHOW_PROGRESS
            STATUS _dl_status
        )
        list(GET _dl_status 0 _dl_code)
        list(GET _dl_status 1 _dl_msg)
        if(NOT _dl_code EQUAL 0)
            message(FATAL_ERROR
                "Failed to download LZMA SDK for 7z SFX module.\n"
                "URL: ${_sfx_download_url}\n"
                "Status: ${_dl_code} (${_dl_msg})")
        endif()
    endif()

    if(NOT EXISTS "${_sfx_extract_dir}/bin/7zSD.sfx")
        file(MAKE_DIRECTORY "${_sfx_extract_dir}")
        execute_process(
            COMMAND "${CHESS_7Z_EXE}" x -y "-o${_sfx_extract_dir}" "${_sfx_lzma_archive}" "bin/7zSD.sfx"
            RESULT_VARIABLE _x_result
            OUTPUT_VARIABLE _x_stdout
            ERROR_VARIABLE _x_stderr
        )
        if(NOT _x_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to extract 7zSD.sfx from LZMA SDK.\n"
                "stdout:\n${_x_stdout}\n"
                "stderr:\n${_x_stderr}")
        endif()
    endif()

    if(EXISTS "${_sfx_extract_dir}/bin/7zSD.sfx")
        set(_sfx_module "${_sfx_extract_dir}/bin/7zSD.sfx")
    endif()
endif()

if(_sfx_module STREQUAL "")
    message(FATAL_ERROR
        "Could not find a config-capable 7z SFX module (7zSD.sfx/7zS2.sfx/7zS.sfx).")
endif()

set(_release_dir "${CHESS_OUT_DIR}")
set(_stage_dir "${_release_dir}/_onefile_stage")
set(_payload_file "${_release_dir}/chess_payload.7z")
set(_config_file "${_release_dir}/chess_sfx_config.txt")
set(_output_file "${_release_dir}/Chess-OneFile.exe")
set(_launcher_file "${_stage_dir}/launch_chess.bat")

file(REMOVE_RECURSE "${_stage_dir}")
file(MAKE_DIRECTORY "${_stage_dir}")
file(MAKE_DIRECTORY "${_release_dir}")

file(COPY "${CHESS_APP_EXE}" DESTINATION "${_stage_dir}")
file(COPY "${CHESS_SOURCE_ASSETS}" DESTINATION "${_stage_dir}")
file(WRITE "${_launcher_file}" [=[
@echo off
setlocal
set "SRC=%~dp0"
set "BASE=%CD%\chess"
set "DST=%BASE%"
set /a IDX=0

:pick_folder
if exist "%DST%" (
    set /a IDX+=1
    set "DST=%CD%\chess(%IDX%)"
    goto pick_folder
)

mkdir "%DST%" >nul 2>&1
if errorlevel 1 (
    exit /b 1
)

xcopy /Y /Q "%SRC%chess_app.exe" "%DST%\" >nul 2>&1
if exist "%SRC%assets" (
    xcopy /E /I /Y /Q "%SRC%assets" "%DST%\assets\" >nul 2>&1
)

start "" "%DST%\chess_app.exe"
endlocal
]=])

file(WRITE "${_config_file}" [=[
;!@Install@!UTF-8!
Title="Chess"
InstallPath="%%T\ChessOneFile"
GUIMode="1"
RunProgram="launch_chess.bat"
;!@InstallEnd@!
]=])

execute_process(
    COMMAND "${CHESS_7Z_EXE}" a -t7z -mx=9 -bd -y "${_payload_file}" "${_app_name}" "assets" "launch_chess.bat"
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

set(_concat_ps1 "${_release_dir}/concat_onefile.ps1")
file(WRITE "${_concat_ps1}" [=[
param(
    [Parameter(Mandatory = $true)][string]$Sfx,
    [Parameter(Mandatory = $true)][string]$Cfg,
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
    foreach ($src in @($Sfx, $Cfg, $Payload)) {
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
        -Cfg "${_config_file}"
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

file(REMOVE_RECURSE "${_stage_dir}")
file(REMOVE "${_payload_file}")
file(REMOVE "${_config_file}")
file(REMOVE "${_concat_ps1}")

message(STATUS "One-file package created: ${_output_file}")
