$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$url = "http://127.0.0.1:8765/"
$serverScript = Join-Path $root "scripts\occ_server.py"
$listener = Get-NetTCPConnection -LocalPort 8765 -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
  throw "Python is required to serve the OCC dashboard on port 8765."
}

if ($listener) {
  $process = Get-CimInstance Win32_Process -Filter "ProcessId = $($listener.OwningProcess)" -ErrorAction SilentlyContinue
  $isOccServer = $process -and $process.CommandLine -like "*occ_server.py*"

  if (-not $isOccServer) {
    Stop-Process -Id $listener.OwningProcess -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 750
    $listener = $null
  }
}

if (-not $listener) {
  Start-Process -FilePath $python.Source `
    -ArgumentList "`"$serverScript`"" `
    -WorkingDirectory $root `
    -WindowStyle Minimized | Out-Null

  Start-Sleep -Seconds 1
}

Start-Process $url | Out-Null
Write-Output "OCC available at $url"
