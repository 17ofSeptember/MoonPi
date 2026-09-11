param([string]$Configuration = 'Release', [string]$BuildDirectory = 'build-native')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
function Check-Exit { if ($LASTEXITCODE -ne 0) { throw "Build step failed with exit code $LASTEXITCODE" } }
npm.cmd ci --prefix frontend
Check-Exit
npm.cmd run build --prefix frontend
Check-Exit
cmake -S . -B $BuildDirectory
Check-Exit
cmake --build $BuildDirectory --config $Configuration --parallel 4
Check-Exit
ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
Check-Exit
Write-Host "Moon Pi built. Run: ./$BuildDirectory/$Configuration/moonpi.exe --simulation --web frontend/dist"
