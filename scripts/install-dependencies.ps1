# Adapted from mod-playerbots/azerothcore-wotlk .github/workflows/windows_build.yml.
# See sources.lock.json for the pinned upstream revision.
$ErrorActionPreference = 'Stop'

# The windows-latest runner image already ships CMake, Visual Studio
# (with the native desktop workload + MSVC) and OpenSSL, so we only need to
# provide Boost and the MySQL client libraries. We fetch these directly
# instead of via chocolatey, whose pinned packages keep breaking on CI
# (VS detection failures and slproweb removing old OpenSSL installers).

# --- Boost 1.87.0 (msvc-14.3, 64-bit) prebuilt binaries -> C:\local\boost_1_87_0 (BOOST_ROOT) ---
# Go through downloads.sourceforge.net, which hands out an https mirror. The
# master.dl host redirects to a plain-http URL that PowerShell 7.4+ refuses to
# follow, and an installer we then execute shouldn't travel in the clear anyway.
# SourceForge sniffs the user agent: anything starting with Mozilla gets the HTML
# download page instead of the file, so send a non-browser one. Note this is the
# opposite of what the MySQL archives CDN below wants.
if (-not (Test-Path 'C:\local\boost_1_87_0\boost'))
{
    $boostExe = Join-Path $env:RUNNER_TEMP 'boost.exe'
    $boostUrl = 'https://downloads.sourceforge.net/project/boost/boost-binaries/' +
        '1.87.0/boost_1_87_0-msvc-14.3-64.exe'
    # Mirrors drop connections often enough on a 200 MB file to be worth retrying.
    # OperationTimeoutSeconds is the one that catches a mirror going quiet mid-transfer,
    # which would otherwise hang the job until the 6h runner limit. Don't collapse the
    # two into a whole-transfer budget, slow mirrors can need 15+ minutes.
    $boostRequest = @{
        Uri                      = $boostUrl
        OutFile                  = $boostExe
        UserAgent                = 'azerothcore-ci'
        ConnectionTimeoutSeconds = 60
        OperationTimeoutSeconds  = 60
    }
    $boostAttempts = 3
    for ($attempt = 1; $attempt -le $boostAttempts; $attempt++)
    {
        try
        {
            Invoke-WebRequest @boostRequest
            # A mirror can answer 200 with an error page, which doesn't throw. Size-check
            # inside the loop so a bad payload retries onto a different mirror instead of
            # failing the job outright. Each attempt re-rolls the mirror.
            $boostMB = (Get-Item $boostExe).Length / 1MB
            if ($boostMB -lt 50) { throw "got $([int]$boostMB) MB, likely an HTML page." }
            break
        }
        catch
        {
            Write-Host "Boost download attempt $attempt failed: $($_.Exception.Message)"
            if ($attempt -eq $boostAttempts) { throw }
            Start-Sleep -Seconds 15
        }
    }
    Write-Host ("Downloaded Boost installer: {0:N1} MB" -f $boostMB)
    $boostProc = Start-Process -FilePath $boostExe -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/DIR=C:\local\boost_1_87_0' -Wait -PassThru
    if ($boostProc.ExitCode -ne 0) { throw "Boost installer failed with exit code $($boostProc.ExitCode)." }
    # Fail fast if the installer "succeeded" but didn't lay down the expected tree,
    # instead of surfacing a cryptic error later during CMake configuration.
    if (-not (Test-Path 'C:\local\boost_1_87_0\boost\version.hpp')) { throw 'Boost install missing headers at C:\local\boost_1_87_0\boost.' }
    if (-not (Test-Path 'C:\local\boost_1_87_0\lib64-msvc-14.3')) { throw 'Boost install missing prebuilt libs at C:\local\boost_1_87_0\lib64-msvc-14.3.' }
}

# --- MySQL 8.4 client headers + libs -> C:\tools\mysql\current (path the build expects) ---
if (-not (Test-Path 'C:\tools\mysql\current\lib\mysqlclient.lib'))
{
    # Use the archives CDN directly (keeps every version, unlike dev.mysql.com/get which only
    # serves the current GA). A browser user-agent is required or Oracle returns an HTML error page.
    $mysqlZip = Join-Path $env:RUNNER_TEMP 'mysql.zip'
    Invoke-WebRequest -Uri 'https://cdn.mysql.com/archives/mysql-8.4/mysql-8.4.9-winx64.zip' -OutFile $mysqlZip -UserAgent 'Mozilla/5.0'
    $mysqlMB = (Get-Item $mysqlZip).Length / 1MB
    Write-Host ("Downloaded MySQL zip: {0:N1} MB" -f $mysqlMB)
    if ($mysqlMB -lt 50) { throw "MySQL zip download looks wrong ($([int]$mysqlMB) MB)." }
    New-Item -ItemType Directory -Force -Path 'C:\tools\mysql' | Out-Null
    Expand-Archive -Path $mysqlZip -DestinationPath 'C:\tools\mysql' -Force
    if (Test-Path 'C:\tools\mysql\current') { Remove-Item -Recurse -Force 'C:\tools\mysql\current' }
    Rename-Item 'C:\tools\mysql\mysql-8.4.9-winx64' 'C:\tools\mysql\current'
    # Fail fast if the archive layout changed and the client import library isn't
    # where the build expects it, rather than failing later during CMake/link.
    if (-not (Test-Path 'C:\tools\mysql\current\lib\mysqlclient.lib')) { throw 'MySQL client import library missing at C:\tools\mysql\current\lib\mysqlclient.lib.' }
}

# --- OpenSSL: use the copy already installed on the runner image ---
$opensslRoot = @($env:OPENSSL_ROOT_DIR, 'C:\Program Files\OpenSSL', 'C:\Program Files\OpenSSL-Win64') |
    Where-Object { $_ -and (Test-Path (Join-Path $_ 'include\openssl\opensslv.h')) } |
    Select-Object -First 1
if (-not $opensslRoot)
{
    throw 'No OpenSSL development install found on the runner image.'
}
"OPENSSL_ROOT_DIR=$opensslRoot" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
Write-Host "Using OpenSSL at $opensslRoot"
