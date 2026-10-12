# Audit — travail précédent sur la T-Watch 2020

Date : 2026-10-12. Sources examinées (lecture seule) :

| Emplacement | Nature |
|---|---|
| `~/projects/My-TTGO-Watch` | Clone de l'upstream `sharandac/My-TTGO-Watch` (le brief indiquait `~/projets/…`, chemin inexistant) |
| Projet PlatformIO personnel (2021) | Projet PlatformIO de mars 2021 : **bttfWatch** (cadrans « Retour vers le futur ») |
| `TTGO_TWatch_Library` | Bibliothèque officielle LilyGO v1.4.1 (LVGL 7.7.2 embarquée) |

## 1. My-TTGO-Watch

### Versions
- Plateforme `espressif32@3.3.0` + framework Arduino **remplacé** par un fork (`sharandac/arduino-esp32-hedge`) pour la DFS / flash 80 MHz.
- **LVGL 7.11.0** (7.7.2 dans l'env émulateur 2020). Deux versions majeures de retard (API v8/v9 incompatible).
- Bibliothèques : TTGO_TWatch_Library (fork `dervomsee/lvgl_update`), ESPAsyncWebServer, AsyncTCP, ArduinoJson 6, ESP32SSDP, IRremoteESP8266 ≤2.8.4, PubSubClient, ESP8266Audio 1.9.5, ESP8266SAM, FTP server, ESP32-targz, TinyGPSPlus, NimBLE-Arduino ^1.3.8.
- Partitions : `default_16MB.csv`.

### Architecture
- `src/hardware/*` : un module par périphérique (pmu, display, framebuffer, touch, motion, rtcctl, motor, sound, wifictl, blectl, gpsctl, timesync, powermgm), communication par **callbacks d'événements** (`callback.cpp`).
- `src/gui/*` : mainbar à tuiles (main / app / note / setup), statusbar, quickbar, widgets, splash, screenshot.
- `src/app/*` : ~40 apps chargées via « autocall » (enregistrement automatique).
- Émulateur SDL2 (`hal/sdl2`, `support/sdl2_build_extra.py`) qui compile la même UI : **le principe est bon et je le reprends**, pas le code.

### Personnalisations
**Aucune.** `git status` propre, HEAD = dernier commit upstream (d6ab1c4, 2023-09-11), aucune branche locale, aucun stash, aucun commit de l'utilisateur. Seuls `.pio/` et `.vscode/` (ignorés) attestent d'une compilation locale.

### Réutilisable (en connaissance, pas en copie de code)
- Séquence d'init AXP202 : limite de charge 300 mA, cible 4,2 V, ADC batterie/VBUS, IRQ VBUS/charge, extinction EXTEN/DCDC2/LDO4 inutilisés sur V1.
- BMA423 : remap des axes, features STEP_CNTR + TILT + WAKEUP (double tap), INT1 sur GPIO39 front montant.
- Veille légère : réveil GPIO niveau bas sur AXP202_INT (35) et niveau haut sur BMA423_INT1 (39), puis `esp_light_sleep_start()`.
- Rétroéclairage LEDC 8 bits.

### Daté ou à proscrire
- **⚠ `setDCDC3Voltage()` en veille** (`pmu.cpp:594-653`) : abaisse la tension du cœur ESP32. **Interdit par la règle 3 du brief** : non repris.
- Coupure de LDO2 en veille (`pmu.cpp:607`) : sur la **V1, LDO2 alimente à la fois le rétroéclairage et le ST7789** (commentaire de la lib LilyGO). Le couper est acceptable (l'ESP32 est sur DCDC3), mais impose de réinitialiser l'écran au réveil. Décision : garder LDO2 allumé et utiliser `SLPIN` du ST7789 + PWM à 0.
- Framework Arduino forké et figé (2021) : non maintenu.
- LVGL 7 : API obsolète, pas de `lv_snapshot`, rotation d'image moins performante.
- Dépendances lourdes inutiles pour notre périmètre (FTP, SSDP, MQTT, SAM, targz, AsyncWebServer).

### Pièges repérés
- La broche de rétroéclairage **diffère selon la révision** : V1 = GPIO12, V2 = GPIO25, V3 = GPIO15. GPIO25 est le WS I²S sur V1/V3 → un mauvais build allume le mauvais périphérique.
- GPIO12 (V1) est une broche de strapping (tension flash MTDI) : ne pas la tirer haut au boot.
- V2 : le tactile est réinitialisé via **EXTEN** de l'AXP202 ; LDO3 alimente écran + tactile ; LDO4 le GPS.
- V3 : LDO4 alimente l'audio, reset tactile sur GPIO14.
- `while( !ttgo->bma->readInterrupt() );` (`motion.cpp:295`) : boucle d'attente active potentiellement bloquante.

## 2. bttfWatch (projet personnel 2021)

Code de Markus Wobisch (licence MIT), récupéré et légèrement ajusté : configuré pour **`LILYGO_WATCH_2020_V1`**, 6 cadrans BTTF (« flux capacitor », afficheurs de destination…), 5 écrans de réglages, chronomètre.

**Ce que ça révèle des goûts :** univers « Retour vers le futur » assumé, afficheurs 7 segments, cadrans qui changent au réveil (aléatoire / cycle), gradation nocturne automatique (22 h – 7 h), affichage optionnel pas et batterie. CPU à 240 MHz en usage (commentaire : « la plus basse valeur pour laquelle le graphisme restait réactif »), 10 MHz en veille.

**Idées reprises :** gradation de nuit automatique, durées d'écran allumé sélectionnables (7/12/20 s), niveaux de luminosité discrets.

## 3. Brochage T-Watch 2020 (lib LilyGO 1.4.1)

| Signal | V1 | V2 | V3 |
|---|---|---|---|
| TFT MOSI / SCLK / CS / DC | 19 / 18 / 5 / 27 | idem | idem |
| Rétroéclairage | **12** | **25** | **15** |
| Tactile SDA/SCL/INT | 23 / 32 / 38 | idem (reset EXTEN) | idem, RST 14 |
| Capteurs SDA/SCL | 21 / 22 | idem | idem |
| RTC INT / AXP INT / BMA INT1 | 37 / 35 / 39 | idem | idem |
| Vibreur | 4 | — (non listé) | 4 |
| IR | 13 | 2 | 13 |
| I²S BCK/WS/DOUT | 26/25/33 | — | 26/25/33 |
| GPS RX/TX/1PPS/WAKE | — | 36/26/34/33 | — |
| SD CS/MISO/MOSI/SCLK | — | 13/4/15/14 | — |
