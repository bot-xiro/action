param(
    [string]$BuildDir = "build",
    [switch]$Clean = $false
)

$ErrorActionPreference = "Stop"

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item $BuildDir -Recurse -Force
}

cmake -B $BuildDir `
    -DCMAKE_SYSTEM_NAME=Linux `
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DKMSSINK_TEST=ON `
    native/bilistream

cmake --build $BuildDir --config Release --parallel

Write-Host "Output:" (Resolve-Path "$BuildDir/libjsapi_bilistream.so")
