[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Start", "Mark", "Stop", "Status", "ClearMarkers", "ClearDetections")]
    [string]$Action,
    [string]$SessionName = "occ-live",
    [string]$Client = "RoF2",
    [string]$TsharkPath = "",
    [string]$Interface = "Ethernet",
    [string]$CaptureFilter = "udp",
    [int]$DurationSec = 0,
    [string]$Label = "marker",
    [string]$Note = ""
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $PSCommandPath
$occRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot ".."))
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot "..\..\.."))
$workspaceRoot = Split-Path -Parent $repoRoot
$capturesRoot = Join-Path $workspaceRoot ".codex\captures\sessions"
$liveSessionPath = Join-Path $occRoot "data\live-session.json"
$monitorScript = Join-Path $scriptRoot "monitor_occ_live.py"

function Resolve-TsharkPath {
    param([string]$PreferredPath)

    if ($PreferredPath -and (Test-Path -LiteralPath $PreferredPath)) {
        return (Resolve-Path -LiteralPath $PreferredPath).ProviderPath
    }

    $fromPath = Get-Command tshark -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($fromPath -and $fromPath.Source -and (Test-Path -LiteralPath $fromPath.Source)) {
        return $fromPath.Source
    }

    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles "Wireshark\tshark.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Wireshark\tshark.exe")
    )) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    throw "tshark.exe was not found. Run '$repoRoot\utils\occ\start_occ.ps1' for guided setup."
}

function Resolve-PythonCommand {
    $python = Get-Command python -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($python -and $python.Source -and (Test-Path -LiteralPath $python.Source)) {
        return @{
            FilePath = $python.Source
            Prefix = @()
        }
    }

    $pyLauncher = Get-Command py -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($pyLauncher -and $pyLauncher.Source -and (Test-Path -LiteralPath $pyLauncher.Source)) {
        return @{
            FilePath = $pyLauncher.Source
            Prefix = @("-3")
        }
    }

    throw "Python 3 is required to run OCC live session monitoring."
}

function Format-Argument {
    param([string]$Value)

    if ($null -eq $Value) {
        return '""'
    }

    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }

    return $Value
}

function Resolve-TsharkInterface {
    param(
        [string]$ExePath,
        [string]$RequestedInterface
    )

    if ([string]::IsNullOrWhiteSpace($RequestedInterface)) {
        $RequestedInterface = "Ethernet"
    }

    if ($RequestedInterface -ieq "loopback") {
        return "\Device\NPF_Loopback"
    }

    if ($RequestedInterface -match "^\d+$") {
        return $RequestedInterface
    }

    $interfaces = & $ExePath -D 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to list tshark interfaces."
    }

    $exactMatches = @()
    $partialMatches = @()

    foreach ($line in $interfaces) {
        if ($line -match "^(?<Index>\d+)\.\s+(?<Device>\S+)\s+\((?<Label>.*)\)$") {
            $candidate = [PSCustomObject]@{
                Device = $Matches.Device
                Label = $Matches.Label
            }

            if ($Matches.Device -ieq $RequestedInterface -or $Matches.Label -ieq $RequestedInterface) {
                $exactMatches += $candidate
                continue
            }

            if ($Matches.Label -like "*$RequestedInterface*") {
                $partialMatches += $candidate
            }
        }
    }

    if ($exactMatches.Count -ge 1) {
        return $exactMatches[0].Device
    }

    if ($partialMatches.Count -eq 1) {
        return $partialMatches[0].Device
    }

    if ($partialMatches.Count -gt 1) {
        $labels = ($partialMatches | ForEach-Object { $_.Label }) -join ", "
        throw "Interface '$RequestedInterface' is ambiguous. Matching labels: $labels"
    }

    throw "Could not resolve interface '$RequestedInterface'."
}

function Get-SessionPaths {
    param([string]$Name)

    $root = Join-Path $capturesRoot $Name
    return @{
        Root = $root
        Meta = Join-Path $root "session.json"
        Markers = Join-Path $root "markers.jsonl"
        Capture = Join-Path $root "$Name.pcapng"
        StdOut = Join-Path $root "tshark.stdout.log"
        StdErr = Join-Path $root "tshark.stderr.log"
        Detections = Join-Path $root "detections.json"
        MonitorStdOut = Join-Path $root "monitor.stdout.log"
        MonitorStdErr = Join-Path $root "monitor.stderr.log"
    }
}

