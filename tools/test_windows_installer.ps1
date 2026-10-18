param(
    [Parameter(Mandatory=$true)][string]$Installer,
    [Parameter(Mandatory=$true)][string]$StorageTests,
    [Parameter(Mandatory=$true)][string]$TestRoot,
    [switch]$TestUI
)
$ErrorActionPreference = 'Stop'
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$testsPath = (Resolve-Path -LiteralPath $StorageTests).Path
$base = [IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($TestRoot))
$runRoot = Join-Path $base ([Guid]::NewGuid().ToString('N'))
$installDir = Join-Path $runRoot 'installed Chess'
$launchDir = Join-Path $runRoot 'unrelated working directory'
$configDir = Join-Path $runRoot 'user preferences'
$previousConfig = $env:CHESS_CONFIG_DIR
$group = 'Chess smoke ' + [IO.Path]::GetFileName($runRoot)
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ChessProject_is1'
if (Test-Path -LiteralPath $uninstallKey) {
    throw 'A per-user Chess installation already exists. Use a clean test account to avoid replacing its registration.'
}
New-Item -ItemType Directory -Path $launchDir -Force | Out-Null
$env:CHESS_CONFIG_DIR = $configDir
$installed = $false
$completed = $false

function Run-SettingsTest([string]$Mode, [int]$Theme = 2) {
    Push-Location -LiteralPath $launchDir
    try {
        & $testsPath $Mode $Theme
        if ($LASTEXITCODE -ne 0) { throw "Settings test failed: $Mode" }
    } finally { Pop-Location }
}

function Install-Chess {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER',
        ('/DIR="' + $installDir + '"'), ('/GROUP="' + $group + '"'), '/TASKS=""',
        ('/LOG="' + (Join-Path $runRoot 'install.log') + '"'))
    $process = Start-Process -FilePath $installerPath -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $processHandle = $process.Handle
    if (-not $process.WaitForExit(60000)) { throw 'Installer did not finish within 60 seconds.' }
    if ($process.ExitCode -ne 0) { throw "Installer failed: $($process.ExitCode)" }
    if (-not (Test-Path -LiteralPath (Join-Path $installDir 'chess_app.exe'))) { throw 'Installed executable missing.' }
    if (-not (Test-Path -LiteralPath (Join-Path $installDir 'assets\pieces\staunton\wk.png'))) { throw 'Installed assets missing.' }
    if (-not (Test-Path -LiteralPath (Join-Path $installDir 'LICENSE'))) { throw 'Packaged license missing.' }
    if (Test-Path -LiteralPath (Join-Path $installDir 'settings.dat')) { throw 'Package contains user settings.' }
}

if ($TestUI) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ChessInstallSmokeWindow {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
    $previousDpi = [ChessInstallSmokeWindow]::SetThreadDpiAwarenessContext([IntPtr](-4))
}

function Click-Game($Game, [int]$X, [int]$Y) {
    $position = [IntPtr](($Y -shl 16) -bor ($X -band 65535))
    [ChessInstallSmokeWindow]::PostMessage($Game.MainWindowHandle, 512, [IntPtr]::Zero, $position) | Out-Null
    [ChessInstallSmokeWindow]::PostMessage($Game.MainWindowHandle, 513, [IntPtr]1, $position) | Out-Null
    Start-Sleep -Milliseconds 100
    [ChessInstallSmokeWindow]::PostMessage($Game.MainWindowHandle, 512, [IntPtr]::Zero, $position) | Out-Null
    [ChessInstallSmokeWindow]::PostMessage($Game.MainWindowHandle, 514, [IntPtr]::Zero, $position) | Out-Null
    for ($sample = 0; $sample -lt 15; $sample++) {
        [ChessInstallSmokeWindow]::PostMessage($Game.MainWindowHandle, 512, [IntPtr]::Zero, $position) | Out-Null
        Start-Sleep -Milliseconds 20
    }
}

