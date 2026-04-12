[CmdletBinding()]
param(
    [switch]$NoBrowser,
    [switch]$AutoInstallTshark,
    [switch]$SkipTsharkCheck,
    [switch]$ForceRestartServer
)

$ErrorActionPreference = "Stop"

$occRoot = Split-Path -Parent $PSCommandPath
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $occRoot "..\.."))
$workspaceRoot = Split-Path -Parent $repoRoot
$serverScript = Join-Path $occRoot "scripts\occ_server.py"
$serverStdOutLog = Join-Path $occRoot "occ_server.start.log"
$serverStdErrLog = Join-Path $occRoot "occ_server.error.log"
$capturesRoot = Join-Path $workspaceRoot ".codex\captures\sessions"
$url = "http://127.0.0.1:8765/"
$downloadUrl = "https://www.wireshark.org/download.html"
$port = 8765

function Write-Step {
    param(
        [string]$Message,
        [ValidateSet("Info", "Success", "Warning", "Error")]
        [string]$Tone = "Info"
    )

    $prefix = switch ($Tone) {
        "Success" { "[ok]" }
        "Warning" { "[!]" }
        "Error" { "[x]" }
        default { "[...]" }
    }

    Write-Host "$prefix $Message"
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

function Resolve-PythonCommand {
    $python = Get-Command python -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($python -and $python.Source -and (Test-Path -LiteralPath $python.Source)) {
        return @{
            FilePath = $python.Source
            Prefix = @()
            DisplayName = $python.Source
        }
    }

    $pyLauncher = Get-Command py -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($pyLauncher -and $pyLauncher.Source -and (Test-Path -LiteralPath $pyLauncher.Source)) {
        return @{
            FilePath = $pyLauncher.Source
            Prefix = @("-3")
            DisplayName = "$($pyLauncher.Source) -3"
        }
    }

    return $null
}

function Resolve-TsharkPath {
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

    return $null
}

function Get-ToolVersionLine {
    param(
        [string]$ExecutablePath,
        [string[]]$Arguments
    )

    try {
        $lines = & $ExecutablePath @Arguments 2>$null
        if ($lines) {
            return ($lines | Select-Object -First 1).ToString().Trim()
        }
    } catch {
        return ""
    }

    return ""
}

function Install-TsharkWithWinget {
    $winget = Get-Command winget -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $winget) {
        Write-Step "winget is not available on this system." "Warning"
        return $false
    }

    Write-Step "Launching Wireshark install via winget." "Info"
    Write-Host "    Keep the TShark feature selected during setup."
    Write-Host "    If prompted to install Npcap, accept it so packet capture works."

    & $winget.Source install --id WiresharkFoundation.Wireshark --exact --accept-package-agreements --accept-source-agreements
    return ($LASTEXITCODE -eq 0)
}

