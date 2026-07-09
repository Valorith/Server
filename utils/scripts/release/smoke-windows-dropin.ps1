param(
    [Parameter(Mandatory = $true)]
    [string]$PackageZip,

    [string]$BaselineBinDir,

    [string]$PerlBinDir,

    [string]$MariaDbPluginRoot,

    [int]$TimeoutSeconds = 10
)

$ErrorActionPreference = "Stop"

$rootDir = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$resolvedPackageZip = (Resolve-Path -LiteralPath $PackageZip).Path
$workRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("eqemu-windows-dropin-smoke-" + [Guid]::NewGuid().ToString("N"))
$extractRoot = Join-Path $workRoot "package"
$fixtureRoot = Join-Path $workRoot "fixture"
$fixtureBin = Join-Path $fixtureRoot "bin"

$smokeBinaries = @(
    "shared_memory.exe",
    "world.exe",
    "zone.exe",
    "loginserver.exe",
    "queryserv.exe",
    "ucs.exe",
    "eqlaunch.exe",
    "export_client_files.exe",
    "import_client_files.exe"
)

function Copy-MariaDbInstallFixtureFiles {
    param([string]$SearchRoot)

    if ([string]::IsNullOrWhiteSpace($SearchRoot)) {
        return
    }

    if (-not (Test-Path -LiteralPath $SearchRoot)) {
        return
    }

    $pluginDir = Join-Path $fixtureBin "plugin"
    New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null

    $pluginNames = @(
        "auth_gssapi_client.dll",
        "caching_sha2_password.dll",
        "client_ed25519.dll",
        "dialog.dll",
        "mysql_clear_password.dll",
        "pvio_npipe.dll",
        "pvio_shmem.dll",
        "sha256_password.dll"
    )

    foreach ($name in $pluginNames) {
        $candidate = Get-ChildItem -LiteralPath $SearchRoot -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch "\\debug\\" } |
            Select-Object -First 1

        if ($candidate) {
            Copy-Item -LiteralPath $candidate.FullName -Destination (Join-Path $fixtureBin $name) -Force
            Copy-Item -LiteralPath $candidate.FullName -Destination (Join-Path $pluginDir $name) -Force
        }
    }
}

function Write-DummyRuntimeConfig {
    param([Parameter(Mandatory = $true)][string]$RuntimeRoot)

    New-Item -ItemType Directory -Force -Path $RuntimeRoot | Out-Null

    $eqemuConfig = @{
        server = @{
            database = @{
                host     = "127.0.0.2"
                port     = "1"
                username = "eqemu_release_smoke"
                password = "eqemu_release_smoke"
                db       = "eqemu_release_smoke"
            }
            qsdatabase = @{
                host     = "127.0.0.2"
                port     = "1"
                username = "eqemu_release_smoke"
                password = "eqemu_release_smoke"
                db       = "eqemu_release_smoke"
            }
            world = @{
                shortname    = "smoke"
                longname     = "Release Smoke Test"
                address      = "127.0.0.1"
                localaddress = "127.0.0.1"
                key          = "release-smoke-test-key"
                tcp          = @{ ip = "127.0.0.1"; port = "19001" }
                telnet       = @{ ip = "127.0.0.1"; port = "19000"; enabled = "false" }
                http         = @{ port = "19080"; enabled = "false"; mimefile = "mime.types" }
                loginserver1 = @{ account = ""; password = ""; host = "127.0.0.1"; port = "15988"; legacy = 0 }
            }
            zones = @{
                defaultstatus = "0"
                ports         = @{ low = "17000"; high = "17010" }
            }
            chatserver  = @{ host = "127.0.0.1"; port = "17778" }
            mailserver  = @{ host = "127.0.0.1"; port = "17778" }
            webinterface = @{ port = "19081" }
            files = @{
                opcodes      = "assets/patches/opcodes.conf"
                mail_opcodes = "assets/patches/mail_opcodes.conf"
            }
            directories = @{
                patches = "assets/patches/"
                opcodes = "assets/patches/"
                plugins = "quests/plugins/"
            }
        }
    }

    $loginConfig = @{
        general = @{
            listen_port = 15988
            default_loginserver_name = "local"
        }
        database = @{
            host     = "127.0.0.2"
            port     = 1
            db       = "eqemu_release_smoke"
            user     = "eqemu_release_smoke"
            password = "eqemu_release_smoke"
        }
        account = @{
            auto_create_accounts = $false
        }
        worldservers = @{
            unregistered_allowed   = $true
            reject_duplicate_servers = $false
        }
        web_api = @{
            enabled = $false
            port    = 16000
        }
        security = @{
            mode                 = 14
            allow_password_login = $true
            allow_token_login    = $true
        }
        logging = @{
            trace            = $false
            world_trace      = $false
            dump_packets_in  = $false
            dump_packets_out = $false
        }
        client_configuration = @{
            titanium_port    = 15998
            titanium_opcodes = "assets/patches/login_opcodes.conf"
            sod_port         = 15999
            sod_opcodes      = "assets/patches/login_opcodes_sod.conf"
        }
    }

    $eqemuConfig | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $RuntimeRoot "eqemu_config.json") -Encoding utf8NoBOM
    $loginConfig | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $RuntimeRoot "login.json") -Encoding utf8NoBOM
}

