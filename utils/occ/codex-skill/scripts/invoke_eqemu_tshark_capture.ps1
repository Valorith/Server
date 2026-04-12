[CmdletBinding()]
param(
    [switch]$ListInterfaces,
    [string]$TsharkPath = "C:\Program Files\Wireshark\tshark.exe",
    [string]$WiresharkPath = "C:\Program Files\Wireshark\Wireshark.exe",
    [string]$Interface = "loopback",
    [string]$CaptureFilter = "udp",
    [int]$DurationSec = 20,
    [int]$PacketCount = 0,
    [string]$OutputPath,
    [switch]$OpenInWireshark
)

$ErrorActionPreference = "Stop"

function Resolve-TsharkInterface {
    param(
        [string]$ExePath,
        [string]$RequestedInterface
    )

    if ([string]::IsNullOrWhiteSpace($RequestedInterface) -or $RequestedInterface -ieq "loopback") {
        return "\Device\NPF_Loopback"
    }

    if ($RequestedInterface -match "^\d+$") {
        return $RequestedInterface
    }

    $interfaces = & $ExePath -D 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to list tshark interfaces."
    }

    foreach ($line in $interfaces) {
        if ($line -match "^(?<Index>\d+)\.\s+(?<Device>\S+)\s+\((?<Label>.*)\)$") {
            if ($Matches.Device -ieq $RequestedInterface -or $Matches.Label -like "*$RequestedInterface*") {
                return $Matches.Device
            }
        }
    }

    throw "Could not resolve interface '$RequestedInterface'. Run with -ListInterfaces first."
}

if (-not (Test-Path -LiteralPath $TsharkPath)) {
    throw "tshark.exe was not found at '$TsharkPath'."
}

if ($ListInterfaces) {
    & $TsharkPath -D
    exit $LASTEXITCODE
}

if (-not $OutputPath) {
    $captureRoot = "C:\AkkStack\.codex\captures"
    New-Item -ItemType Directory -Force -Path $captureRoot | Out-Null
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputPath = Join-Path $captureRoot "eqemu-$timestamp.pcapng"
}

$outputDirectory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

$resolvedInterface = Resolve-TsharkInterface -ExePath $TsharkPath -RequestedInterface $Interface

$arguments = @(
    "-i", $resolvedInterface,
    "-w", $OutputPath,
    "-q"
)

if (-not [string]::IsNullOrWhiteSpace($CaptureFilter)) {
    $arguments += @("-f", $CaptureFilter)
}

if ($DurationSec -gt 0) {
    $arguments += @("-a", "duration:$DurationSec")
}

if ($PacketCount -gt 0) {
    $arguments += @("-c", $PacketCount)
}

Write-Host "Starting tshark capture..."
Write-Host "  tshark: $TsharkPath"
Write-Host "  interface: $resolvedInterface"
Write-Host "  filter: $CaptureFilter"
Write-Host "  output: $OutputPath"

& $TsharkPath @arguments
if ($LASTEXITCODE -ne 0) {
    throw "tshark exited with code $LASTEXITCODE."
}

if (-not (Test-Path -LiteralPath $OutputPath)) {
    throw "tshark completed but did not create '$OutputPath'. This usually means zero packets matched the capture."
}

Write-Host "Capture saved to $OutputPath"

if ($OpenInWireshark) {
    if (-not (Test-Path -LiteralPath $WiresharkPath)) {
        throw "Wireshark.exe was not found at '$WiresharkPath'."
    }

    Start-Process -FilePath $WiresharkPath -ArgumentList "`"$OutputPath`""
}
