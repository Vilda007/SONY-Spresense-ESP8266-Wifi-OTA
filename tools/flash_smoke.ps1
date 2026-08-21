# tools/flash_smoke.ps1 — minimal D1 mini Pro smoke test (no WiFi, no relay).
#
# Compiles d1_smoke (no libraries, just ESP core + Serial) and uploads to
# the port given by -CredFile or D1_PORT env var. Expects to see:
#   SMOKE_BOOT
#   ChipID=...
#   FlashRealSize=...
#   Heap=...
#   ALIVE 1, ALIVE 2, ALIVE 3, ...
# on Serial @115200. If the chip is dead, the only output will be the
# boot ROM garbage and possibly a Guru Meditation / Exception 0.
#
# Usage:
#   notepad $env:USERPROFILE\.d1_test_pass   # set port=COM9
#   .\tools\flash_smoke.ps1

param(
  [string]$CredFile = (Join-Path $env:USERPROFILE ".d1_test_pass")
)

$ErrorActionPreference = "Stop"

$cli  = "C:\Program Files\Arduino CLI\arduino-cli.exe"
$fqbn = "esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200"

$port = $env:D1_PORT
if (Test-Path $CredFile) {
  Get-Content $CredFile | ForEach-Object {
    $line = $_.Trim()
    if ($line -eq "" -or $line.StartsWith("#")) { return }
    $kv = $line -split "=", 2
    if ($kv.Length -ne 2) { return }
    if ($kv[0].Trim().ToLower() -eq "port") { $port = $kv[1].Trim() }
  }
}
if ([string]::IsNullOrEmpty($port)) {
  Write-Error "port not set in $CredFile or D1_PORT env var"
  exit 1
}

Write-Host "Compiling d1_smoke ..."
& $cli compile --fqbn $fqbn --output-dir build_d1_smoke d1_smoke
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Uploading to $port ..."
& $cli upload -p $port --fqbn $fqbn d1_smoke
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Done. Read COM$port @115200."
