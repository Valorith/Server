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

$script:SystemRuntimeRootsCache = $null
$script:BlockedRuntimeSearchRootsCache = $null
$script:SystemRuntimeDependencyCache = @{}

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

function New-SystemDllSet {
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

    return $systemDlls
}

function Normalize-PathForCompare {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $null
    }

    try {
        return [System.IO.Path]::GetFullPath($Path).TrimEnd([char[]]@("\", "/"))
    }
    catch {
        return $null
    }
}

function Test-IsSubPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $normalizedPath = Normalize-PathForCompare -Path $Path
    $normalizedRoot = Normalize-PathForCompare -Path $Root

    if (-not $normalizedPath -or -not $normalizedRoot) {
        return $false
    }

    if ($normalizedPath.Equals($normalizedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }

    $rootPrefix = "$normalizedRoot$([System.IO.Path]::DirectorySeparatorChar)"
    return $normalizedPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)
}

function Add-UniqueResolvedDirectory {
    param(
        [System.Collections.Generic.List[string]]$Directories,

        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return
    }

    try {
        $resolvedPath = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    }
    catch {
        return
    }

    if (-not $Directories.Contains($resolvedPath)) {
        [void]$Directories.Add($resolvedPath)
    }
}

function Get-SystemRuntimeRoots {
    if ($null -ne $script:SystemRuntimeRootsCache) {
        return $script:SystemRuntimeRootsCache
    }

    $roots = [System.Collections.Generic.List[string]]::new()

    Add-UniqueResolvedDirectory -Directories $roots -Path $env:WINDIR
    Add-UniqueResolvedDirectory -Directories $roots -Path $env:SystemRoot

    if ($env:SystemDrive) {
        Add-UniqueResolvedDirectory -Directories $roots -Path (Join-Path $env:SystemDrive "Windows")
    }

    $script:SystemRuntimeRootsCache = $roots
    return $script:SystemRuntimeRootsCache
}

function Get-BlockedRuntimeSearchRoots {
    if ($null -ne $script:BlockedRuntimeSearchRootsCache) {
        return $script:BlockedRuntimeSearchRootsCache
    }

    $roots = [System.Collections.Generic.List[string]]::new()

    foreach ($root in (Get-SystemRuntimeRoots)) {
        Add-UniqueResolvedDirectory -Directories $roots -Path $root
    }

    $blockedRoots = @(
        $env:WindowsSdkDir,
        $env:WindowsSdkBinPath,
        $env:WindowsSdkVerBinPath,
        $env:UniversalCRTSdkDir,
        $env:VSINSTALLDIR,
        $env:VCINSTALLDIR,
        $env:DevEnvDir,
        $env:VCToolsInstallDir,
        $env:FrameworkDir,
        $env:FrameworkDir64
    )

    $programFilesX86 = ${env:ProgramFiles(x86)}
    if ($programFilesX86) {
        $blockedRoots += @(
            (Join-Path $programFilesX86 "Windows Kits"),
            (Join-Path $programFilesX86 "Microsoft SDKs")
        )
    }

    $blockedRoots | ForEach-Object { Add-UniqueResolvedDirectory -Directories $roots -Path $_ }

    $script:BlockedRuntimeSearchRootsCache = $roots
    return $script:BlockedRuntimeSearchRootsCache
}

function Test-IsBlockedRuntimeSearchDirectory {
    param([string]$Path)

    foreach ($root in (Get-BlockedRuntimeSearchRoots)) {
        if (Test-IsSubPath -Path $Path -Root $root) {
            return $true
        }
    }

    return $false
}

function Find-SystemRuntimeDependency {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $cacheKey = $Name.ToLowerInvariant()
    if ($script:SystemRuntimeDependencyCache.ContainsKey($cacheKey)) {
        return $script:SystemRuntimeDependencyCache[$cacheKey]
    }

    $systemDirs = [System.Collections.Generic.List[string]]::new()
    foreach ($root in (Get-SystemRuntimeRoots)) {
        Add-UniqueResolvedDirectory -Directories $systemDirs -Path (Join-Path $root "System32")
        Add-UniqueResolvedDirectory -Directories $systemDirs -Path (Join-Path $root "SysWOW64")
        Add-UniqueResolvedDirectory -Directories $systemDirs -Path $root
    }

    foreach ($directory in $systemDirs) {
        $candidate = Join-Path $directory $Name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $resolvedCandidate = (Resolve-Path -LiteralPath $candidate).Path
            $script:SystemRuntimeDependencyCache[$cacheKey] = $resolvedCandidate
            return $resolvedCandidate
        }
    }

    $script:SystemRuntimeDependencyCache[$cacheKey] = $null
    return $script:SystemRuntimeDependencyCache[$cacheKey]
}

