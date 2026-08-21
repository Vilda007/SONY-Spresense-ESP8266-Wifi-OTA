# tools/flash_d1_test.ps1 — isolated WiFi test build & flash for the D1 mini Pro (ESP8266EX).
#
# Compiles d1_mini_relay with TEST_DIRECT_WIFI and the credentials read from
# either env vars OR a credential file, then uploads over USB. Used to verify
# the D1 WiFi/HTTP stack end-to-end without the Spresense master (D1 alone on
# USB, no Serial2 wires connected).
#
# Inputs (in priority order):
#   1. env vars:    D1_PORT, D1_TEST_SSID, D1_TEST_PASS, D1_TEST_URL
#   2. credential file path from -CredFile (default: %USERPROFILE%\.d1_test_pass)
#      Format of the file (one per line, '#' = comment):
#        port=COM9
#        ssid=<your-ssid>
#        pass=<your-wifi-password>
#        url=http://<your-server>:8080/
#
# The credentials are embedded in the firmware image as TEST_SSID / TEST_PASS /
# TEST_SERVER_URL constants. Do NOT keep such a build running on a board that
# will later be flashed with a production build — re-flash with a clean (no
# -DTEST_DIRECT_WIFI) build to scrub the secrets.
#
# Usage (PowerShell):
#   # Either set env vars and call without -CredFile:
#   $env:D1_PORT="COM9"
#   $env:D1_TEST_SSID="<your-ssid>"
#   $env:D1_TEST_PASS="<your-wifi-password>"
#   $env:D1_TEST_URL="http://<your-server>:8080/"
#   .\tools\flash_d1_test.ps1
#
#   # OR put creds in a file (recommended — never appears in shell history):
#   notepad $env:USERPROFILE\.d1_test_pass
#   .\tools\flash_d1_test.ps1 -CredFile $env:USERPROFILE\.d1_test_pass

param(
  [string]$CredFile = (Join-Path $env:USERPROFILE ".d1_test_pass")
)

$ErrorActionPreference = "Stop"

$cli  = "C:\Program Files\Arduino CLI\arduino-cli.exe"
# FQBN for WEMOS D1 mini Pro (ESP8266EX, 16 MB flash chip, 80 MHz crystal).
# Arduino ESP8266 toolchain only supports up to 4M logical layout — the
# physical 16 MB module behaves like 4 MB to the linker. d1_mini pinout
# matches both D1 mini and D1 mini Pro.
$fqbn = "esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200"

# Load credentials from file (preferred — never appears in shell history).
$port = $null; $ssid = $null; $pass = $null; $url = $null
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
      "url"  { $url  = $v }
    }
  }
}

# Env vars override file values (so the file is optional once env is set).
if ($env:D1_PORT)        { $port = $env:D1_PORT }
if ($env:D1_TEST_SSID)   { $ssid = $env:D1_TEST_SSID }
if ($env:D1_TEST_PASS)   { $pass = $env:D1_TEST_PASS }
if ($env:D1_TEST_URL)    { $url  = $env:D1_TEST_URL }

foreach ($pair in @("port=$port","ssid=$ssid","pass=$pass","url=$url")) {
  if ([string]::IsNullOrEmpty($pair.Split("=",2)[1])) {
    Write-Error "Missing credential: $pair. Set via -CredFile or D1_* env vars."
    exit 1
  }
}

# Build the -D flags. arduino-cli forwards build.extra_flags verbatim to the compiler.
$extra = "-DTEST_DIRECT_WIFI -DTEST_SSID=`"$ssid`" -DTEST_PASS=`"$pass`" -DTEST_SERVER_URL=`"$url`""

Write-Host "Compiling with TEST_DIRECT_WIFI (SSID=$ssid URL=$url, port=$port) ..."
& $cli compile --fqbn $fqbn --build-property "build.extra_flags=$extra" --library lib/relay_proto --output-dir build_d1 d1_mini_relay
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Uploading to $port ..."
& $cli upload -p $port --fqbn $fqbn d1_mini_relay
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Done. Read COM$port to verify (expect: WiFi OK IP=..., SELFTEST POST -> 200 ACK:hello)."
