param(
    [ValidateSet("windows", "linux", "all")]
    [string]$Platform = "all",

    [string]$Ref = "current",

    [ValidateRange(1, 5)]
    [int]$Iterations = 2,

    [string]$ReportRoot = "C:\AkkStack\artifacts\release-parity",

    [string]$AkkStackRoot = "C:\AkkStack",

    [string]$EmuCompileScript = "$env:USERPROFILE\.codex\skills\emu-compile\scripts\invoke-akkstack-eqemu-build.ps1",

    [string]$WindowsLocalDir = "",

    [string[]]$WindowsCiDirs = @(),

    [string]$LinuxLocalDir = "",

    [string[]]$LinuxCiDirs = @(),

    [switch]$SkipEmuCompile,

    [switch]$NoCleanLocalRef,

    [switch]$KeepWorkRoot
)

$ErrorActionPreference = "Stop"
if ($null -ne (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue)) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$rootDir = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runRoot = Join-Path $ReportRoot "parity-$runStamp"

$windowsBinaries = @(
    "shared_memory.exe",
    "world.exe",
    "zone.exe",
    "ucs.exe",
    "queryserv.exe",
    "loginserver.exe",
    "eqlaunch.exe",
    "import_client_files.exe",
    "export_client_files.exe"
)

$linuxBinaries = @(
    "shared_memory",
    "world",
    "zone",
    "ucs",
    "queryserv",
    "loginserver",
    "eqlaunch",
    "import_client_files",
    "export_client_files"
)

function Write-Step {
    param([string]$Message)
    Write-Host "[release-parity] $Message" -ForegroundColor Cyan
}

function Assert-UnderRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$AllowedRoot
    )

    $resolvedPath = [System.IO.Path]::GetFullPath($Path).TrimEnd([char[]]@("\", "/"))
    $resolvedRoot = [System.IO.Path]::GetFullPath($AllowedRoot).TrimEnd([char[]]@("\", "/"))
    $prefix = "$resolvedRoot$([System.IO.Path]::DirectorySeparatorChar)"

    if (-not ($resolvedPath.Equals($resolvedRoot, [StringComparison]::OrdinalIgnoreCase) -or $resolvedPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase))) {
        throw "Refusing to operate outside [$resolvedRoot]: $resolvedPath"
    }
}

function Reset-Directory {
    param([string]$Path)

    Assert-UnderRoot -Path $Path -AllowedRoot $ReportRoot
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string[]]$ArgumentList,

        [string]$WorkingDirectory = $rootDir
    )

    Write-Host "> $FilePath $($ArgumentList -join ' ')" -ForegroundColor DarkGray
    Push-Location -LiteralPath $WorkingDirectory
    try {
        & $FilePath @ArgumentList 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($ArgumentList -join ' ')"
        }
    }
    finally {
        Pop-Location
    }
}

