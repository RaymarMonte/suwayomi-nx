<#
Windows side of the suwayomi-nx test server (WP-02, docs/DECISIONS.md D29).
Java runs on Windows so the Switch can reach the server through the LAN (D11). Normally tools/test-server/test-server.sh calls
this from WSL; it also works on its own from PowerShell. It never touches the owner's real Suwayomi data: everything lives in
<repo>\.test-server\ (git-ignored: jar\ = download cache, fixtures\ = pristine fixture manga, root\ = the server's data).

  test-server.ps1 fetch                  download the pinned jar (server.json) into jar\ and check its SHA-256
  test-server.ps1 start [-Auth <mode>]   start Java in the background, wait until it answers. Restarts it if the auth mode differs.
                                         Modes: none | basic (BASIC_AUTH) | ui_login (JWT, short token expiry from server.json)
  test-server.ps1 stop                   stop it (only the java.exe this script started)
  test-server.ps1 status                 one line of JSON: running, pid, auth, port, LAN addresses, ...
  test-server.ps1 reset                  stop, wipe root\ (only if it holds our marker file), copy fixtures\ into root\local\
  test-server.ps1 paths                  one line of JSON with the folders (the WSL script uses it)
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('fetch', 'start', 'stop', 'status', 'reset', 'paths')][string]$Action,
    [ValidateSet('none', 'basic', 'ui_login')][string]$Auth = 'none'
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is very slow with its progress bar on

$Cfg = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'server.json') | ConvertFrom-Json
$Port = [int]$Cfg.port
# Inside the repo (git-ignored), not in %LOCALAPPDATA%: Claude Code's sandbox hides files that Windows programs write outside
# the project folder from WSL, and WSL writes the fixtures here. In the repo every side sees the same files.
$Base = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\.test-server'))
$JarDir = Join-Path $Base 'jar'
$Jar = Join-Path $JarDir "Suwayomi-Server-$($Cfg.version).jar"
$Fixtures = Join-Path $Base 'fixtures'
$Root = Join-Path $Base 'root'
$Marker = Join-Path $Root '.snx-test-root'   # reset deletes root\ only when this file is there
$OutLog = Join-Path $Base 'server.out.log'
$ErrLog = Join-Path $Base 'server.err.log'
$ReadyTimeoutSec = 90

$AuthModes = @{ none = 'NONE'; basic = 'BASIC_AUTH'; ui_login = 'UI_LOGIN' }

function Get-Jar {
    if (Test-Path -LiteralPath $Jar) { return }
    New-Item -ItemType Directory -Force -Path $JarDir | Out-Null
    $part = "$Jar.part"
    Write-Host "Downloading Suwayomi-Server $($Cfg.version) (~180 MB) ..."
    & curl.exe --fail --location --silent --show-error --retry 3 --output $part $Cfg.jarUrl
    if ($LASTEXITCODE -ne 0) { throw "download failed (curl exit $LASTEXITCODE)" }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $part).Hash.ToLower()
    if ($hash -ne $Cfg.jarSha256) {
        Remove-Item -LiteralPath $part
        throw "SHA-256 mismatch: got $hash, expected $($Cfg.jarSha256)"
    }
    Move-Item -LiteralPath $part -Destination $Jar
    Write-Host "OK: $Jar"
}

