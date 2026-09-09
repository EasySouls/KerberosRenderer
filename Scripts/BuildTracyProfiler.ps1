param(
    [string]$BuildDirectory = "out\build\tracy-profiler",
    [string]$Generator = "Visual Studio 18 2026",
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release"
)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$tracyRoot = Join-Path $repoRoot "KerberosEngine\ThirdParty\tracy"
$sourceDirectory = Join-Path $tracyRoot "profiler"
$buildPath = Join-Path $repoRoot $BuildDirectory

cmake -S $sourceDirectory -B $buildPath -G $Generator
if ($LASTEXITCODE -ne 0) {
    throw "Failed to configure the Tracy profiler."
}

cmake --build $buildPath --config $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Failed to build the Tracy profiler."
}
