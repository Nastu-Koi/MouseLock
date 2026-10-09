param([string]$Executable, [string]$Snapshots)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Executable) { $Executable = Join-Path $root 'build\MouseLockGame.exe' }
if (-not $Snapshots) { $Snapshots = Join-Path $root 'build\notification-snapshots' }
New-Item -ItemType Directory -Path $Snapshots -Force | Out-Null
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $Executable
$start.Arguments = '--notification-smoke-test "' + $Snapshots + '"'
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$app = [System.Diagnostics.Process]::Start($start)
$elapsed = [System.Diagnostics.Stopwatch]::StartNew()
try {
    if (-not $app.WaitForExit(15000)) {
        $app.Kill()
        $app.WaitForExit()
        throw 'Native notification lifecycle test timed out'
    }
    $result = @{
        name = 'Native notification lifecycle and monitor scaling'
        exitCode = $app.ExitCode
        elapsedMs = $elapsed.ElapsedMilliseconds
        stdout = $app.StandardOutput.ReadToEnd()
        stderr = $app.StandardError.ReadToEnd()
    }
    $result | ConvertTo-Json -Compress
    if ($app.ExitCode -ne 0) { throw 'Native notification smoke test failed' }
} finally {
    $app.Dispose()
}
