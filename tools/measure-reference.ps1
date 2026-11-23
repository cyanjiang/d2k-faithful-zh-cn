# Measures the original's real pace while a mission is running in the
# reference install (docs/reference-setup.md). Read-only: it reads three
# globals of DUNE2000.DAT with ReadProcessMemory and changes nothing.
#   0x5173f4  game tick counter (written in the tick block of FUN_00448550)
#   0x6b9868  slot in the 100-slot tick pattern, +1 per main-loop pass (FUN_0045b790)
#   0x4e3b18  game speed slider (u16)
# Usage: start a mission in the original, keep it unpaused, then run
#   powershell -ExecutionPolicy Bypass -File tools\measure-reference.ps1 [-Seconds 10]
param([int]$Seconds = 10)
$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class D2kMem {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(int access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int size, out IntPtr read);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr h);
    [DllImport("winmm.dll")]
    public static extern uint timeBeginPeriod(uint ms);
    [DllImport("winmm.dll")]
    public static extern uint timeEndPeriod(uint ms);
    public static uint U32(IntPtr h, long addr) {
        var b = new byte[4];
        IntPtr n;
        if (!ReadProcessMemory(h, new IntPtr(addr), b, 4, out n)) throw new Exception("ReadProcessMemory failed at 0x" + addr.ToString("x"));
        return BitConverter.ToUInt32(b, 0);
    }
}
'@

$proc = Get-Process -Name 'DUNE2000.DAT', 'DUNE2000' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { throw 'DUNE2000.DAT is not running.' }
$h = [D2kMem]::OpenProcess(0x0410, $false, $proc.Id) # PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
if ($h -eq [IntPtr]::Zero) { throw 'OpenProcess failed.' }
[D2kMem]::timeBeginPeriod(1) | Out-Null
try {
    $speed = [D2kMem]::U32($h, 0x4e3b18) -band 0xffff
    $tick0 = [D2kMem]::U32($h, 0x5173f4)
    $slot = [D2kMem]::U32($h, 0x6b9868)
    $loops = 0
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $Seconds * 1000) {
        Start-Sleep -Milliseconds 2
        $s = [D2kMem]::U32($h, 0x6b9868)
        if ($s -ge $slot) { $loops += $s - $slot } else { $loops += 100 - $slot + $s } # wrapped (100 -> 1)
        $slot = $s
    }
    $tick1 = [D2kMem]::U32($h, 0x5173f4)
    $secs = $sw.ElapsedMilliseconds / 1000.0
    $tps = ($tick1 - $tick0) / $secs
    $lps = $loops / $secs
    'game speed slider: {0}' -f $speed
    'ticks per second:  {0:N2}' -f $tps
    'loops per second:  {0:N1}  (lower bound if above ~5000)' -f $lps
} finally {
    [D2kMem]::timeEndPeriod(1) | Out-Null
    [D2kMem]::CloseHandle($h) | Out-Null
}
