param(
    [string]$OutputRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build'),
    [string]$VcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot 'cpp\floating'
$header = Get-Content -LiteralPath (Join-Path $source 'version.h') -Raw
if ($header -notmatch '(?m)^#define AUDCORE_FLOATING_VERSION "(\d+\.\d+\.\d+)"') { throw 'Invalid skin version' }
$version = $Matches[1]
$mutex = [Threading.Mutex]::new($false, 'Global\Utae.StreamingBuild.Single')
$locked = $false
try {
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked -or (Get-Process cargo,rustc,cl,link,rc,cmake,clang,clang-cl,cc1plus -ErrorAction SilentlyContinue)) {
        throw 'Another build is running'
    }
    if ((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt (2GB / 1KB)) {
        throw 'Less than 2 GiB of physical memory is available'
    }
    if (!(Test-Path -LiteralPath $VcVars)) { throw "Missing compiler setup: $VcVars" }
    $output = (New-Item -ItemType Directory -Force -Path (Join-Path $OutputRoot "Audcore-Floating-v$version")).FullName
    $batch = Join-Path $output 'build-floating.cmd'
    $artifact = Join-Path $output "Audcore-Floating-v$version.skin.audcore"
    $flags = '/nologo /utf-8 /std:c++20 /EHsc /W4 /O1 /MT /experimental:deterministic "/pathmap:{0}=."' -f $root
    $lines = @(
        '@echo off',
        ('cd /d "{0}" || exit /b 1' -f $output),
        ('call "{0}" >nul || exit /b 1' -f $VcVars),
        'set "CL="', 'set "_CL_="', 'set "LINK="', 'set "_LINK_="',
        ('rc /nologo /i "{0}\crates\audcore-app" /fo skin.res "{1}\skin.rc" || exit /b 1' -f $source, $source),
        ('cl {0} /LD "{1}\skin.cpp" skin.res /Fe:floating.dll /link /INCREMENTAL:NO user32.lib gdi32.lib comctl32.lib || exit /b 1' -f $flags, $source)
    )
    if ($Test) {
        $lines += ('cl {0} "{1}\menu_test.cpp" skin.res /Fe:menu-test.exe /link /INCREMENTAL:NO user32.lib gdi32.lib comctl32.lib || exit /b 1' -f $flags, $source)
    }
    [IO.File]::WriteAllLines($batch, $lines, [Text.Encoding]::Default)
    & python (Join-Path $PSScriptRoot 'cpp\win98\limited_build.py') cmd.exe /d /c $batch
    if ($LASTEXITCODE -ne 0) { throw 'Native skin build failed' }
    $binary = Join-Path $output 'floating.dll'
    $info = (Get-Item -LiteralPath $binary).VersionInfo
    if ($info.FileVersion -ne $version -or $info.ProductVersion -ne $version) { throw 'Skin resource version mismatch' }
    & python (Join-Path $root 'scripts\audit-release.py') $binary --check
    if ($LASTEXITCODE -ne 0) { throw 'Native skin binary audit failed' }
    if ($Test) {
        & (Join-Path $output 'menu-test.exe') $binary
        if ($LASTEXITCODE -ne 0) { throw 'Native menu tests failed' }
    }
    Move-Item -LiteralPath $binary -Destination $artifact -Force
    Write-Output $artifact
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
