param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$Tag,

    [Parameter(Mandatory = $true)]
    [string]$CommitSha
)

$ErrorActionPreference = "Stop"

$rootDir = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$binDir = Join-Path $rootDir "build/bin"
$distDir = Join-Path $rootDir "dist"
$packageName = "eqemu-server-windows-x64-$Tag"
$stageDir = Join-Path $distDir $packageName
$zipPath = Join-Path $distDir "$packageName.zip"

$requiredBinaries = @(
    "world.exe",
    "zone.exe",
    "ucs.exe",
    "queryserv.exe",
    "eqlaunch.exe",
    "shared_memory.exe",
    "loginserver.exe",
    "import_client_files.exe",
    "export_client_files.exe"
)

function Copy-UniqueFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $target = Join-Path $stageDir ([System.IO.Path]::GetFileName($Path))
    if (Test-Path $target) {
        return
    }

    Copy-Item -LiteralPath $Path -Destination $target
}

function Copy-VisualCppRuntime {
    if (-not $env:VCToolsRedistDir -or -not (Test-Path $env:VCToolsRedistDir)) {
        Write-Host "VCToolsRedistDir was not set; skipping Visual C++ runtime copy."
        return
    }

    Get-ChildItem -Path $env:VCToolsRedistDir -Recurse -File -Filter "*.dll" |
        Where-Object {
            $_.FullName -match "\\x64\\" -and
            $_.FullName -match "\\Microsoft\.VC\d+\.CRT\\"
        } |
        ForEach-Object { Copy-UniqueFile -Path $_.FullName }
}

function Get-DumpbinDependencies {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $output = & dumpbin /dependents $Path 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed for $Path`n$output"
    }

    $dependencies = New-Object System.Collections.Generic.List[string]
    foreach ($line in $output) {
        if ($line -match "^\s*([A-Za-z0-9_.-]+\.dll)\s*$") {
            $dependencies.Add($matches[1])
        }
    }

    return $dependencies
}

function Test-RuntimeDependencies {
    $dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
    if (-not $dumpbin) {
        throw "dumpbin was not found. Run this script from an MSVC developer environment."
    }

    $systemDlls = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    @(
        "advapi32.dll", "authz.dll", "bcrypt.dll", "bcryptprimitives.dll",
        "cabinet.dll", "cfgmgr32.dll", "comctl32.dll", "comdlg32.dll",
        "credui.dll", "crypt32.dll", "cryptbase.dll", "cryptsp.dll",
        "cryptui.dll", "dbghelp.dll", "dhcpcsvc.dll", "dhcpcsvc6.dll",
        "dnsapi.dll", "dwmapi.dll", "fwpuclnt.dll", "gdi32.dll",
        "imagehlp.dll", "imm32.dll", "iphlpapi.dll", "kernel32.dll",
        "mpr.dll", "msvcp_win.dll", "mswsock.dll", "ncrypt.dll",
        "netapi32.dll", "netutils.dll", "normaliz.dll", "nsi.dll",
        "ntdll.dll", "ole32.dll", "oleacc.dll", "oleaut32.dll",
        "powrprof.dll", "profapi.dll", "propsys.dll", "psapi.dll",
        "rasapi32.dll", "rpcrt4.dll", "sechost.dll", "secur32.dll",
        "setupapi.dll", "shell32.dll", "shlwapi.dll", "ucrtbase.dll",
        "user32.dll", "userenv.dll", "usp10.dll", "uxtheme.dll",
        "version.dll", "windows.storage.dll", "winhttp.dll", "winmm.dll",
        "winspool.drv", "wintrust.dll", "wlanapi.dll", "ws2_32.dll",
        "wldap32.dll"
    ) | ForEach-Object { [void]$systemDlls.Add($_) }

    $presentDlls = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    Get-ChildItem -Path $stageDir -File -Filter "*.dll" | ForEach-Object {
        [void]$presentDlls.Add($_.Name)
    }

    $missing = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    Get-ChildItem -Path $stageDir -File | Where-Object { $_.Extension -in @(".exe", ".dll") } | ForEach-Object {
        foreach ($dependency in (Get-DumpbinDependencies -Path $_.FullName)) {
            if ($dependency -match "^(api-ms-win-|ext-ms-)") {
                continue
            }
            if ($systemDlls.Contains($dependency)) {
                continue
            }
            if (-not $presentDlls.Contains($dependency)) {
                [void]$missing.Add("$dependency required by $($_.Name)")
            }
        }
    }

    if ($missing.Count -gt 0) {
        throw "Missing Windows runtime dependencies:`n$($missing -join "`n")"
    }
}

if (Test-Path $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

foreach ($binary in $requiredBinaries) {
    $path = Join-Path $binDir $binary
    if (-not (Test-Path $path)) {
        throw "Required binary is missing: $path"
    }
}

Get-ChildItem -Path $binDir -File -Filter "*.exe" |
    Where-Object { $_.Name -ne "tests.exe" } |
    ForEach-Object { Copy-UniqueFile -Path $_.FullName }

$libraryRoots = @(
    $binDir,
    (Join-Path $rootDir "build/libs"),
    (Join-Path $rootDir "build/vcpkg_installed"),
    (Join-Path $rootDir "vcpkg_installed")
)

foreach ($libraryRoot in $libraryRoots) {
    if (-not (Test-Path $libraryRoot)) {
        continue
    }

    Get-ChildItem -Path $libraryRoot -Recurse -File -Filter "*.dll" |
        Where-Object { $_.FullName -notmatch "\\debug\\" } |
        ForEach-Object { Copy-UniqueFile -Path $_.FullName }
}

Copy-VisualCppRuntime
Test-RuntimeDependencies

$manifestFiles = Get-ChildItem -Path $stageDir -File | Sort-Object Name | ForEach-Object {
    [PSCustomObject]@{
        path   = $_.Name
        size   = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$manifest = [PSCustomObject]@{
    version      = $Version
    tag          = $Tag
    commit_sha   = $CommitSha
    platform     = "windows-x64"
    generated_at = [DateTimeOffset]::UtcNow.ToString("o")
    files        = $manifestFiles
}

$manifestPath = Join-Path $stageDir "release-manifest.json"
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM

Compress-Archive -Path $stageDir -DestinationPath $zipPath -Force

if (-not (Test-Path $zipPath)) {
    throw "Failed to create $zipPath"
}

Write-Host "Created $zipPath"
