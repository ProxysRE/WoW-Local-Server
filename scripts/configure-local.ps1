# Run inside the extracted Server folder after installing MySQL 8.x.
param(
    [string]$DatabaseUser = 'acore',
    [Parameter(Mandatory=$true)][string]$DatabasePassword,
    [ValidateRange(50,2000)][int]$Bots = 500
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if ($DatabaseUser -match '[;"\r\n]' -or $DatabasePassword -match '[;"\r\n]') {
    throw 'The AzerothCore connection string cannot contain semicolons, quotes or newlines.'
}
function Set-ConfValue([string]$Path, [string]$Key, [string]$Value) {
    $content = [IO.File]::ReadAllText($Path)
    $pattern = '(?m)^\s*' + [regex]::Escape($Key) + '\s*=.*$'
    $line = "$Key = $Value"
    if ([regex]::IsMatch($content, $pattern)) {
        $content = [regex]::Replace($content, $pattern, [System.Text.RegularExpressions.MatchEvaluator]{param($m) $line})
    } else { $content += "`n$line`n" }
    [IO.File]::WriteAllText($Path, $content, [Text.UTF8Encoding]::new($false))
}
Get-ChildItem configs -Recurse -Filter '*.conf.dist' | ForEach-Object {
    $destination = $_.FullName.Substring(0, $_.FullName.Length - 5)
    if (-not (Test-Path $destination)) { Copy-Item $_.FullName $destination }
}
$dbPrefix = "127.0.0.1;3306;$DatabaseUser;$DatabasePassword;"
Set-ConfValue configs/authserver.conf LoginDatabaseInfo ('"' + $dbPrefix + 'acore_auth"')
foreach ($database in @(@('LoginDatabaseInfo','acore_auth'), @('WorldDatabaseInfo','acore_world'), @('CharacterDatabaseInfo','acore_characters'))) {
    Set-ConfValue configs/worldserver.conf $database[0] ('"' + $dbPrefix + $database[1] + '"')
}
foreach ($path in @('configs/authserver.conf','configs/worldserver.conf')) {
    Set-ConfValue $path SourceDirectory '"Source"'
    Set-ConfValue $path MySQLExecutable '"mysql.exe"'
}
Set-ConfValue configs/worldserver.conf DataDir '"Data"'
Set-ConfValue configs/worldserver.conf EnablePlayerSettings '1'
Set-ConfValue configs/worldserver.conf DBC.EnforceItemAttributes '0'
Set-ConfValue configs/worldserver.conf MapUpdate.Threads '4'
Set-ConfValue configs/worldserver.conf RealmID '1'
$botConfig = 'configs/modules/playerbots.conf'
if (-not (Test-Path $botConfig)) { throw "Missing $botConfig" }
Set-ConfValue $botConfig PlayerbotsDatabaseInfo ('"' + $dbPrefix + 'acore_playerbots"')
Set-ConfValue $botConfig Playerbots.MinRandomBots "$Bots"
Set-ConfValue $botConfig Playerbots.MaxRandomBots "$Bots"
$arenaConfig = 'configs/modules/1v1arena.conf'
if (Test-Path $arenaConfig) {
    Set-ConfValue $arenaConfig Arena1v1.Playerbots.Enable '1'
    Set-ConfValue $arenaConfig Arena1v1.Playerbots.MaxItemLevelGap '15'
}
New-Item -ItemType Directory -Force Data | Out-Null
Write-Host 'Configuration prepared. Read docs/SETUP_RU.md before first startup.'