function Get-Sha256 {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Compare-BinarySet {
    param(
        [string]$ExpectedDir,
        [string]$ActualDir,
        [string[]]$Names,
        [string]$Label,
        [string]$ReportPath
    )

    $rows = foreach ($name in $Names) {
        $expectedPath = Join-Path $ExpectedDir $name
        $actualPath = Join-Path $ActualDir $name
        $expectedItem = Get-Item -LiteralPath $expectedPath -ErrorAction SilentlyContinue
        $actualItem = Get-Item -LiteralPath $actualPath -ErrorAction SilentlyContinue
        $expectedHash = Get-Sha256 -Path $expectedPath
        $actualHash = Get-Sha256 -Path $actualPath

        [pscustomobject]@{
            label          = $Label
            name           = $name
            expected_path  = $expectedPath
            actual_path    = $actualPath
            expected_size  = if ($expectedItem) { $expectedItem.Length } else { $null }
            actual_size    = if ($actualItem) { $actualItem.Length } else { $null }
            expected_sha256 = $expectedHash
            actual_sha256  = $actualHash
            match          = ($null -ne $expectedHash -and $expectedHash -eq $actualHash)
        }
    }

    $rows | Export-Csv -LiteralPath $ReportPath -NoTypeInformation
    $mismatches = @($rows | Where-Object { -not $_.match })
    if ($mismatches.Count -gt 0) {
        $mismatchNames = ($mismatches | ForEach-Object { $_.name }) -join ", "
        throw "$Label mismatch for: $mismatchNames. See $ReportPath"
    }

    Write-Step "$Label matched $($Names.Count) binaries"
}

function Invoke-WindowsCiBuild {
    param(
        [string]$BuildDir
    )

    $targets = @(
        "shared_memory",
        "world",
        "zone",
        "ucs",
        "queryserv",
        "loginserver",
        "eqlaunch",
        "import_client_files",
        "export_client_files"
    )

    Reset-Directory -Path $BuildDir
    Invoke-Checked -FilePath "cmake" -ArgumentList @(
        "-S", $rootDir,
        "-B", $BuildDir,
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DEQEMU_BUILD_TESTS=ON",
        "-DEQEMU_BUILD_LOGIN=ON",
        "-DEQEMU_DETERMINISTIC_BUILD=ON"
    )

    $buildArgs = @(
        "--build", $BuildDir,
        "--config", "Release",
        "--target"
    )
    $buildArgs += $targets
    Invoke-Checked -FilePath "cmake" -ArgumentList $buildArgs

    return (Join-Path $BuildDir "bin\Release")
}

function Invoke-CleanLocalRef {
    if ($NoCleanLocalRef) {
        return
    }

    if (-not (Test-Path -LiteralPath $EmuCompileScript -PathType Leaf)) {
        throw "emu-compile script not found: $EmuCompileScript"
    }

    Write-Step "Cleaning emu-compile cache for $Ref"
    Invoke-Checked -FilePath "powershell" -ArgumentList @(
        "-ExecutionPolicy", "Bypass",
        "-File", $EmuCompileScript,
        "-BuildMode", "clean-ref",
        "-Ref", $Ref,
        "-NoMonitor"
    )
}

function Invoke-LocalWindowsBuild {
    param([string]$ArtifactRoot)

    if (-not (Test-Path -LiteralPath $EmuCompileScript -PathType Leaf)) {
        throw "emu-compile script not found: $EmuCompileScript"
    }

    Invoke-CleanLocalRef
    Reset-Directory -Path $ArtifactRoot
    Invoke-Checked -FilePath "powershell" -ArgumentList @(
        "-ExecutionPolicy", "Bypass",
        "-File", $EmuCompileScript,
        "-BuildMode", "windows-release",
        "-Ref", $Ref,
        "-WindowsArtifactRoot", $ArtifactRoot,
        "-NoMonitor"
    )

    $artifactDir = Get-ChildItem -LiteralPath $ArtifactRoot -Directory |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if (-not $artifactDir) {
        throw "No Windows artifact directory was produced under $ArtifactRoot"
    }

    return $artifactDir.FullName
}

function Invoke-LinuxCiBuild {
    param(
        [string]$OutputDir,
        [int]$Iteration
    )

    Reset-Directory -Path $OutputDir
    $cacheDir = Join-Path $ReportRoot "linux-vcpkg-cache"
    New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null

$script = @'
set -euo pipefail

dependency_source=/home/eqemu/code
dependency_build=/home/eqemu/code/build
source_root=/home/eqemu/.codex-builds/master/repo
build_root=/home/eqemu/.codex-builds/master/build-ninja
vcpkg_cache=/home/eqemu/.cache/vcpkg/archives

copy_workspace() {
  local target="$1"
  mkdir -p "$target"
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete \
      --exclude build/ \
      --exclude 'build-*/' \
      --exclude dist/ \
      --exclude .codex/ \
      --exclude submodules/vcpkg/buildtrees/ \
      --exclude submodules/vcpkg/downloads/ \
      --exclude submodules/vcpkg/packages/ \
      /workspace/ "$target"/
  else
    (cd /workspace && tar \
      --exclude ./build \
      --exclude './build-*' \
      --exclude ./dist \
      --exclude ./.codex \
      --exclude ./submodules/vcpkg/buildtrees \
      --exclude ./submodules/vcpkg/downloads \
      --exclude ./submodules/vcpkg/packages \
      -cf - .) | (cd "$target" && tar xf -)
  fi
}

rm -rf "$dependency_source" "$dependency_build" /home/eqemu/.codex-builds/master
mkdir -p "$dependency_source" "$source_root" "$vcpkg_cache"
copy_workspace "$dependency_source"
copy_workspace "$source_root"

git config --global --add safe.directory /workspace
git config --global --add safe.directory "$dependency_source"
git config --global --add safe.directory "$source_root"

export VCPKG_BINARY_SOURCES="clear;files,$vcpkg_cache,readwrite"

if [ -z "$(find "$vcpkg_cache" -type f -name '*.zip' -print -quit)" ]; then
  cmake -S "$dependency_source" -B "$dependency_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DEQEMU_BUILD_TESTS=ON \
    -DEQEMU_BUILD_LOGIN=ON \
    -DEQEMU_BUILD_LUA=ON \
    -DEQEMU_BUILD_PERL=ON \
    -DEQEMU_DETERMINISTIC_BUILD=ON 2>&1 | tee /out/prime-configure.log
fi

cmake -S "$source_root" -B "$build_root" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DEQEMU_BUILD_TESTS=ON \
  -DEQEMU_BUILD_LOGIN=ON \
  -DEQEMU_BUILD_LUA=ON \
  -DEQEMU_BUILD_PERL=ON \
  -DEQEMU_DETERMINISTIC_BUILD=ON 2>&1 | tee /out/configure.log
if ! cmake --build "$build_root" --parallel > /out/build.log 2>&1; then
  tail -200 /out/build.log
  exit 1
fi
tail -80 /out/build.log
(cd "$build_root" && ./bin/tests) 2>&1 | tee /out/tests.log
cp "$build_root/CMakeCache.txt" /out/CMakeCache.txt
for binary in shared_memory world zone ucs queryserv eqlaunch loginserver import_client_files export_client_files; do
  cp -a "$build_root/bin/$binary" "/out/$binary"
done
'@

    Invoke-Checked -FilePath "docker" -ArgumentList @(
        "run",
        "--rm",
        "--entrypoint", "bash",
        "--user", "0",
        "-v", "$rootDir`:/workspace",
        "-v", "$OutputDir`:/out",
        "-v", "$cacheDir`:/home/eqemu/.cache/vcpkg/archives",
        "-w", "/workspace",
        "akkadius/eqemu-server:v16",
        "-lc",
        $script
    )

    return $OutputDir
}

function Invoke-LocalLinuxBuild {
    if (-not (Test-Path -LiteralPath $EmuCompileScript -PathType Leaf)) {
        throw "emu-compile script not found: $EmuCompileScript"
    }

    Invoke-CleanLocalRef
    Invoke-Checked -FilePath "powershell" -ArgumentList @(
        "-ExecutionPolicy", "Bypass",
        "-File", $EmuCompileScript,
        "-BuildMode", "linux-test",
        "-Ref", $Ref,
        "-NoMonitor",
        "-NoOpenSpire",
        "-SkipSpireBuild"
    )

    return (Join-Path $AkkStackRoot "server\bin")
}

New-Item -ItemType Directory -Force -Path $ReportRoot | Out-Null
Reset-Directory -Path $runRoot

$summary = [ordered]@{
    root       = $rootDir
    ref        = $Ref
    platform   = $Platform
    iterations = $Iterations
    run_root   = $runRoot
    started_at = [DateTimeOffset]::Now.ToString("o")
    checks     = @()
}

if ($Platform -in @("windows", "all")) {
    Write-Step "Windows local-vs-CI parity"
    $localWindowsDir = if (-not [string]::IsNullOrWhiteSpace($WindowsLocalDir)) {
        (Resolve-Path -LiteralPath $WindowsLocalDir).Path
    }
    elseif ($SkipEmuCompile) {
        Join-Path $AkkStackRoot "artifacts\windows-release\$Ref"
    }
    else {
        Invoke-LocalWindowsBuild -ArtifactRoot (Join-Path $runRoot "windows-local")
    }

    $previousCiDir = $null
    for ($i = 1; $i -le $Iterations; $i++) {
        $ciBinDir = if ($WindowsCiDirs.Count -ge $i) {
            (Resolve-Path -LiteralPath $WindowsCiDirs[$i - 1]).Path
        }
        else {
            Invoke-WindowsCiBuild -BuildDir (Join-Path $runRoot "windows-ci-$i")
        }
        $reportPath = Join-Path $runRoot "windows-local-vs-ci-$i.csv"
        Compare-BinarySet -ExpectedDir $localWindowsDir -ActualDir $ciBinDir -Names $windowsBinaries -Label "windows local-vs-ci iteration $i" -ReportPath $reportPath
        $summary["checks"] += $reportPath

        $commonDlls = @(Get-ChildItem -LiteralPath $localWindowsDir -Filter "*.dll" |
            Where-Object { Test-Path -LiteralPath (Join-Path $ciBinDir $_.Name) } |
            Sort-Object Name |
            Select-Object -ExpandProperty Name)
        if ($commonDlls.Count -gt 0) {
            $dllReportPath = Join-Path $runRoot "windows-local-vs-ci-dlls-$i.csv"
            Compare-BinarySet -ExpectedDir $localWindowsDir -ActualDir $ciBinDir -Names $commonDlls -Label "windows common DLL local-vs-ci iteration $i" -ReportPath $dllReportPath
            $summary["checks"] += $dllReportPath
        }

        if ($previousCiDir) {
            $repeatReport = Join-Path $runRoot "windows-ci-repeat-$($i - 1)-vs-$i.csv"
            Compare-BinarySet -ExpectedDir $previousCiDir -ActualDir $ciBinDir -Names $windowsBinaries -Label "windows CI repeatability $($i - 1)-vs-$i" -ReportPath $repeatReport
            $summary["checks"] += $repeatReport
        }
        $previousCiDir = $ciBinDir
    }
}

if ($Platform -in @("linux", "all")) {
    Write-Step "Linux local-vs-CI parity"
    $localLinuxDir = if (-not [string]::IsNullOrWhiteSpace($LinuxLocalDir)) {
        (Resolve-Path -LiteralPath $LinuxLocalDir).Path
    }
    elseif ($SkipEmuCompile) {
        Join-Path $AkkStackRoot "server\bin"
    }
    else {
        Invoke-LocalLinuxBuild
    }

    $previousCiDir = $null
    for ($i = 1; $i -le $Iterations; $i++) {
        $ciBinDir = if ($LinuxCiDirs.Count -ge $i) {
            (Resolve-Path -LiteralPath $LinuxCiDirs[$i - 1]).Path
        }
        else {
            Invoke-LinuxCiBuild -OutputDir (Join-Path $runRoot "linux-ci-$i") -Iteration $i
        }
        $reportPath = Join-Path $runRoot "linux-local-vs-ci-$i.csv"
        Compare-BinarySet -ExpectedDir $localLinuxDir -ActualDir $ciBinDir -Names $linuxBinaries -Label "linux local-vs-ci iteration $i" -ReportPath $reportPath
        $summary["checks"] += $reportPath

        if ($previousCiDir) {
            $repeatReport = Join-Path $runRoot "linux-ci-repeat-$($i - 1)-vs-$i.csv"
            Compare-BinarySet -ExpectedDir $previousCiDir -ActualDir $ciBinDir -Names $linuxBinaries -Label "linux CI repeatability $($i - 1)-vs-$i" -ReportPath $repeatReport
            $summary["checks"] += $repeatReport
        }
        $previousCiDir = $ciBinDir
    }
}

$summary.completed_at = [DateTimeOffset]::Now.ToString("o")
$summaryPath = Join-Path $runRoot "summary.json"
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

if (-not $KeepWorkRoot) {
    Write-Step "Reports kept at $runRoot"
}

Write-Step "Release parity validation passed. Summary: $summaryPath"
