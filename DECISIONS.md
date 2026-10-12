# Décisions

Chaque entrée : décision — justification. Les plus récentes en bas.

## Phase 0 — 2026-10-12

**D1. Nom : « Kronograf ».** Conservé : court, nordique, évoque l'horlogerie sans marque existante. Le dossier reste `~/projects/kronograph`, créé par l'utilisateur (le brief disait `~/projets/kronograf`, chemin inexistant ; `~/projects` est le vrai dossier de travail).

**D2. Référence :** `~/projects/My-TTGO-Watch` (lecture seule). C'est un clone upstream vierge ; les goûts de l'utilisateur se lisent plutôt dans son projet **bttfWatch** de 2021 (voir `docs/AUDIT.md`).

**D3. Révision matérielle : non encore détectée** (montre absente du bus USB, voir `BLOCKERS.md` B1). Hypothèse de travail **V1**, d'après `config.h` de bttfWatch (`LILYGO_WATCH_2020_V1`), à confirmer par `esptool chip_id/flash_id` et un scan I²C/UART avant tout flash. La révision est un paramètre de compilation (`-D KG_BOARD_V1|V2|V3`) **et** l'inventaire est vérifié à l'exécution (scan I²C) : un périphérique optionnel absent du scan n'apparaît pas dans l'UI.

**D4. Nouveau projet plutôt que fork.** My-TTGO-Watch : LVGL 7, framework Arduino forké figé en 2021, ~40 apps génériques et un système d'auto-enregistrement — exactement l'anti-pattern « OS de montre » du brief. On reprend la connaissance (séquences AXP202/BMA423, brochage, pièges), pas le code.

**D5. Pile :**
- PlatformIO + **Arduino-ESP32 2.0.x** (`espressif32@6.x`, IDF 4.4) : NimBLE, Wi-Fi, NVS, OTA disponibles ; IDF pur n'apporte pas de gain décisif ici et allonge le développement.
- **LVGL 8.3.11** plutôt que 9.x : API stable et bien documentée, empreinte RAM moindre, `lv_snapshot` et rotation d'image disponibles ; la 9.x a eu des régressions de perf sur ESP32 et un portage SDL plus mouvant. Réévaluable si un besoin de la 9 apparaît.
- Pilotes fins maison : ST7789 via `esp_lcd` (SPI DMA), FT6336 et PCF8563 en registres I²C directs. **XPowersLib** (lewisxhe, maintenu) pour l'AXP202, **SensorLib/BMA423** pour l'accéléromètre (le firmware de fonctionnalités Bosch est indispensable au pas/tilt/double tap). Pas de TTGO_TWatch_Library (monolithique, LVGL 7 embarquée).

**D6. Sécurité alimentation (règle 3) :** la HAL n'expose **aucune** fonction de réglage de DCDC3 ni de coupure de DCDC3/LDO2-ESP. La pratique de My-TTGO-Watch qui abaisse DCDC3 en veille n'est pas reprise. LDO2 (rétroéclairage + ST7789 sur V1) reste allumé ; l'écran passe en `SLPIN` et le PWM à 0.

**D7. Simulateur :** CMake + SDL2 (Homebrew), sources `ui/` partagées à l'identique avec la cible ESP32, HAL simulée pilotable. Mode headless qui écrit des PNG via `stb_image_write` (domaine public, un seul en-tête).

## Tranche 0 — matériel — 2026-10-12

**D8. Sauvegarde d'origine faite.** `backups/twatch-original-20261012.bin` (hors git) — 16 777 216 octets, **SHA-256 `bda0e42e9bc03c051208ea4511217f8fbeff7cd537400aa86ae74b2397ed7898`**. Lue par blocs de 1 Mo à 230 400 bauds (`tools/backup_flash.sh`), chaque bloc vérifié MD5 par esptool ; 460 800 bauds produisait du bruit série sur ce câble. Signature bootloader 0xE9 à 0x1000. Table de partitions d'origine : nvs 20K @0x9000, otadata, app0/app1 6400K, spiffs 3520K @0xC90000 (16 Mo, deux OTA — firmware probablement My-TTGO-Watch). Restauration : `esptool.py --port <port> --baud 230400 write_flash 0 backups/twatch-original-20261012.bin`.

**D9. Puce :** ESP32-D0WDQ6 rev v1.0, quartz 40 MHz, flash Winbond 16 Mo. Compatible V1/V2 (la V3 embarque normalement une ESP32 rev3) ; build V1 en premier, confirmé par le scan I²C et l'écran.

**D10. Port série :** `/dev/cu.usbserial-*` (pilote Apple), pas `/dev/cu.SLAB_USBtoUART` (pilote Silicon Labs, échecs répétés). Téléversement à 230 400 bauds.
