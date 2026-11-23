<#
Windows side of the suwayomi-nx test server (WP-02, docs/DECISIONS.md D29).
Java runs on Windows so the Switch can reach the server through the LAN (D11). Normally tools/test-server/test-server.sh calls
this from WSL; it also works on its own from PowerShell. It never touches the owner's real Suwayomi data: everything lives in
<repo>\.test-server\ (git-ignored: jar\ = download cache, fixtures\ = pristine fixture manga, root\ = the server's data).

  test-server.ps1 fetch                  download the pinned jar (server.json) into jar\ and check its SHA-256
  test-server.ps1 start [-Auth <mode>]   start the test site (jwebserver, port 4569, loopback only) and Java in the background,
                                         wait until both answer. Restarts Java if the auth mode differs.
                                         Modes: none | basic (BASIC_AUTH) | ui_login (JWT, short token expiry from server.json)
  test-server.ps1 stop                   stop both (only the java.exe and jwebserver.exe this script started)
  test-server.ps1 status                 one line of JSON: running, pid, auth, port, LAN addresses, the test site, ...
  test-server.ps1 reset                  stop, wipe root\ (only if it holds our marker file), copy fixtures\ into root\local\
                                         (root\tmp\ is Java's temp folder, so the server's cover cache goes with it)
  test-server.ps1 build-source           compile the test extension (tools\test-source, WP-23) and pack one jar per extension and
                                         version into web\store\ (the list: web\store\build.json, written by gen-test-source.ts)
  test-server.ps1 paths                  one line of JSON with the folders (the WSL script uses it)
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('fetch', 'start', 'stop', 'status', 'reset', 'build-source', 'paths')][string]$Action,
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
# The test source (WP-23, D55): the store and test site, served by the JDK's jwebserver on the PC's loopback only (the
# Switch never talks to it: covers and pages reach clients through the server). Written by tools/gen-fixtures/gen-test-source.ts.
$WebPort = [int]$Cfg.webPort
$Web = Join-Path $Base 'web'
$Store = Join-Path $Web 'store'
$WebOutLog = Join-Path $Base 'web.out.log'
$WebErrLog = Join-Path $Base 'web.err.log'
$SourceDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\test-source'))
$SourceBuild = Join-Path $Base 'source-build'   # javac's output and the jar staging folders; rebuilt every time

$AuthModes = @{ none = 'NONE'; basic = 'BASIC_AUTH'; ui_login = 'UI_LOGIN' }

# A JDK tool (javac, jar, jwebserver) from the PATH, else from the folder of the java.exe that runs the server.
function Get-JdkTool([string]$name) {
    $cmd = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $cmd) { return $cmd.Source }
    $java = Get-Command java -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $java) {
        $beside = Join-Path (Split-Path -Parent $java.Source) "$name.exe"
        if (Test-Path -LiteralPath $beside) { return $beside }
    }
    throw "$name.exe not found. The test source needs a full JDK (Temurin 21, docs/SETUP.md), not only a Java runtime (JRE)."
}

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

function Test-PortInUse([int]$port = $Port) {
    $listening = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
    return $null -ne $listening
}

