$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('wow-update-test-' + [Guid]::NewGuid().ToString('N'))
function Write-Fixture([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Text)
}
function Assert-Content([string]$Path, [string]$Expected) {
    if ([IO.File]::ReadAllText($Path) -ne $Expected) { throw "Unexpected content: $Path" }
}
try {
    $archive = Join-Path $root 'extracted update'
    $server = Join-Path $root 'existing server'
    New-Item -ItemType Directory -Path $archive -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../scripts/install-core-update.ps1') -Destination (Join-Path $archive 'Install-Update.ps1')
    foreach ($name in @('worldserver', 'authserver')) {
        Write-Fixture (Join-Path $server "configs/$name.conf") "keep-$name"
        Write-Fixture (Join-Path $server "$name.exe") "old-$name"
        Write-Fixture (Join-Path $archive "Server/$name.exe") "new-$name"
    }
    Write-Fixture (Join-Path $server 'configs/modules/playerbots.conf') 'keep-playerbots'
    Write-Fixture (Join-Path $server 'configs/modules/AutoBalance.conf') 'keep-custom-balance'
    Write-Fixture (Join-Path $server 'Source/data/sql/updates/existing.sql') 'existing-sql'
    Write-Fixture (Join-Path $archive 'Server/configs/modules/AutoBalance.conf') 'new-balance'
    Write-Fixture (Join-Path $archive 'Server/Source/data/sql/updates/new.sql') 'new-sql'
    & (Join-Path $archive 'Install-Update.ps1') -ServerPath $server
    Assert-Content (Join-Path $server 'configs/worldserver.conf') 'keep-worldserver'
    Assert-Content (Join-Path $server 'configs/authserver.conf') 'keep-authserver'
    Assert-Content (Join-Path $server 'configs/modules/playerbots.conf') 'keep-playerbots'
    Assert-Content (Join-Path $server 'configs/modules/AutoBalance.conf') 'keep-custom-balance'
    Assert-Content (Join-Path $server 'worldserver.exe') 'new-worldserver'
    Assert-Content (Join-Path $server 'authserver.exe') 'new-authserver'
    Assert-Content (Join-Path $server 'Source/data/sql/updates/existing.sql') 'existing-sql'
    Assert-Content (Join-Path $server 'Source/data/sql/updates/new.sql') 'new-sql'
    $backups = @(Get-ChildItem -LiteralPath (Join-Path $server 'UpdateBackups') -Directory)
    if ($backups.Count -ne 1) { throw 'Expected one file backup' }
    Assert-Content (Join-Path $backups[0].FullName 'Files/worldserver.exe') 'old-worldserver'
    Assert-Content (Join-Path $backups[0].FullName 'configs/modules/playerbots.conf') 'keep-playerbots'
    Remove-Item -LiteralPath (Join-Path $server 'configs/modules/AutoBalance.conf')
    & (Join-Path $archive 'Install-Update.ps1') -ServerPath $server
    Assert-Content (Join-Path $server 'configs/modules/AutoBalance.conf') 'new-balance'
    Write-Host 'Installer: configs preserved, SQL merged, file backups verified, missing module config added'
}
finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