function Convert-ExitCodeToHex {
    param([Nullable[int]]$ExitCode)

    if ($null -eq $ExitCode) {
        return $null
    }

    return "0x{0:X8}" -f ([BitConverter]::ToUInt32([BitConverter]::GetBytes([int32]$ExitCode), 0))
}

function Invoke-SmokeBinary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string]$RuntimeRoot,

        [Parameter(Mandatory = $true)]
        [string]$PathValue
    )

    $stdoutPath = Join-Path $RuntimeRoot "$Name.stdout.txt"
    $stderrPath = Join-Path $RuntimeRoot "$Name.stderr.txt"

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = Join-Path $fixtureBin $Name
    $psi.WorkingDirectory = $RuntimeRoot
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $psi.EnvironmentVariables["PATH"] = $PathValue

    try {
        $process = [System.Diagnostics.Process]::Start($psi)
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $exited = $process.WaitForExit($TimeoutSeconds * 1000)

        if (-not $exited) {
            try {
                $process.Kill()
            }
            catch {
            }

            $process.WaitForExit(2000) | Out-Null
            $status = "timeout-killed"
            $exitCode = $null
        }
        else {
            $status = "exited"
            $exitCode = $process.ExitCode
        }

        $stdoutTask.Wait(2000) | Out-Null
        $stderrTask.Wait(2000) | Out-Null

        $stdout = if ($stdoutTask.IsCompleted) { $stdoutTask.Result } else { "<stdout read timed out>" }
        $stderr = if ($stderrTask.IsCompleted) { $stderrTask.Result } else { "<stderr read timed out>" }

        Set-Content -LiteralPath $stdoutPath -Value $stdout -Encoding utf8NoBOM
        Set-Content -LiteralPath $stderrPath -Value $stderr -Encoding utf8NoBOM

        return [PSCustomObject]@{
            Binary  = $Name
            Status  = $status
            ExitCode = $exitCode
            Hex     = Convert-ExitCodeToHex -ExitCode $exitCode
            Stdout  = $stdout
            Stderr  = $stderr
        }
    }
    catch {
        return [PSCustomObject]@{
            Binary  = $Name
            Status  = "start-error"
            ExitCode = $null
            Hex     = $null
            Stdout  = ""
            Stderr  = $_.Exception.Message
        }
    }
}

