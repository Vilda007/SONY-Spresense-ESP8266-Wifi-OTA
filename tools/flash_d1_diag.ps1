# tools/flash_d1_diag.ps1 — minimal D1 mini diagnostic flash.
#
# Compiles d1_diag with TEST_SSID/TEST_PASS build flags and uploads. Used
# to verify a suspect ESP8266 module (chip id, flash, WiFi.begin behaviour)
# without the full relay stack. Output on Serial1 (GPIO2/D4) requires a
# separate USB-serial adapter; Serial (UART0) is reserved for the link.
#
# Inputs (in priority order):
#   1. env vars:    D1_PORT, D1_TEST_SSID, D1_TEST_PASS
#   2. credential file path from -CredFile (default: %USERPROFILE%\.d1_test_pass)
#
# Usage (PowerShell):
#   notepad $env:USERPROFILE\.d1_test_pass
#   .\tools\flash_d1_diag.ps1

$ErrorActionPreference = "Stop"

param(
  [string]$CredFile = (Join-Path $env:USERPROFILE ".d1_test_pass")
)

$cli  = "C:\Program Files\Arduino CLI\arduino-cli.exe"
$fqbn = "esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200"

$port = $null; $ssid = $null; $pass = $null
if (Test-Path $CredFile) {
  Get-Content $CredFile | ForEach-Object {
    $line = $_.Trim()
    if ($line -eq "" -or $line.StartsWith("#")) { return }
    $kv = $line -split "=", 2
    if ($kv.Length -ne 2) { return }
    $k = $kv[0].Trim().ToLower()
    $v = $kv[1].Trim()
    switch ($k) {
      "port" { $port = $v }
      "ssid" { $ssid = $v }
      "pass" { $pass = $v }
    }
  }
}

if ($env:D1_PORT)      { $port = $env:D1_PORT }
if ($env:D1_TEST_SSID) { $ssid = $env:D1_TEST_SSID }
if ($env:D1_TEST_PASS) { $pass = $env:D1_TEST_PASS }

foreach ($pair in @("port=$port","ssid=$ssid")) {
  if ([string]::IsNullOrEmpty($pair.Split("=",2)[1])) {
    Write-Error "Missing credential: $pair. Set via -CredFile or D1_* env vars."
    exit 1
  }
}
if ([string]::IsNullOrEmpty($pass)) { $pass = "" }   # pass can be empty for open networks

$extra = "-DTEST_SSID=`"$ssid`" -DTEST_PASS=`"$pass`""

Write-Host "Compiling diag (SSID=$ssid) ..."
& $cli compile --fqbn $fqbn --build-property "build.extra_flags=$extra" --output-dir build_d1_diag d1_diag
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Uploading to $port ..."
& $cli upload -p $port --fqbn $fqbn d1_diag
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Done. Connect a USB-serial adapter to GPIO2/D4 (TX) at 115200 baud"
Write-Host "      and read its output to see chip id, flash info and WiFi status."
