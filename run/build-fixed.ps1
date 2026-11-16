param(
    [int]$Scene = -1,
    [switch]$Normal,
    [string]$Extra = ""
)
# Fixed-scene test build.
#
#   .\run\build-fixed.ps1 -Scene 4     # show GLENZ only
#   .\run\build-fixed.ps1 -Normal      # restore the normal build
#
# Scene number (index into scene_list in parts/scenes.c):
#    0 alku    1 u2a     2 pam      3 image   4 glenz    5 tunnel
#    6 techno  7 panic   8 forest   9 lens   10 plasma  11 dots
#   12 water  13 sinus  14 jplogo  15 ddstars 16 end    17 scroll
#   18 u2e    19 credits
#
# Only main.c uses TEST_FIXED_SCENE, so only main.c has to be recompiled.
# Touch main.c so make notices the flag change.
$ErrorActionPreference = "Stop"
$m2 = Join-Path (Split-Path -Parent $PSScriptRoot) "src\m2"
$ccflags = "-g -m68020 -Ofast -nostdlib -Wextra -Wno-unused-function " +
           "-Wno-volatile-register-var -fomit-frame-pointer " +
           "-fno-tree-loop-distribution -flto -fno-exceptions"
if (-not $Normal -and $Scene -ge 0) {
    $ccflags += " -DTEST_FIXED_SCENE=$Scene"
}
if ($Extra -ne "") { $ccflags += " " + $Extra }

$env:PATH = "$env:AMIGA_TOOLCHAIN\opt\bin;$env:AMIGA_TOOLCHAIN;$env:PATH"
$make = Get-Command gnumake -ErrorAction SilentlyContinue
if (-not $make) { $make = Get-Command make -ErrorAction Stop }

Push-Location $m2
# Force main.c to be rebuilt (the new CCFLAGS are not tracked by make).
(Get-Item "main.c").LastWriteTime = Get-Date
& $make.Source -j8 "CCFLAGS=$ccflags"
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "build failed" }
Pop-Location

$exe = Join-Path $m2 "out\m2.exe"
$fi = Get-Item $exe
if ($Normal -or $Scene -lt 0) {
    Write-Output ("built: {0} ({1} bytes, normal)" -f $exe, $fi.Length)
} else {
    Write-Output ("built: {0} ({1} bytes, TEST_FIXED_SCENE={2})" -f `
        $exe, $fi.Length, $Scene)
}
Write-Output "run: .\run\run-m0.ps1"
