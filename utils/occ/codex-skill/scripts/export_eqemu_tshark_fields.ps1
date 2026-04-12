[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,
    [string]$TsharkPath = "C:\Program Files\Wireshark\tshark.exe",
    [string]$DisplayFilter = "udp",
    [ValidateSet("summary", "payloads")]
    [string]$Mode = "payloads",
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $TsharkPath)) {
    throw "tshark.exe was not found at '$TsharkPath'."
}

if (-not (Test-Path -LiteralPath $InputPath)) {
    throw "Capture file was not found at '$InputPath'."
}

$resolvedInputPath = (Resolve-Path -LiteralPath $InputPath).ProviderPath

$fieldList = @(
    "frame.number",
    "frame.time_epoch",
    "ip.src",
    "udp.srcport",
    "ip.dst",
    "udp.dstport",
    "frame.len",
    "_ws.col.info"
)

if ($Mode -eq "payloads") {
    $fieldList += @(
        "udp.payload",
        "data.data"
    )
}

$arguments = @(
    "-r", $resolvedInputPath,
    "-n",
    "-T", "fields",
    "-E", "header=y",
    "-E", "separator=`t",
    "-E", "quote=n",
    "-E", "occurrence=f"
)

if (-not [string]::IsNullOrWhiteSpace($DisplayFilter)) {
    $arguments += @("-Y", $DisplayFilter)
}

foreach ($field in $fieldList) {
    $arguments += @("-e", $field)
}

$result = & $TsharkPath @arguments
if ($LASTEXITCODE -ne 0) {
    throw "tshark exited with code $LASTEXITCODE."
}

if ($OutputPath) {
    $outputDirectory = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
        New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    }

    Set-Content -LiteralPath $OutputPath -Value $result -Encoding UTF8
    Write-Host "Export saved to $OutputPath"
} else {
    $result
}
