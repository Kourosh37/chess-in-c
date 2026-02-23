param(
    [Parameter(Mandatory = $true)][string]$InputPng,
    [Parameter(Mandatory = $true)][string]$OutputIco
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace Win32 -Name NativeMethods -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool DestroyIcon(System.IntPtr hIcon);
'@

$outDir = Split-Path -Parent $OutputIco
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$bitmap = [System.Drawing.Bitmap]::FromFile($InputPng)
try {
    $hIcon = $bitmap.GetHicon()
    try {
        $icon = [System.Drawing.Icon]::FromHandle($hIcon)
        try {
            $stream = [System.IO.File]::Open($OutputIco,
                [System.IO.FileMode]::Create,
                [System.IO.FileAccess]::Write,
                [System.IO.FileShare]::None)
            try {
                $icon.Save($stream)
            }
            finally {
                $stream.Dispose()
            }
        }
        finally {
            $icon.Dispose()
        }
    }
    finally {
        [void][Win32.NativeMethods]::DestroyIcon($hIcon)
    }
}
finally {
    $bitmap.Dispose()
}