function Ensure-TsharkInstalled {
    if ($SkipTsharkCheck) {
        Write-Step "Skipping tshark preflight at your request. OCC will start, but capture controls may fail until tshark is installed." "Warning"
        return $null
    }

    $tsharkPath = Resolve-TsharkPath
    if ($tsharkPath) {
        $versionLine = Get-ToolVersionLine -ExecutablePath $tsharkPath -Arguments @("--version")
        $versionSuffix = if ($versionLine) { " ($versionLine)" } else { "" }
        Write-Step "Found tshark at $tsharkPath$versionSuffix" "Success"
        return $tsharkPath
    }

    Write-Step "OCC capture controls need Wireshark's command-line tool: tshark." "Warning"
    Write-Step "Install Wireshark with TShark enabled, and allow Npcap if the installer offers it." "Warning"

    if ($AutoInstallTshark) {
        if (-not (Install-TsharkWithWinget)) {
            throw "Wireshark installation did not complete successfully. Install it from $downloadUrl and run start_occ.ps1 again."
        }

        $tsharkPath = Resolve-TsharkPath
        if (-not $tsharkPath) {
            throw "Wireshark finished, but tshark.exe is still missing. Re-run the installer and make sure the TShark feature is selected."
        }

        $versionLine = Get-ToolVersionLine -ExecutablePath $tsharkPath -Arguments @("--version")
        $versionSuffix = if ($versionLine) { " ($versionLine)" } else { "" }
        Write-Step "tshark is ready at $tsharkPath$versionSuffix" "Success"
        return $tsharkPath
    }

    if (-not [Environment]::UserInteractive) {
        throw "tshark was not found. Install Wireshark from $downloadUrl or run start_occ.ps1 -AutoInstallTshark."
    }

    while (-not $tsharkPath) {
        Write-Host ""
        Write-Host "Choose an option:"
        Write-Host "  I - install Wireshark now with winget"
        Write-Host "  D - open the Wireshark download page"
        Write-Host "  Q - quit and install later"
        $choice = (Read-Host "Selection").Trim().ToUpperInvariant()

        switch ($choice) {
            "I" {
                $installSucceeded = Install-TsharkWithWinget
                if (-not $installSucceeded) {
                    Write-Step "The automated install did not complete. You can use the download page instead." "Warning"
                }
            }
            "D" {
                Write-Step "Opening the Wireshark download page." "Info"
                Start-Process $downloadUrl | Out-Null
                Write-Host "    After installation finishes, press Enter here and OCC will re-check tshark."
                Write-Host "    Keep TShark enabled and install Npcap if prompted."
                Read-Host "Press Enter after Wireshark finishes installing" | Out-Null
            }
            "Q" {
                throw "Cancelled before Wireshark/tshark was installed."
            }
            default {
                Write-Step "Please enter I, D, or Q." "Warning"
            }
        }

        $tsharkPath = Resolve-TsharkPath
        if (-not $tsharkPath) {
            Write-Step "tshark is still not available. Re-run the installer and make sure the TShark feature is selected." "Warning"
        }
    }

    $versionLine = Get-ToolVersionLine -ExecutablePath $tsharkPath -Arguments @("--version")
    $versionSuffix = if ($versionLine) { " ($versionLine)" } else { "" }
    Write-Step "tshark is ready at $tsharkPath$versionSuffix" "Success"
    return $tsharkPath
}

function Get-OccListener {
    return Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
}

function Get-ProcessDetails {
    param([int]$ProcessId)

    if (-not $ProcessId) {
        return $null
    }

    return Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction SilentlyContinue
}

function Test-IsOccServerProcess {
    param([object]$ProcessInfo)

    if (-not $ProcessInfo) {
        return $false
    }

    return ($ProcessInfo.CommandLine -like "*occ_server.py*")
}

function Wait-ForOccReady {
    param(
        [int]$TimeoutSeconds = 15,
        [switch]$AllowLogHint
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            Invoke-WebRequest -Uri $url -TimeoutSec 2 -ErrorAction Stop | Out-Null
            return $true
        } catch {
            if ($AllowLogHint -and (Test-Path -LiteralPath $serverStdOutLog)) {
                $recentOutput = (Get-Content -LiteralPath $serverStdOutLog -Tail 5 -ErrorAction SilentlyContinue) -join [Environment]::NewLine
                if ($recentOutput -match "OCC available at http://127\.0\.0\.1:8765/") {
                    return $true
                }
            }
            Start-Sleep -Milliseconds 350
        }
    }

    return $false
}

