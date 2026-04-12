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

$forwardScript = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\scripts\invoke_occ_tshark_session.ps1"))

& $forwardScript `
    -Action $Action `
    -SessionName $SessionName `
    -Client $Client `
    -TsharkPath $TsharkPath `
    -Interface $Interface `
    -CaptureFilter $CaptureFilter `
    -DurationSec $DurationSec `
    -Label $Label `
    -Note $Note

exit $LASTEXITCODE
