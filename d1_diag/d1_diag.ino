// d1_diag.ino — minimal D1 mini Pro diagnostic sketch (see test_ssid_pass.h).
// Usage: arduino-cli compile --build-property 'build.extra_flags=-DTEST_SSID="..." -DTEST_PASS="..."' d1_diag
//        arduino-cli upload -p COM9 --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 d1_diag

#include "test_ssid_pass.h"
