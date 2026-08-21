# tools/flash_d1_recovery.ps1 — full erase + bootloader reflash for D1 mini (Pro).
#
# Useful when the user firmware crashes during WiFi.begin() (Exception 0),
# the boot ROM misbehaves, or the flash layout is corrupted. Steps:
#   1. erase_flash
#   2. flash boot_v1.x.bin at 0x00000000
#   3. flash esp_init_data_default.bin at 0x00001000 (0xFC000 for 4MB variant)
#   4. flash blank.bin at 0x0007E000 (system param) and 0x0007FC00 (RF cal)
#   5. compile + upload user sketch (with creds from -CredFile)
#
# Inputs: -CredFile path (default ~/.d1_test_pass)
#         env vars D1_PORT, D1_TEST_SSID, D1_TEST_PASS, D1_TEST_URL

param(
  [string]$CredFile = (Join-Path $env:USERPROFILE ".d1_test_pass")
)

$ErrorActionPreference = "Stop"

$cli      = "C:\Program Files\Arduino CLI\arduino-cli.exe"
$fqbn     = "esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200"
$coreRoot = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp8266\hardware\esp8266\3.1.2"
$bootDir  = Join-Path $coreRoot "tools\esptool\test\images\esp8266_sdk"

$boot = Join-Path $bootDir "boot_v1.4(b1).bin"
$init = Join-Path $bootDir "esp_init_data_default.bin"
$blnk = Join-Path $bootDir "blank.bin"

foreach ($f in @($boot, $init, $blnk)) {
  if (-not (Test-Path $f)) { Write-Error "Missing $f"; exit 1 }
}

# --- credentials ---
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
if ($env:D1_PORT)        { $port = $env:D1_PORT }
if ($env:D1_TEST_SSID)   { $ssid = $env:D1_TEST_SSID }
if ($env:D1_TEST_PASS)   { $pass = $env:D1_TEST_PASS }
if ($env:D1_TEST_URL)    { $url  = $env:D1_TEST_URL }
if ([string]::IsNullOrEmpty($port) -or [string]::IsNullOrEmpty($ssid)) {
  Write-Error "Missing port/ssid in $CredFile or env vars."
  exit 1
}
if ([string]::IsNullOrEmpty($pass)) { $pass = "" }
if ([string]::IsNullOrEmpty($url))  { $url  = "" }

# --- step 1: erase flash via raw esptool ---
# arduino-cli's "erase-flash" subcommand exists only in newer versions; use esptool directly
# because it is guaranteed to be present (bundled with the ESP8266 core).
$py = (Get-Command python.exe -ErrorAction SilentlyContinue).Source
if (-not $py) { $py = (Get-Command py.exe -ErrorAction SilentlyContinue).Source }
if (-not $py) { Write-Error "python.exe not found on PATH"; exit 1 }

$esptool = Join-Path $coreRoot "tools\esptool\esptool.py"
if (-not (Test-Path $esptool)) {
  Write-Error "esptool.py not found at $esptool"
  exit 1
}

$esptoolArgs = @(
  $esptool, "--chip", "esp8266", "--port", $port, "--baud", "115200",
  "--before", "default_reset", "--after", "hard_reset"
)

Write-Host "Erasing flash on $port ..."
& $py @esptoolArgs erase_flash
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# --- step 2-4: write bootloader + init data + blanks ---
Write-Host "Writing bootloader at 0x0 ..."
& $py @esptoolArgs write_flash 0x00000000 $boot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Writing esp_init_data_default at 0x3FC000 ..."
& $py @esptoolArgs write_flash 0x3FC000 $init
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Writing blank at 0x3FE000 (system param) ..."
& $py @esptoolArgs write_flash 0x3FE000 $blnk
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# --- step 5: build + upload user sketch with TEST_DIRECT_WIFI ---
$extra = "-DTEST_DIRECT_WIFI -DTEST_SSID=`"$ssid`" -DTEST_PASS=`"$pass`" -DTEST_SERVER_URL=`"$url`""

Write-Host "Compiling d1_mini_relay (TEST_DIRECT_WIFI) ..."
& $cli compile --fqbn $fqbn --build-property "build.extra_flags=$extra" --library lib/relay_proto --output-dir build_d1 d1_mini_relay
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Uploading to $port ..."
& $cli upload -p $port --fqbn $fqbn d1_mini_relay
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Recovery done. Press RST on the D1 and watch COM$port for boot messages."
