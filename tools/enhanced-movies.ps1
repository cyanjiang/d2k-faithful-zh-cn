# Upscaled movie pictures for the enhanced_movies option (docs/enhanced-movies.md).
# Personal use: everything stays on this PC, nothing goes into the repo.
#
#   tools\enhanced-movies.ps1 export            every VQA -> lossless ProRes 4444 source
#   tools\enhanced-movies.ps1 upscale           Topaz -> 2560x1600 master -> the .mp4 the game
#                                               plays; the master (~5 GB a minute) is deleted
#   tools\enhanced-movies.ps1 encode            a kept master (-KeepMaster) or lanczos test -> .mp4
#   (-Only NAME limits any step to one movie, e.g. -Only A_BR01_E)
#
# Folders: work files in %LOCALAPPDATA%\d2k-faithful\movies-work, the game's
# files in %APPDATA%\d2k-faithful\d2k\movies-enhanced. ffmpeg on PATH for
# export / encode; Topaz Video AI installed and signed in for upscale.
param(
    [Parameter(Mandatory)][ValidateSet("export", "upscale", "encode", "lanczos")][string]$Step,
    [string]$Only = "",
    [int]$Fps = 60,                      # Topaz frame interpolation to this rate; 0 = keep 15 fps
    [string]$Model = "iris-3",           # Topaz enhancement model (tvai_up); chosen 2026-10-23
    [switch]$KeepMaster,                 # keep the ProRes master after encoding
    [string]$FpsModel = "apo-8",         # Topaz interpolation model (tvai_fi)
    [string]$Tool = "",                    # default: the Release d2k-tool next to this script
    [string]$Topaz = "C:\Program Files\Topaz Labs LLC\Topaz Video"
)
$ErrorActionPreference = "Stop"
if (-not $Tool) { $Tool = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\build\msvc\bin\Release\d2k-tool.exe" }
$Tool = (Resolve-Path $Tool).Path
$movies = Join-Path $env:APPDATA "d2k-faithful\d2k\gamedata\1.06\MOVIES"
$work = Join-Path $env:LOCALAPPDATA "d2k-faithful\movies-work"
$out = Join-Path $env:APPDATA "d2k-faithful\d2k\movies-enhanced"
New-Item -ItemType Directory -Force "$work\source", "$work\master", $out | Out-Null

$vqas = Get-ChildItem $movies -Filter *.VQA | Where-Object { -not $Only -or $_.BaseName -eq $Only }
if (-not $vqas) { throw "no movies found in $movies (run the game's setup first)" }

# Runs a command line through cmd from a batch file: cmd's pipe keeps raw
# frames binary (PowerShell 5's does not), and a file avoids PowerShell 5
# mangling the quotes it passes to cmd.
function Invoke-Cmd([string]$line) {
    $bat = Join-Path $work "run.cmd"
    Set-Content -Path $bat -Value "@echo off`r`n$line" -Encoding ascii
    & cmd /c $bat | Out-Host
    return $LASTEXITCODE
}

# H.264 for Windows' own decoder: 4:2:0, BT.709 limited range, as the game expects.
$x264 = "-c:v libx264 -preset slow -crf 14 -pix_fmt yuv420p -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv -movflags +faststart -an"
$to709 = "scale=out_color_matrix=bt709:out_range=tv"

$failed = @()
foreach ($v in $vqas) {
    $name = $v.BaseName
    $src = "$work\source\$name.mov"
    $master = "$work\master\$name.mov"
    switch ($Step) {
        "export" {
            if (Test-Path $src) { "skip $name (exported)"; continue }
            $rc = Invoke-Cmd "`"$Tool`" vqa-rgb `"$($v.FullName)`" - | ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 640x400 -r 15 -i - -c:v prores_ks -profile:v 4444 -pix_fmt yuv444p10le `"$src`""
            if ($rc -ne 0) { throw "export of $name failed" }
            "exported $name"
        }
        "upscale" {
            if (Test-Path "$out\$name.mp4") { "skip $name (done)"; continue }
            $ff = Join-Path $Topaz "ffmpeg.exe"
            if (-not (Test-Path $ff)) { throw "Topaz Video not found at $Topaz" }
            $env:TVAI_MODEL_DATA_DIR = Join-Path $env:ProgramData "Topaz Labs LLC\Topaz Video\models"
            $env:TVAI_MODEL_DIR = Join-Path $env:ProgramData "Topaz Labs LLC\Topaz Video\models"
            $filter = "tvai_up=model=${Model}:scale=4"
            if ($Fps -gt 0) { $filter += ",tvai_fi=model=${FpsModel}:fps=$Fps" }
            $sw = [Diagnostics.Stopwatch]::StartNew()
            # Topaz's ffmpeg prints the account's sign-in tokens on stderr at
            # every start, whatever the log level: never show or save its
            # output. cmd sends it to nul (in PowerShell 5 any stderr line of
            # a native program stops the script, even with 2>$null).
            $rc = Invoke-Cmd "`"$ff`" -hide_banner -loglevel error -y -i `"$src`" -vf `"$filter`" -c:v prores_ks -profile:v 3 `"$master`" >nul 2>nul"
            if ($rc -ne 0) { "FAILED $name (Topaz exit $rc)"; $failed += $name; continue }
            $rc = Invoke-Cmd "ffmpeg -hide_banner -loglevel error -y -i `"$master`" -vf `"$to709`" $x264 `"$out\$name.mp4`""
            if ($rc -ne 0) { "FAILED $name (encode)"; $failed += $name; continue }
            if (-not $KeepMaster) { Remove-Item $master }
            "done $name in $([int]$sw.Elapsed.TotalMinutes) min"
        }
        "lanczos" {
            # A plain (non-AI) 4x upscale, to test the game side without Topaz.
            $rc = Invoke-Cmd "ffmpeg -hide_banner -loglevel error -y -i `"$src`" -vf `"scale=2560:1600:flags=lanczos`" -c:v prores_ks -profile:v 3 `"$master`""
            if ($rc -ne 0) { throw "lanczos of $name failed" }
            "lanczos $name"
        }
        "encode" {
            if (-not (Test-Path $master)) { "skip $name (no master)"; continue }
            $rc = Invoke-Cmd "ffmpeg -hide_banner -loglevel error -y -i `"$master`" -vf `"$to709`" $x264 `"$out\$name.mp4`""
            if ($rc -ne 0) { throw "encode of $name failed" }
            "encoded $name"
        }
    }
}
if ($failed) { "failed: $($failed -join ' ')"; exit 1 }