try {
    New-Item -ItemType Directory -Force -Path $extractRoot, $fixtureBin | Out-Null
    Expand-Archive -LiteralPath $resolvedPackageZip -DestinationPath $extractRoot -Force

    $packageRoot = Get-ChildItem -LiteralPath $extractRoot -Directory | Select-Object -First 1
    if (-not $packageRoot) {
        throw "Package did not contain a top-level directory: $resolvedPackageZip"
    }

    if ($BaselineBinDir) {
        $resolvedBaselineBinDir = (Resolve-Path -LiteralPath $BaselineBinDir).Path
        Copy-Item -Path (Join-Path $resolvedBaselineBinDir "*") -Destination $fixtureBin -Recurse -Force
    }
    else {
        if ($MariaDbPluginRoot) {
            Copy-MariaDbInstallFixtureFiles -SearchRoot $MariaDbPluginRoot
        }

        Copy-MariaDbInstallFixtureFiles -SearchRoot (Join-Path $rootDir "build/vcpkg_installed")
        Copy-MariaDbInstallFixtureFiles -SearchRoot (Join-Path $rootDir "vcpkg_installed")
    }

    Copy-Item -Path (Join-Path $packageRoot.FullName "*") -Destination $fixtureBin -Recurse -Force

    foreach ($binary in $smokeBinaries) {
        $path = Join-Path $fixtureBin $binary
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Smoke fixture is missing required binary: $path"
        }
    }

    $pathEntries = [System.Collections.Generic.List[string]]::new()
    [void]$pathEntries.Add($fixtureBin)

    if ($PerlBinDir) {
        $resolvedPerlBinDir = (Resolve-Path -LiteralPath $PerlBinDir).Path
        [void]$pathEntries.Add($resolvedPerlBinDir)

        $perlRoot = Split-Path -Parent (Split-Path -Parent $resolvedPerlBinDir)
        $perlCBin = Join-Path $perlRoot "c\bin"
        if (Test-Path -LiteralPath $perlCBin) {
            [void]$pathEntries.Add((Resolve-Path -LiteralPath $perlCBin).Path)
        }
    }

    if ($env:WINDIR) {
        [void]$pathEntries.Add((Join-Path $env:WINDIR "System32"))
        [void]$pathEntries.Add($env:WINDIR)
    }

    $pathValue = $pathEntries -join ";"
    $results = @()

    foreach ($binary in $smokeBinaries) {
        $runtimeRoot = Join-Path $workRoot ("runtime-" + [System.IO.Path]::GetFileNameWithoutExtension($binary))
        Write-DummyRuntimeConfig -RuntimeRoot $runtimeRoot
        $results += Invoke-SmokeBinary -Name $binary -RuntimeRoot $runtimeRoot -PathValue $pathValue
    }

    $failures = [System.Collections.Generic.List[string]]::new()
    foreach ($result in $results) {
        $combinedOutput = "$($result.Stdout)`n$($result.Stderr)"

        if ($result.Status -ne "exited") {
            [void]$failures.Add("$($result.Binary) did not exit cleanly: $($result.Status)")
            continue
        }

        if ($result.ExitCode -ne 1) {
            [void]$failures.Add("$($result.Binary) returned $($result.ExitCode) $($result.Hex); expected exit code 1 for dummy DB failure")
        }

        if ($combinedOutput -match "Crash|EXCEPTION_|heap corruption|0xC000|could not be loaded|specified module could not be found") {
            [void]$failures.Add("$($result.Binary) output indicates a loader or crash failure")
        }
    }

    $summary = $results | Select-Object Binary, Status, ExitCode, Hex
    $summary | Format-Table -AutoSize | Out-String | Write-Host

    if ($failures.Count -gt 0) {
        Write-Host "Smoke test work root: $workRoot"
        throw "Windows drop-in smoke test failed:`n$($failures -join "`n")"
    }

    Write-Host "Windows drop-in smoke test passed."
}
finally {
    if ($env:EQEMU_KEEP_RELEASE_SMOKE -ne "1" -and (Test-Path -LiteralPath $workRoot)) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