function Get-RuntimeSearchDirectories {
    $directories = [System.Collections.Generic.List[string]]::new()

    function Add-SearchDirectory {
        param(
            [string]$Path,
            [switch]$AllowBlockedRoot
        )

        if ([string]::IsNullOrWhiteSpace($Path)) {
            return
        }

        try {
            $resolvedPath = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
        }
        catch {
            return
        }

        if (-not $AllowBlockedRoot -and (Test-IsBlockedRuntimeSearchDirectory -Path $resolvedPath)) {
            return
        }

        if (-not $directories.Contains($resolvedPath)) {
            [void]$directories.Add($resolvedPath)
        }
    }

    Add-SearchDirectory -Path $stageDir -AllowBlockedRoot
    Add-SearchDirectory -Path $binDir -AllowBlockedRoot

    foreach ($libraryRoot in $libraryRoots) {
        if (-not (Test-Path $libraryRoot)) {
            continue
        }

        Add-SearchDirectory -Path $libraryRoot -AllowBlockedRoot
        Get-ChildItem -Path $libraryRoot -Recurse -Directory |
            Where-Object { $_.FullName -notmatch "\\debug\\" } |
            ForEach-Object { Add-SearchDirectory -Path $_.FullName -AllowBlockedRoot }
    }

    @(
        (Join-Path $env:SystemDrive "Strawberry/perl/bin"),
        (Join-Path $env:SystemDrive "Strawberry/c/bin")
    ) | ForEach-Object { Add-SearchDirectory -Path $_ -AllowBlockedRoot }

    if ($env:PATH) {
        $env:PATH -split ";" | ForEach-Object { Add-SearchDirectory -Path $_ }
    }

    return $directories
}

function Find-RuntimeDependency {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [System.Collections.Generic.List[string]]$SearchDirectories
    )

    foreach ($directory in $SearchDirectories) {
        $candidate = Join-Path $directory $Name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $resolvedCandidate = (Resolve-Path -LiteralPath $candidate).Path
            if (-not (Test-IsBlockedRuntimeSearchDirectory -Path $resolvedCandidate)) {
                return $resolvedCandidate
            }
        }
    }

    return $null
}

function Resolve-RuntimeDependencies {
    $dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
    if (-not $dumpbin) {
        throw "dumpbin was not found. Run this script from an MSVC developer environment."
    }

    $systemDlls = New-SystemDllSet
    $searchDirectories = Get-RuntimeSearchDirectories

    for ($pass = 1; $pass -le 10; $pass++) {
        $presentDlls = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        Get-ChildItem -Path $stageDir -File -Filter "*.dll" | ForEach-Object {
            [void]$presentDlls.Add($_.Name)
        }

        $copiedCount = 0
        Get-ChildItem -Path $stageDir -File | Where-Object { $_.Extension -in @(".exe", ".dll") } | ForEach-Object {
            foreach ($dependency in (Get-DumpbinDependencies -Path $_.FullName)) {
                if ($dependency -match "^(api-ms-win-|ext-ms-)") {
                    continue
                }
                if ($systemDlls.Contains($dependency)) {
                    continue
                }
                if ($presentDlls.Contains($dependency)) {
                    continue
                }
                if (Find-SystemRuntimeDependency -Name $dependency) {
                    continue
                }

                $candidate = Find-RuntimeDependency -Name $dependency -SearchDirectories $searchDirectories
                if ($candidate) {
                    Write-Host "Copying runtime dependency $dependency from $candidate"
                    Copy-UniqueFile -Path $candidate
                    [void]$presentDlls.Add($dependency)
                    $copiedCount++
                }
            }
        }

        if ($copiedCount -eq 0) {
            return
        }
    }

    throw "Runtime dependency resolution did not converge after 10 passes."
}

function Test-RuntimeDependencies {
    $dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
    if (-not $dumpbin) {
        throw "dumpbin was not found. Run this script from an MSVC developer environment."
    }

    $systemDlls = New-SystemDllSet
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
            if (Find-SystemRuntimeDependency -Name $dependency) {
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
Resolve-RuntimeDependencies
Test-RuntimeDependencies

$manifestFiles = @(Get-ChildItem -Path $stageDir -File | Sort-Object Name | ForEach-Object {
    [PSCustomObject]@{
        path   = $_.Name
        size   = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})

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