function Read-SessionMeta {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }

    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Write-SessionMeta {
    param(
        [string]$Path,
        [object]$Data
    )

    $json = $Data | ConvertTo-Json -Depth 8
    Set-Content -LiteralPath $Path -Value $json -Encoding UTF8
}

function Read-SessionMarkers {
    param([string]$Path)

    $markers = @()
    if (-not (Test-Path -LiteralPath $Path)) {
        return $markers
    }

    foreach ($line in Get-Content -LiteralPath $Path) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        try {
            $markers += ($line | ConvertFrom-Json)
        } catch {
            continue
        }
    }

    return $markers
}

function Read-SessionDetections {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return @{
            activityCount = 0
            activity = @()
            detectionCount = 0
            detections = @()
            lastFrameNumber = 0
            syncedUtc = ""
        }
    }

    try {
        $payload = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
        $activityCount = 0
        $activity = @()
        $detectionCount = 0
        $lastFrameNumber = 0
        $syncedUtc = ""
        $detections = @()

        if ($payload.PSObject.Properties.Name -contains "activityCount") {
            $activityCount = [int]$payload.activityCount
        }
        if ($payload.PSObject.Properties.Name -contains "activity") {
            $activity = @($payload.activity)
        }
        if ($payload.PSObject.Properties.Name -contains "detectionCount") {
            $detectionCount = [int]$payload.detectionCount
        }
        if ($payload.PSObject.Properties.Name -contains "lastFrameNumber") {
            $lastFrameNumber = [int]$payload.lastFrameNumber
        }
        if ($payload.PSObject.Properties.Name -contains "syncedUtc") {
            $syncedUtc = [string]$payload.syncedUtc
        }
        if ($payload.PSObject.Properties.Name -contains "detections") {
            $detections = @($payload.detections)
        }

        return @{
            activityCount = $activityCount
            activity = $activity
            detectionCount = $detectionCount
            detections = $detections
            lastFrameNumber = $lastFrameNumber
            syncedUtc = $syncedUtc
        }
    } catch {
        return @{
            activityCount = 0
            activity = @()
            detectionCount = 0
            detections = @()
            lastFrameNumber = 0
            syncedUtc = ""
        }
    }
}

function Test-SessionProcessRunning {
    param([object]$Meta)

    if (-not $Meta -or -not $Meta.ProcessId) {
        return $false
    }

    $process = Get-Process -Id $Meta.ProcessId -ErrorAction SilentlyContinue
    return $null -ne $process
}

function Export-OccSessionState {
    param(
        [string]$SessionName,
        [hashtable]$Paths,
        [object]$Meta
    )

    $occDir = Split-Path -Parent $liveSessionPath
    if (-not (Test-Path -LiteralPath $occDir)) {
        return
    }

    $running = Test-SessionProcessRunning -Meta $Meta
    $markers = @(Read-SessionMarkers -Path $Paths.Markers)
    $detections = Read-SessionDetections -Path $Paths.Detections
    $status = if (-not $Meta) {
        "idle"
    } elseif ($running) {
        "running"
    } elseif (($Meta.PSObject.Properties.Name -contains "StoppedUtc") -or (Test-Path -LiteralPath $Paths.Capture)) {
        "stopped"
    } else {
        "idle"
    }

    $markerPayload = @()
    for ($i = 0; $i -lt $markers.Count; $i++) {
        $marker = $markers[$i]
        if (-not $marker) {
            continue
        }

        $markerPayload += [PSCustomObject]@{
            Id = "{0}|{1}|{2}" -f $marker.MarkedUtc, $marker.Label, $i
            Index = $i
            Label = $marker.Label
            Note = $marker.Note
            MarkedUtc = $marker.MarkedUtc
            MarkedEpoch = $marker.MarkedEpoch
        }
    }

    $payload = [PSCustomObject]@{
        status = $status
        sessionName = if ($Meta) { $Meta.SessionName } else { $SessionName }
        interface = if ($Meta) { $Meta.Interface } else { "" }
        resolvedInterface = if ($Meta) { $Meta.ResolvedInterface } else { "" }
        captureFilter = if ($Meta) { $Meta.CaptureFilter } else { "" }
        client = if ($Meta) { $Meta.Client } else { "" }
        capturePath = if ($Meta) { $Meta.CapturePath } else { "" }
        markersPath = if ($Meta) { $Meta.MarkersPath } else { "" }
        detectionsPath = if ($Meta) { $Meta.DetectionsPath } else { "" }
        startedUtc = if ($Meta) { $Meta.StartedUtc } else { "" }
        stoppedUtc = if ($Meta -and ($Meta.PSObject.Properties.Name -contains "StoppedUtc")) { $Meta.StoppedUtc } else { "" }
        markerCount = $markerPayload.Count
        markers = $markerPayload
        activityCount = $detections.activityCount
        activity = @($detections.activity)
        detectionCount = $detections.detectionCount
        detections = @($detections.detections)
        lastFrameNumber = $detections.lastFrameNumber
        syncedUtc = if ($detections.syncedUtc) { $detections.syncedUtc } else { [DateTime]::UtcNow.ToString("o") }
    }

    $payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $liveSessionPath -Encoding UTF8
}

