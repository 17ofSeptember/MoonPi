param([string]$BuildDirectory = 'build-native', [string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if (-not (Test-Path frontend/dist/index.html)) { throw 'Build the frontend first.' }
$releaseRoot = [IO.Path]::GetFullPath((Join-Path (Get-Location) 'release'))
$packageId = [Guid]::NewGuid().ToString('N')
$stageRoot = Join-Path $releaseRoot ('.staging-' + $packageId)
$packageRoot = Join-Path $stageRoot 'MoonPi'
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
cmake --install $BuildDirectory --config $Configuration --prefix $packageRoot
if ($LASTEXITCODE -ne 0) { throw 'Installation failed' }
$manifest = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($packageRoot.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $packageRoot 'manifest.json') -Encoding UTF8
$stagedZip = Join-Path $stageRoot 'MoonPi-windows-x64.zip'
Compress-Archive -LiteralPath $packageRoot -DestinationPath $stagedZip
$installedRoot = Join-Path $releaseRoot 'MoonPi'
if (Test-Path -LiteralPath $installedRoot) {
    # Preserve any locally saved projects in the prior extracted build.
    try {
        Move-Item -LiteralPath $installedRoot -Destination (Join-Path $releaseRoot ('MoonPi-previous-' + $packageId)) -ErrorAction Stop
    } catch {
        # A running executable or open directory may prevent renaming on Windows.
        # Leave the existing installation intact; publish this build beside it.
        $installedRoot = Join-Path $releaseRoot ('MoonPi-build-' + $packageId)
        Write-Warning ('Existing release folder is in use. New extracted build: ' + $installedRoot)
    }
}
Move-Item -LiteralPath $packageRoot -Destination $installedRoot
$finalZip = Join-Path $releaseRoot 'MoonPi-windows-x64.zip'
Move-Item -LiteralPath $stagedZip -Destination $finalZip -Force
$zipHash = (Get-FileHash -LiteralPath $finalZip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath ($finalZip + '.sha256') -Value ($zipHash + '  MoonPi-windows-x64.zip') -Encoding ASCII
# Only remove the now-empty directory; no recursive deletion of user data.
Remove-Item -LiteralPath $stageRoot
Write-Host 'Created release/MoonPi-windows-x64.zip and SHA-256 checksum'
