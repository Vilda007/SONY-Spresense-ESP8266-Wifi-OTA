# tools/flash_wifi_test.ps1 — compile and upload the bare WiFi-only sketch.
#
# Tests whether WiFi.begin() itself crashes this particular D1 mini Pro
# (no relay stack, no HTTP, no Strings, no frame parser). If the WiFi
# sketch crashes, the ESP8266 module is bad.

param(
  [string]$CredFile = (Join-Path $env:USERPROFILE ".d1_test_pass")
)

$ErrorActionPreference = "Stop"

$cli  = "C:\Program Files\Arduino CLI\arduino-cli.exe"
$fqbn = "esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200"

$port = $env:D1_PORT
$ssid = $env:D1_TEST_SSID
$pass = $env:D1_TEST_PASS
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

if ([string]::IsNullOrEmpty($port) -or [string]::IsNullOrEmpty($ssid)) {
  Write-Error "port/ssid missing in $CredFile or D1_PORT/D1_TEST_SSID env vars"
  exit 1
}
if ([string]::IsNullOrEmpty($pass)) { $pass = "" }

$extra = "-DTEST_SSID=`"$ssid`" -DTEST_PASS=`"$pass`""

Write-Host "Compiling d1_wifi_test ..."
& $cli compile --fqbn $fqbn --build-property "build.extra_flags=$extra" --output-dir build_d1_wifi_test d1_wifi_test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Uploading to $port ..."
& $cli upload -p $port --fqbn $fqbn d1_wifi_test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Done. Read COM$port @115200 after pressing RST."
