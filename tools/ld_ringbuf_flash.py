# PlatformIO (extra_scripts, pre): frees ~3.8 KB of IRAM for the BLE stack.
#
# Arduino-ESP32 2.0.17's precompiled sections.ld puts esp_ringbuf in IRAM; with the BLE
# controller linked in, IRAM overflows by ~2.9 KB. This generates a copy of the script with
# esp_ringbuf moved to flash — the equivalent of ESP-IDF's official
# CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH option, safe here because no IRAM interrupt uses a
# ring buffer (Arduino's UART ISR is not in IRAM: CONFIG_UART_ISR_IN_IRAM is not defined).
# The modified script is placed ahead of the framework's in the linker search path; it is
# regenerated on every build, so it follows framework updates.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

fw = env.PioPlatform().get_package_dir("framework-arduinoespressif32")  # noqa: F821
src = os.path.join(fw, "tools", "sdk", "esp32", "ld", "sections.ld")
out_dir = os.path.join(env.subst("$BUILD_DIR"), "ld")  # noqa: F821
os.makedirs(out_dir, exist_ok=True)

text = open(src).read()
iram_rule = "    *libesp_ringbuf.a:(.literal .literal.* .text .text.*)\n"
assert text.count(iram_rule) == 1, "unexpected sections.ld: esp_ringbuf IRAM rule not found"
text = text.replace(iram_rule, "")
text = text.replace("EXCLUDE_FILE(*libesp_ringbuf.a ", "EXCLUDE_FILE(")
open(os.path.join(out_dir, "sections.ld"), "w").write(text)
env.Prepend(LIBPATH=[out_dir])  # noqa: F821