$resolvedTsharkPath = Resolve-TsharkPath -PreferredPath $TsharkPath
$pythonCommand = Resolve-PythonCommand

if (-not (Test-Path -LiteralPath $monitorScript)) {
    throw "Missing OCC monitor script at '$monitorScript'."
}

$paths = Get-SessionPaths -Name $SessionName
New-Item -ItemType Directory -Force -Path $paths.Root | Out-Null
$meta = Read-SessionMeta -Path $paths.Meta

switch ($Action) {
    "Start" {
        if ($meta -and (Test-SessionProcessRunning -Meta $meta)) {
            throw "Session '$SessionName' is already running with PID $($meta.ProcessId)."
        }

        $resolvedInterface = Resolve-TsharkInterface -ExePath $resolvedTsharkPath -RequestedInterface $Interface
        Remove-Item -LiteralPath $paths.Capture, $paths.Markers, $paths.StdOut, $paths.StdErr, $paths.Detections, $paths.MonitorStdOut, $paths.MonitorStdErr -ErrorAction SilentlyContinue

        $arguments = @(
            "-i", $resolvedInterface,
            "-w", $paths.Capture,
            "-q"
        )

        if (-not [string]::IsNullOrWhiteSpace($CaptureFilter)) {
            $arguments += @("-f", $CaptureFilter)
        }

        if ($DurationSec -gt 0) {
            $arguments += @("-a", "duration:$DurationSec")
        }

        $process = Start-Process -FilePath $resolvedTsharkPath -ArgumentList $arguments -RedirectStandardOutput $paths.StdOut -RedirectStandardError $paths.StdErr -PassThru -WindowStyle Hidden
        Start-Sleep -Milliseconds 300

        $meta = [PSCustomObject]@{
            SessionName = $SessionName
            Client = $Client
            ProcessId = $process.Id
            CapturePath = $paths.Capture
            MarkersPath = $paths.Markers
            DetectionsPath = $paths.Detections
            StdOutPath = $paths.StdOut
            StdErrPath = $paths.StdErr
            Interface = $Interface
            ResolvedInterface = $resolvedInterface
            CaptureFilter = $CaptureFilter
            StartedUtc = [DateTime]::UtcNow.ToString("o")
            StartedEpoch = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() / 1000.0
            TsharkPath = $resolvedTsharkPath
            RepoRoot = $repoRoot
        }

        Write-SessionMeta -Path $paths.Meta -Data $meta

        $monitorArguments = @(
            $pythonCommand.Prefix +
            @(
                "-u",
                (Format-Argument $monitorScript),
                "--session-name", $SessionName,
                "--client", $Client,
                "--repo-root", (Format-Argument $repoRoot),
                "--captures-root", (Format-Argument $capturesRoot),
                "--tshark-path", (Format-Argument $resolvedTsharkPath)
            )
        )
        $monitorProcess = Start-Process `
            -FilePath $pythonCommand.FilePath `
            -ArgumentList $monitorArguments `
            -WorkingDirectory $repoRoot `
            -RedirectStandardOutput $paths.MonitorStdOut `
            -RedirectStandardError $paths.MonitorStdErr `
            -PassThru `
            -WindowStyle Hidden

        $meta | Add-Member -NotePropertyName MonitorProcessId -NotePropertyValue $monitorProcess.Id -Force
        Write-SessionMeta -Path $paths.Meta -Data $meta

        Export-OccSessionState -SessionName $SessionName -Paths $paths -Meta $meta
        Write-Host "Started session '$SessionName' with PID $($process.Id)"
        Write-Host "Capture file: $($paths.Capture)"
        break
    }
    "Mark" {
        if (-not $meta -or -not (Test-SessionProcessRunning -Meta $meta)) {
            throw "Session '$SessionName' is not running."
        }

        $now = [DateTimeOffset]::UtcNow
        $marker = [PSCustomObject]@{
            Label = $Label
            Note = $Note
            MarkedUtc = $now.ToString("o")
            MarkedEpoch = $now.ToUnixTimeMilliseconds() / 1000.0
        }

        Add-Content -LiteralPath $paths.Markers -Value ($marker | ConvertTo-Json -Compress)
        Export-OccSessionState -SessionName $SessionName -Paths $paths -Meta $meta
        Write-Host "Marked '$Label' at $($marker.MarkedUtc)"
        break
    }
    "Stop" {
        if (-not $meta) {
            throw "Session '$SessionName' does not exist."
        }

        if (Test-SessionProcessRunning -Meta $meta) {
            Stop-Process -Id $meta.ProcessId -Force -ErrorAction Stop
            Start-Sleep -Milliseconds 1000
            $deadline = (Get-Date).AddSeconds(5)
            while ((Get-Date) -lt $deadline) {
                if (-not (Get-Process -Id $meta.ProcessId -ErrorAction SilentlyContinue)) {
                    break
                }
                Start-Sleep -Milliseconds 250
            }
        }

        $meta | Add-Member -NotePropertyName StoppedUtc -NotePropertyValue ([DateTime]::UtcNow.ToString("o")) -Force
        Write-SessionMeta -Path $paths.Meta -Data $meta
        Start-Sleep -Milliseconds 1200
        if ($meta.PSObject.Properties.Name -contains "MonitorProcessId") {
            Stop-Process -Id $meta.MonitorProcessId -ErrorAction SilentlyContinue
        }
        Export-OccSessionState -SessionName $SessionName -Paths $paths -Meta $meta
        Write-Host "Stopped session '$SessionName'"
        Write-Host "Capture file: $($meta.CapturePath)"
        break
    }
    "Status" {
        if (-not $meta) {
            throw "Session '$SessionName' does not exist."
        }

        $running = Test-SessionProcessRunning -Meta $meta
        Write-Host "Session: $($meta.SessionName)"
        Write-Host "Running: $running"
        Write-Host "PID: $($meta.ProcessId)"
        Write-Host "Capture: $($meta.CapturePath)"
        Write-Host "Markers: $($meta.MarkersPath)"
        Write-Host "Detections: $($meta.DetectionsPath)"
        Write-Host "Filter: $($meta.CaptureFilter)"
        Export-OccSessionState -SessionName $SessionName -Paths $paths -Meta $meta
        break
    }
    "ClearMarkers" {
        if (-not $meta) {
            throw "Session '$SessionName' does not exist."
        }

        if (Test-Path -LiteralPath $paths.Markers) {
            Clear-Content -LiteralPath $paths.Markers
        } else {
            New-Item -ItemType File -Force -Path $paths.Markers | Out-Null
        }

        Export-OccSessionState -SessionName $SessionName -Paths $paths -Meta $meta
        Write-Host "Cleared markers for session '$SessionName'"
        break
    }
    "ClearDetections" {
        if (-not $meta) {
            throw "Session '$SessionName' does not exist."
        }

        $currentDetections = Read-SessionDetections -Path $paths.Detections
        $clearedDetections = [PSCustomObject]@{
            activityCount = 0
            activity = @()
            detectionCount = 0
            detections = @()
            lastFrameNumber = [int]$currentDetections.lastFrameNumber
            syncedUtc = [DateTime]::UtcNow.ToString("o")
        }

        $clearedDetections | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $paths.Detections -Encoding UTF8
        Export-OccSessionState -SessionName $SessionName -Paths $paths -Meta $meta
        Write-Host "Cleared detections for session '$SessionName'"
        break
    }
}
