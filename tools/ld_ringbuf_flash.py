# PlatformIO (extra_scripts, pre) : libère ~3,8 Ko d'IRAM pour la pile BLE.
#
# Le sections.ld précompilé d'Arduino-ESP32 2.0.17 place esp_ringbuf en IRAM ; avec le
# contrôleur BLE lié, l'IRAM déborde de ~2,9 Ko. On génère une copie du script où
# esp_ringbuf part en flash — l'équivalent de CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH,
# option officielle d'ESP-IDF, sûre ici car aucune interruption en IRAM n'utilise de tampon
# circulaire (l'ISR UART d'Arduino n'est pas en IRAM : CONFIG_UART_ISR_IN_IRAM n'est pas
# défini). Le script modifié est placé devant celui du framework dans le chemin de l'éditeur
# de liens ; il est régénéré à chaque compilation, donc suit les mises à jour du framework.
import os

Import("env")  # noqa: F821  (fourni par PlatformIO)

fw = env.PioPlatform().get_package_dir("framework-arduinoespressif32")  # noqa: F821
src = os.path.join(fw, "tools", "sdk", "esp32", "ld", "sections.ld")
out_dir = os.path.join(env.subst("$BUILD_DIR"), "ld")  # noqa: F821
os.makedirs(out_dir, exist_ok=True)

text = open(src).read()
iram_rule = "    *libesp_ringbuf.a:(.literal .literal.* .text .text.*)\n"
assert text.count(iram_rule) == 1, "sections.ld inattendu : règle IRAM d'esp_ringbuf introuvable"
text = text.replace(iram_rule, "")
text = text.replace("EXCLUDE_FILE(*libesp_ringbuf.a ", "EXCLUDE_FILE(")
open(os.path.join(out_dir, "sections.ld"), "w").write(text)
env.Prepend(LIBPATH=[out_dir])  # noqa: F821