# The java.exe this script started ({pid, auth}), or $null. It's found by its command line, which names our root folder,
# so the owner's real Suwayomi server (or any other Java program) is never mistaken for ours.
function Get-ServerProcess {
    foreach ($proc in (Get-CimInstance Win32_Process -Filter "Name = 'java.exe'")) {
        if ($null -eq $proc.CommandLine -or -not $proc.CommandLine.Contains("server.rootDir=$Root`"")) { continue }
        $auth = 'none'
        foreach ($key in $AuthModes.Keys) {
            if ($proc.CommandLine.Contains("server.authMode=$($AuthModes[$key])`"")) { $auth = $key }
        }
        return [pscustomobject]@{ pid = [int]$proc.ProcessId; auth = $auth }
    }
    return $null
}

function Test-PortInUse {
    $listening = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
    return $null -ne $listening
}

# True once anything answers HTTP on the port (401 counts: with auth on, that's the expected answer).
function Test-ServerAnswers {
    try {
        Invoke-WebRequest -UseBasicParsing -TimeoutSec 3 -Uri "http://127.0.0.1:$Port/api/graphql" -Method Post `
            -ContentType 'application/json' -Body '{"query":"{ aboutServer { version } }"}' | Out-Null
        return $true
    } catch [System.Net.WebException] {
        return $null -ne $_.Exception.Response
    }
}

function Show-LogTail {
    foreach ($log in @($OutLog, $ErrLog)) {
        if (Test-Path -LiteralPath $log) {
            Write-Host "--- last lines of $log"
            Get-Content -LiteralPath $log -Tail 25 | ForEach-Object { Write-Host $_ }
        }
    }
}

function Stop-Server {
    $info = Get-ServerProcess
    if ($null -ne $info) {
        Write-Host "Stopping test server (pid $($info.pid)) ..."
        # A hard stop: there's no clean way to send Ctrl+C to a hidden Java process. Suwayomi's H2 database copes with it,
        # and `reset` rebuilds everything anyway.
        Stop-Process -Id $info.pid -Force
        Wait-Process -Id $info.pid -Timeout 15 -ErrorAction SilentlyContinue
    }
}

function Start-Server {
    $info = Get-ServerProcess
    if ($null -ne $info) {
        if ($info.auth -eq $Auth) { Write-Host "Already running (pid $($info.pid), auth $Auth)."; return }
        Write-Host "Running with auth $($info.auth); restarting with auth $Auth."
        Stop-Server
    }
    if (-not (Test-Path -LiteralPath $Marker)) { throw "No test server data in $Root yet: run reset first." }
    if (Test-PortInUse) { throw "Port $Port is already in use by another program." }
    Get-Jar

    $props = [ordered]@{
        'server.rootDir'                    = $Root
        'server.ip'                         = '0.0.0.0'
        'server.port'                       = $Port
        'server.systemTrayEnabled'          = 'false'
        'server.initialOpenInBrowserEnabled' = 'false'
        'server.webUIEnabled'               = 'false'
        'server.webUIUpdateCheckInterval'   = '0'
        'server.kcefEnabled'                = 'false'
        'server.globalUpdateInterval'       = '0'
        'server.backupInterval'             = '0'
        'server.localSourcePath'            = (Join-Path $Root 'local')
        'server.authMode'                   = $AuthModes[$Auth]
        'server.authUsername'               = $Cfg.username
        'server.authPassword'               = $Cfg.password
        'server.jwtTokenExpiry'             = $Cfg.jwtTokenExpiry
    }
    $java = (Get-Command java -CommandType Application | Select-Object -First 1).Source
    $line = "`"$java`""
    foreach ($key in $props.Keys) { $line += " `"-Dsuwayomi.tachidesk.config.$key=$($props[$key])`"" }
    $line += " -jar `"$Jar`" > `"$OutLog`" 2> `"$ErrLog`""

    Write-Host "Starting Suwayomi-Server $($Cfg.version) on port $Port (auth $Auth) ..."
    # Started through WMI (Win32_Process.Create), not Start-Process: a child of this PowerShell would inherit the pipe that
    # WSL reads our output from, and WSL would then wait for Java to end. A hidden cmd.exe writes Java's output to the logs.
    $startup = New-CimInstance -ClassName Win32_ProcessStartup -ClientOnly -Property @{ ShowWindow = [uint16]0 }
    $created = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
        CommandLine = "cmd.exe /d /s /c `"$line`""; CurrentDirectory = $Base; ProcessStartupInformation = $startup
    }
    if ($created.ReturnValue -ne 0) { throw "Starting Java failed (Win32_Process.Create returned $($created.ReturnValue))." }

    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $ReadyTimeoutSec) {
        Start-Sleep -Milliseconds 500
        if ($null -eq (Get-Process -Id $created.ProcessId -ErrorAction SilentlyContinue)) {
            Show-LogTail; throw 'Java exited during start-up (log above).'
        }
        if (Test-ServerAnswers) { Write-Host ("Ready after {0:N1} s." -f $watch.Elapsed.TotalSeconds); return }
    }
    Show-LogTail
    throw "The server didn't answer within $ReadyTimeoutSec s."
}

function Reset-Root {
    Stop-Server
    if (-not (Test-Path -LiteralPath (Join-Path $Fixtures 'Alpha'))) { throw "No fixtures in $Fixtures yet (tools/gen-fixtures writes them)." }
    if (Test-Path -LiteralPath $Root) {
        if (-not (Test-Path -LiteralPath $Marker)) { throw "Refusing to delete $Root because it has no $Marker file." }
        Remove-Item -LiteralPath $Root -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Root | Out-Null
    Set-Content -LiteralPath $Marker -Value 'suwayomi-nx test server data. tools/test-server reset deletes this whole folder.'
    # /E all subfolders, /COPY:DAT + /DCOPY:DAT keep the timestamps (Local Source uses them as chapter dates), quiet output.
    & robocopy.exe $Fixtures (Join-Path $Root 'local') /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed (exit $LASTEXITCODE)" }
    Write-Host "Reset: fresh $Root with the fixture manga in local\."
}

function Get-LanAddresses {
    # IPv4 addresses of connected adapters that have a default gateway: the LAN, not WSL's virtual switch.
    $addrs = @()
    foreach ($ipc in (Get-NetIPConfiguration | Where-Object { $null -ne $_.IPv4DefaultGateway -and $_.NetAdapter.Status -eq 'Up' })) {
        foreach ($a in $ipc.IPv4Address) { $addrs += $a.IPAddress }
    }
    return , $addrs
}

switch ($Action) {
    'fetch' { Get-Jar }
    'start' { Start-Server }
    'stop' { Stop-Server }
    'reset' { Reset-Root }
    'status' {
        $info = Get-ServerProcess
        $status = [ordered]@{
            running = $null -ne $info
            answers = $false
            pid     = $null
            auth    = $null
            port    = $Port
            version = $Cfg.version
            lan     = Get-LanAddresses
            root    = $Root
        }
        if ($null -ne $info) {
            $status.pid = $info.pid; $status.auth = $info.auth; $status.answers = Test-ServerAnswers
        }
        $status | ConvertTo-Json -Compress
    }
    'paths' {
        [ordered]@{ base = $Base; jar = $Jar; fixtures = $Fixtures; root = $Root; outLog = $OutLog; errLog = $ErrLog } |
            ConvertTo-Json -Compress
    }
}