function Change-ThemeInGame([int]$ExpectedTheme) {
    $game = Start-Process -FilePath (Join-Path $installDir 'chess_app.exe') -WorkingDirectory $launchDir `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $runRoot "game-$ExpectedTheme.log") `
        -RedirectStandardError (Join-Path $runRoot "game-$ExpectedTheme.err")
    $gameHandle = $game.Handle
    try {
        $deadline = (Get-Date).AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 200
            $game.Refresh()
        } while (-not $game.HasExited -and $game.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline)
        if ($game.HasExited -or $game.MainWindowHandle -eq 0) { throw 'Installed game did not open a window.' }
        Start-Sleep -Milliseconds 1000
        $rect = New-Object ChessInstallSmokeWindow+RECT
        [ChessInstallSmokeWindow]::GetClientRect($game.MainWindowHandle, [ref]$rect) | Out-Null
        # The app starts with a logical 1280x820 canvas. The native client may
        # be larger under Windows DPI scaling; button coordinates stay logical.
        Write-Output ("Native client: {0}x{1}" -f ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
        $width = 1280
        $height = 820
        $menuHeight = [Math]::Min(670, [Math]::Max(550, $height * 0.72))
        Click-Game $game ($width / 2) (($height - $menuHeight) / 2 + 355)
        $settingsWidth = [Math]::Min(1160, [Math]::Max(900, $width * 0.82))
        $settingsHeight = [Math]::Min(760, [Math]::Max(600, $height * 0.82))
        $leftWidth = ($settingsWidth - 72) * 0.54
        Click-Game $game (($width - $settingsWidth) / 2 + $leftWidth - 32) (($height - $settingsHeight) / 2 + 280)
        Start-Sleep -Milliseconds 600
        [ChessInstallSmokeWindow]::PostMessage($game.MainWindowHandle, 16, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        if (-not $game.WaitForExit(15000)) { throw 'Installed game did not close.' }
        if ($game.ExitCode -ne 0) { throw "Installed game failed: $($game.ExitCode)" }
    } finally {
        if (-not $game.HasExited) { Stop-Process -Id $game.Id }
    }
    Run-SettingsTest 'check' $ExpectedTheme
    $log = Get-Content -LiteralPath (Join-Path $runRoot "game-$ExpectedTheme.log") -Raw
    if ($log -notmatch 'Window closed successfully' -or $log -match 'WARNING: FILEIO') {
        throw 'Installed game did not load its assets and close normally.'
    }
}

try {
    Run-SettingsTest 'write'
    $installed = $true
    Install-Chess
    Run-SettingsTest 'check' 2
    $expected = 2
    if ($TestUI) {
        $originalAcl = Get-Acl -LiteralPath $installDir
        try {
            $restrictedAcl = Get-Acl -LiteralPath $installDir
            $identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
            $inherit = [Security.AccessControl.InheritanceFlags]::ContainerInherit -bor [Security.AccessControl.InheritanceFlags]::ObjectInherit
            $deny = New-Object Security.AccessControl.FileSystemAccessRule($identity, [Security.AccessControl.FileSystemRights]::Write, $inherit, [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Deny)
            $restrictedAcl.AddAccessRule($deny)
            Set-Acl -LiteralPath $installDir -AclObject $restrictedAcl
            try {
                [IO.File]::WriteAllText((Join-Path $installDir 'must-not-be-writable.txt'), 'test')
                throw 'Install directory was expected to reject writes.'
            } catch [UnauthorizedAccessException] { Write-Output 'Install directory rejects writes as expected.' }
            Change-ThemeInGame 0
            Change-ThemeInGame 1
        } finally { Set-Acl -LiteralPath $installDir -AclObject $originalAcl }
        $expected = 1
    }
    Install-Chess # Upgrade/reinstall must not replace per-user settings.
    Run-SettingsTest 'check' $expected
    if ($TestUI) { Change-ThemeInGame 2; $expected = 2 }
    $completed = $true
} finally {
    try {
        if ($installed) {
            $resolvedInstall = [IO.Path]::GetFullPath($installDir)
            $expectedPrefix = [IO.Path]::GetFullPath($runRoot).TrimEnd('\') + '\'
            if (-not $resolvedInstall.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to uninstall outside test directory: $resolvedInstall"
            }
            $uninstaller = Join-Path $resolvedInstall 'unins000.exe'
            if (Test-Path -LiteralPath $uninstaller) {
                $process = Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -WindowStyle Hidden -PassThru
                $processHandle = $process.Handle
                if (-not $process.WaitForExit(60000) -or $process.ExitCode -ne 0) { throw 'Test uninstall failed.' }
            }
        }
    } finally {
        $env:CHESS_CONFIG_DIR = $previousConfig
        if ($TestUI -and $previousDpi -ne [IntPtr]::Zero) {
            [ChessInstallSmokeWindow]::SetThreadDpiAwarenessContext($previousDpi) | Out-Null
        }
    }
}
if ($completed) {
    $env:CHESS_CONFIG_DIR = $configDir
    try { Run-SettingsTest 'check' $expected } finally { $env:CHESS_CONFIG_DIR = $previousConfig }
    if (Test-Path -LiteralPath $uninstallKey) { throw 'Test uninstall left its registration behind.' }
    Write-Output "Install, reinstall, persistence and uninstall checks passed. Fixtures: $runRoot"
}