# The jwebserver.exe this script started (its pid), or $null. Found by its command line, which names our web folder.
function Get-WebProcess {
    foreach ($proc in (Get-CimInstance Win32_Process -Filter "Name = 'jwebserver.exe'")) {
        if ($null -ne $proc.CommandLine -and $proc.CommandLine.Contains("-d `"$Web`"")) { return [int]$proc.ProcessId }
    }
    return $null
}

# True once the test site sends the store index.
function Test-WebAnswers {
    try {
        Invoke-WebRequest -UseBasicParsing -TimeoutSec 3 -Uri "http://127.0.0.1:$WebPort/store/index.json" | Out-Null
        return $true
    } catch {
        return $false
    }
}

# Starts a hidden command through WMI (see Start-Server for why) and returns its process id.
function Start-Hidden([string]$commandLine) {
    $startup = New-CimInstance -ClassName Win32_ProcessStartup -ClientOnly -Property @{ ShowWindow = [uint16]0 }
    $created = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
        CommandLine = "cmd.exe /d /s /c `"$commandLine`""; CurrentDirectory = $Base; ProcessStartupInformation = $startup
    }
    if ($created.ReturnValue -ne 0) { throw "Starting '$commandLine' failed (Win32_Process.Create returned $($created.ReturnValue))." }
    return $created.ProcessId
}

function Start-Web {
    if ($null -ne (Get-WebProcess)) { return }
    if (-not (Test-Path -LiteralPath (Join-Path $Store 'index.json'))) { throw "No test site in $Web yet: run test-server.sh reset (it generates it)." }
    if (Test-PortInUse $WebPort) { throw "Port $WebPort (the test site) is already in use by another program." }
    $jweb = Get-JdkTool 'jwebserver'
    Write-Host "Starting the test site on 127.0.0.1:$WebPort ..."
    $shell = Start-Hidden "`"$jweb`" -b 127.0.0.1 -p $WebPort -d `"$Web`" -o info > `"$WebOutLog`" 2> `"$WebErrLog`""
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt 30) {
        Start-Sleep -Milliseconds 250
        if (Test-WebAnswers) { return }
        if ($null -eq (Get-Process -Id $shell -ErrorAction SilentlyContinue)) { break }
    }
    foreach ($log in @($WebOutLog, $WebErrLog)) {
        if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 15 | ForEach-Object { Write-Host $_ } }
    }
    throw "The test site (jwebserver) didn't answer on port $WebPort (log above)."
}

function Stop-Web {
    $webPid = Get-WebProcess
    if ($null -ne $webPid) {
        Write-Host "Stopping the test site (pid $webPid) ..."
        Stop-Process -Id $webPid -Force
        Wait-Process -Id $webPid -Timeout 15 -ErrorAction SilentlyContinue
    }
}

# Compiles tools\test-source against the pinned server jar (on the class path only, never packed in) and packs one jar per
# web\store\build.json entry: all our classes + an AndroidManifest.xml from the template. Text files are written without a
# byte-order mark (PowerShell 5.1's Set-Content would add one).
function Build-Source {
    $listFile = Join-Path $Store 'build.json'
    if (-not (Test-Path -LiteralPath $listFile)) { throw "No $listFile yet (tools/gen-fixtures/gen-test-source.ts writes it)." }
    Get-Jar
    $javac = Get-JdkTool 'javac'
    $jarTool = Get-JdkTool 'jar'
    $watch = [Diagnostics.Stopwatch]::StartNew()
    if (Test-Path -LiteralPath $SourceBuild) { Remove-Item -LiteralPath $SourceBuild -Recurse -Force }
    $classes = Join-Path $SourceBuild 'classes'
    New-Item -ItemType Directory -Force -Path $classes | Out-Null
    $sources = @(Get-ChildItem -LiteralPath (Join-Path $SourceDir 'src') -Recurse -Filter '*.java' | ForEach-Object { $_.FullName })
    # -nowarn: the only warning is "getFilterList() overrides a bridge method" (a Kotlin detail, harmless).
    & $javac --release 21 -encoding UTF-8 -nowarn -cp $Jar -d $classes @sources
    if ($LASTEXITCODE -ne 0) { throw "javac failed (exit $LASTEXITCODE): see the errors above." }

    $template = [IO.File]::ReadAllText((Join-Path $SourceDir 'AndroidManifest.template.xml'))
    $noBom = New-Object Text.UTF8Encoding $false
    # A Last-Modified date in the future: OkHttp then never treats a cached copy as fresh by itself and asks again, so a
    # rebuilt jar is never served from the server's HTTP cache. Each build gets a later date than the last one.
    $future = (Get-Date).ToUniversalTime().AddYears(100)
    $built = 0
    foreach ($entry in (Get-Content -Raw -LiteralPath $listFile | ConvertFrom-Json)) {
        $stage = Join-Path $SourceBuild $entry.jar
        Copy-Item -LiteralPath $classes -Destination $stage -Recurse
        $fields = [ordered]@{ '@PACKAGE@' = $entry.package; '@VERSION_CODE@' = [string]$entry.versionCode
            '@VERSION_NAME@' = $entry.versionName; '@NAME@' = $entry.name; '@CONTENT_WARNING@' = [string]$entry.contentWarning }
        $manifest = $template
        foreach ($key in $fields.Keys) { $manifest = $manifest.Replace($key, $fields[$key]) }
        [IO.File]::WriteAllText((Join-Path $stage 'AndroidManifest.xml'), $manifest, $noBom)
        $target = Join-Path $Store $entry.jar
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
        & $jarTool --create --file $target --date 2026-01-01T00:00:00Z -C $stage .
        if ($LASTEXITCODE -ne 0) { throw "jar failed for $($entry.jar) (exit $LASTEXITCODE)." }
        (Get-Item -LiteralPath $target).LastWriteTimeUtc = $future
        $built++
    }
    Write-Host ("Built {0} extension jars into {1} in {2:N1} s." -f $built, $Store, $watch.Elapsed.TotalSeconds)
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
    Start-Web   # first: the seed (and the server's extension store) need it
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
    # IS-006: the server keeps the covers of manga outside the library in <java temp>\Tachidesk\thumbnails\<manga id>.jpg.
    # In Windows' %TEMP% that cache outlived every reset, and after a reset the same ids go to other manga: browse showed
    # wrong covers. Inside root\ a reset wipes it with everything else (and it stays apart from any other Suwayomi).
    $tmp = Join-Path $Root 'tmp'
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    $line += " `"-Djava.io.tmpdir=$tmp`""
    $line += " -jar `"$Jar`" > `"$OutLog`" 2> `"$ErrLog`""

    Write-Host "Starting Suwayomi-Server $($Cfg.version) on port $Port (auth $Auth) ..."
    # Started through WMI (Win32_Process.Create), not Start-Process: a child of this PowerShell would inherit the pipe that
    # WSL reads our output from, and WSL would then wait for Java to end. A hidden cmd.exe writes Java's output to the logs.
    $shell = Start-Hidden $line

    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $ReadyTimeoutSec) {
        Start-Sleep -Milliseconds 500
        if ($null -eq (Get-Process -Id $shell -ErrorAction SilentlyContinue)) {
            Show-LogTail; throw 'Java exited during start-up (log above).'
        }
        if (Test-ServerAnswers) { Write-Host ("Ready after {0:N1} s." -f $watch.Elapsed.TotalSeconds); return }
    }
    Show-LogTail
    throw "The server didn't answer within $ReadyTimeoutSec s."
}

function Reset-Root {
    Stop-Server
    Stop-Web
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
    'stop' { Stop-Server; Stop-Web }
    'reset' { Reset-Root }
    'build-source' { Build-Source }
    'status' {
        $info = Get-ServerProcess
        $webPid = Get-WebProcess
        $status = [ordered]@{
            running = $null -ne $info
            answers = $false
            pid     = $null
            auth    = $null
            port    = $Port
            version = $Cfg.version
            lan     = Get-LanAddresses
            root    = $Root
            web     = [ordered]@{ running = $null -ne $webPid; answers = $false; pid = $webPid; port = $WebPort
                storeUrl = "http://127.0.0.1:$WebPort/store/index.json" }
        }
        if ($null -ne $info) {
            $status.pid = $info.pid; $status.auth = $info.auth; $status.answers = Test-ServerAnswers
        }
        if ($null -ne $webPid) { $status.web.answers = Test-WebAnswers }
        $status | ConvertTo-Json -Compress
    }
    'paths' {
        [ordered]@{ base = $Base; jar = $Jar; fixtures = $Fixtures; root = $Root; outLog = $OutLog; errLog = $ErrLog; web = $Web } |
            ConvertTo-Json -Compress
    }
}
