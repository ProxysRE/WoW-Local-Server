# Run from the extracted update archive with Windows PowerShell 5.1 or later.
param(
    [Parameter(Mandatory = $true)][string]$ServerPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$targetRoot = (Resolve-Path -LiteralPath $ServerPath).Path
$payload = Join-Path $PSScriptRoot 'Server'
if (-not (Test-Path -LiteralPath (Join-Path $payload 'worldserver.exe'))) {
    throw 'Extract the complete update archive before running this script.'
}
foreach ($required in @('configs/worldserver.conf', 'configs/authserver.conf', 'configs/modules/playerbots.conf', 'Source/data/sql')) {
    if (-not (Test-Path -LiteralPath (Join-Path $targetRoot $required))) {
        throw "Existing server file or directory is missing: $required"
    }
}
if ((Resolve-Path -LiteralPath $payload).Path -eq $targetRoot) {
    throw 'The update payload and destination must be different directories.'
}
$running = @(Get-Process -Name worldserver, authserver -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw 'Stop worldserver and authserver before installing the update. Leave MySQL running.'
}
$backupRoot = Join-Path $targetRoot ('UpdateBackups/' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $targetRoot 'configs') -Destination (Join-Path $backupRoot 'configs') -Recurse
$files = @(Get-ChildItem -LiteralPath $payload -Recurse -File)
foreach ($file in $files) {
    $relative = $file.FullName.Substring($payload.Length).TrimStart([char[]]'\/')
    $destination = Join-Path $targetRoot $relative
    # Preserve every active config, including a previously customized AutoBalance profile.
    if ($file.Extension -eq '.conf' -and (Test-Path -LiteralPath $destination)) {
        Write-Host "Keeping config: $relative"
        continue
    }
    if (Test-Path -LiteralPath $destination) {
        $backupFile = Join-Path $backupRoot ('Files/' + $relative)
        New-Item -ItemType Directory -Path (Split-Path -Parent $backupFile) -Force | Out-Null
        Copy-Item -LiteralPath $destination -Destination $backupFile
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination
}
Write-Host "Files installed. File/config backup: $backupRoot"
Write-Host 'This is NOT a database backup. SQL updates have NOT been executed by this script.'
Write-Host 'After backing up all four databases, start authserver and worldserver normally.'
Write-Host 'Let the built-in updater apply migrations; do not manually import full SQL dumps.'
