param(
    [string]$BuildDirectory = "out\build\tracy-profiler",
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release"
)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$profiler = Join-Path $repoRoot "$BuildDirectory\$Configuration\tracy-profiler.exe"

if (-not (Test-Path $profiler)) {
    throw "Tracy profiler was not found at '$profiler'. Run BuildTracyProfiler.ps1 first."
}

& $profiler