function Start-OccServer {
    param([hashtable]$PythonCommand)

    Set-Content -LiteralPath $serverStdOutLog -Value "" -Encoding UTF8
    Set-Content -LiteralPath $serverStdErrLog -Value "" -Encoding UTF8

    $arguments = @($PythonCommand.Prefix + @((Format-Argument $serverScript)))

    return Start-Process `
        -FilePath $PythonCommand.FilePath `
        -ArgumentList $arguments `
        -WorkingDirectory $occRoot `
        -RedirectStandardOutput $serverStdOutLog `
        -RedirectStandardError $serverStdErrLog `
        -WindowStyle Minimized `
        -PassThru
}

Write-Host ""
Write-Host "Opcode Command Center"
Write-Host "====================="

$pythonCommand = Resolve-PythonCommand
if (-not $pythonCommand) {
    throw "Python 3 is required to launch OCC. Install Python from https://www.python.org/downloads/windows/ and run this script again."
}

$versionArgs = if ($pythonCommand.Prefix.Count -gt 0) { @($pythonCommand.Prefix + @("--version")) } else { @("--version") }
$pythonVersionLine = Get-ToolVersionLine -ExecutablePath $pythonCommand.FilePath -Arguments $versionArgs
$pythonSuffix = if ($pythonVersionLine) { " ($pythonVersionLine)" } else { "" }
Write-Step "Using Python runtime $($pythonCommand.DisplayName)$pythonSuffix" "Success"

$null = Ensure-TsharkInstalled

$listener = Get-OccListener
if ($listener) {
    $processInfo = Get-ProcessDetails -ProcessId $listener.OwningProcess
    if (-not (Test-IsOccServerProcess -ProcessInfo $processInfo)) {
        $owner = if ($processInfo) { "$($processInfo.Name) (PID $($processInfo.ProcessId))" } else { "an unknown process (PID $($listener.OwningProcess))" }
        throw "Port $port is already in use by $owner. Free the port or stop that process before launching OCC."
    }

    if ($ForceRestartServer) {
        Write-Step "Stopping the existing OCC server so it can be restarted cleanly." "Info"
        Stop-Process -Id $listener.OwningProcess -Force -ErrorAction Stop
        Start-Sleep -Milliseconds 750
        $listener = $null
    } elseif (Wait-ForOccReady -TimeoutSeconds 2) {
        Write-Step "OCC server is already running on port $port." "Success"
    } else {
        Write-Step "The existing OCC server is not responding. Restarting it now." "Warning"
        Stop-Process -Id $listener.OwningProcess -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 750
        $listener = $null
    }
}

if (-not $listener) {
    Write-Step "Starting the local OCC bridge server." "Info"
    $serverProcess = Start-OccServer -PythonCommand $pythonCommand
    Write-Step "Started OCC server process $($serverProcess.Id). Waiting for http://127.0.0.1:$port/..." "Info"

    if (-not (Wait-ForOccReady -TimeoutSeconds 15 -AllowLogHint)) {
        $errorDetails = ""
        if (Test-Path -LiteralPath $serverStdErrLog) {
            $errorDetails = ((Get-Content -LiteralPath $serverStdErrLog -Tail 20 -ErrorAction SilentlyContinue) -join [Environment]::NewLine).Trim()
        }

        if ($serverProcess -and (Get-Process -Id $serverProcess.Id -ErrorAction SilentlyContinue)) {
            Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue
        }

        if ($errorDetails) {
            throw "OCC did not become ready. Last server error:`n$errorDetails"
        }

        throw "OCC did not become ready within 15 seconds. Check $serverStdErrLog for details."
    }

    Write-Step "OCC server is ready." "Success"
}

if (-not (Test-Path -LiteralPath $capturesRoot)) {
    New-Item -ItemType Directory -Force -Path $capturesRoot | Out-Null
}

if (-not $NoBrowser) {
    Write-Step "Opening OCC in your default browser." "Info"
    Start-Process $url | Out-Null
} else {
    Write-Step "Browser launch skipped." "Info"
}

Write-Host ""
Write-Step "OCC is available at $url" "Success"
Write-Host "    OCC root:     $occRoot"
Write-Host "    Captures:     $capturesRoot"
Write-Host "    Server logs:  $serverStdOutLog"
Write-Host "                  $serverStdErrLog"
