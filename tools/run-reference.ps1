# Starts the original Dune 2000 1.06 reference install (docs/reference-setup.md):
# mounts DUNE2000.iso for the CD check, runs DUNE2000.DAT from reference/d2k106
# and dismounts the ISO when the game exits. Local dev aid only.
# The original has no switch to start a mission; use the menus.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$iso = Join-Path $repo 'DUNE2000.iso'
$dir = Join-Path $repo 'reference\d2k106'

$mounted = (Get-DiskImage -ImagePath $iso).Attached
if (-not $mounted) { Mount-DiskImage -ImagePath $iso | Out-Null }
try {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = Join-Path $dir 'DUNE2000.DAT'
    $psi.WorkingDirectory = $dir
    $psi.UseShellExecute = $false
    [System.Diagnostics.Process]::Start($psi).WaitForExit()
} finally {
    if (-not $mounted) { Dismount-DiskImage -ImagePath $iso | Out-Null }
}
