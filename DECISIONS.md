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

**D11. Premier flash (Tranche 0) vérifié.** Démarrage propre ; scan I²C : AXP202, PCF8563, BMA423, FT6336 présents. PSRAM 4 Mo. `status` : heap interne 74 % libre (222 Ko / 298 Ko, budget ≥ 30 %), PSRAM 4,19 Mo libres, LVGL 18 %. RTC avec bit VL levé (heure non fiable) → réglage NTP en Tranche 1. Capture série montre = simulateur (`docs/screens/{sim,device}/t0-diag.png`). En charge, la valeur batterie passe au vert au lieu d'ajouter « Charge · » (débordait sur l'heure, vu sur la capture montre).

**D12. Écran noir au premier flash — corrigé.** Symptôme : rétroéclairage allumé, image noire, alors que la capture LVGL était correcte. Cause : sans broche RST, `esp_lcd` fait un reset logiciel puis `SLPOUT` après 20 ms ; si la dalle était active (redémarrage à chaud), la fiche ST7789 exige 120 ms → `SLPOUT` ignoré, dalle en veille. Correctif : 150 ms après le reset, `SLPOUT` renvoyé, 120 ms. Validé par photo de l'utilisateur (`docs/photos/t0-diag-photo.jpg`) : orientation, ordre RGB et inversion corrects. Ajout de la commande `lcdtest` (mire R|V|B hors LVGL).

**D13. Révision V1 confirmée** : le rétroéclairage répond sur GPIO12 (V2 : GPIO25, V3 : GPIO15), vibreur et quatre périphériques I²C présents.

**D14. SPI écran plafonné à 40 MHz.** Le brochage T-Watch (MOSI 19, SCLK 18) ne correspond aux broches IO_MUX natives d'aucun hôte SPI : les signaux passent par la matrice GPIO, limitée à 40 MHz sur ESP32. 80 MHz n'est pas atteignable de façon fiable. Conséquence : une image pleine (115 Ko) prend ~23 ms → les animations à 60 fps devront redessiner des zones partielles (aiguilles, segments), pas l'écran entier.

## Tranche 1 — 2026-10-12

**D15. Validé sur montre :** NTP (Wi-Fi allumé ~3 s puis coupé, heure écrite en UTC dans le PCF8563), tactile sans inversion (coin HG → (29,28), BD → (196,210)), glissé circulaire de la lunette, extinction auto à 10 s, réveil et extinction au bouton. **Reste à valider :** réveil poignet levé et double tap (remappage BMA423 `0x88` = axes identité, non vérifié — l'utilisateur ne peut pas porter la montre pour l'instant).

**D16. Trotteuse :** 8 Hz, ~17–20 ms par image à 240 MHz (rendu + SPI) pour 15–21 kpx, soit ~15 % CPU. Aiguilles vectorielles (`lv_line` sur boîte englobante) retenues ; l'option images pivotées n'a pas été prototypée : la rotation d'image LVGL 8 de 100 px de long coûterait un rendu par transformation de chaque pixel de la boîte, sans gain de netteté sur des bâtons droits.

**D17. Mode atelier (avancé de la Tranche 4).** Les décrochages USB (B2) empêchaient de valider les tranches. Sur USB **et** avec Wi-Fi configuré : Wi-Fi maintenu, OTA (`pio run -e twatch-v1-ota -t upload`, `kronograf.local`) et console TCP 2323 (`tools/kg.py --host kronograf.local …`). Débranchée : Wi-Fi coupé. Justification vis-à-vis de l'anti-pattern « Wi-Fi permanent » : sur secteur, l'autonomie n'est pas en jeu ; désactivable par `-D KG_WORKSHOP=0`. Mot de passe OTA facultatif (`KG_OTA_PASS`).

**D18. Pièges notés :** `sntp_get_sync_status()` remet l'état à zéro à la lecture ; ouvrir le port série sur macOS redémarre l'ESP32 (auto-reset CP210x) ; les logs d'autres tâches s'intercalent dans la sortie série (captures indexées par rangée + nouvel essai).

## Tranche 2 — 2026-10-12

**D19. Cadran 3 = « Nuit »** (heure seule, Barlow Condensed 104 px, rouge sombre `#9A140C`, rétroéclairage à 25 % via `hal_brightness_scale`, une mise à jour par minute) plutôt qu'une fusion des deux univers : les deux premiers cadrans sont déjà riches ; il manquait un cadran sobre, lisible la nuit sans éblouir, et le moins coûteux en énergie (aucune animation, rétroéclairage réduit).

**D20. Afficheurs 7 segments dessinés en code** (`src/ui/seg7.c`, polygones anti-crénelés, inclinaison 0,09) plutôt qu'une police 7 segments : contrôle total des segments éteints (mélange 18/255), du halo (deux polygones élargis semi-transparents), du filament central et du scintillement à l'allumage (courbe 40→200→70→230→150→255 sur 180 ms, seulement sur les segments nouvellement allumés). Étiquettes au pochoir : Allerta Stencil (OFL).

**D21. Données des lignes :** rouge = prochaine alarme (tirets + « AUCUNE ALARME » tant que la Tranche 3 n'existe pas) ; vert = présent ; ambre = dernière synchro NTP (plus parlante que « dernier réveil », qui serait toujours « à l'instant »). Easter egg : appui long sur la ligne verte, ou 10 000 pas franchis (une fois par jour) → trois flashs d'éclairs ramifiés sur 0,7 s + vibreur ; le son de décharge viendra avec le pilote I²S (Tranche 6).

**D22. Transitions par instantanés.** `lv_scr_load_anim` recomposait les deux cadrans à chaque image : 43 ms/image (≈ 23 fps, sous le plancher de 30). Désormais : deux `lv_snapshot` en PSRAM (2 × 115 Ko) puis glissement de deux images → **25 ms/image en moyenne (≈ 40 fps)**, limité par le SPI ; une image isolée à ~94 ms au premier rendu du vrai cadran. Bonus : on n'appelle plus `lv_scr_load_anim`, qui **plante en LVGL 8.3.11** si on l'appelle pendant une transition (`scr_load_internal` remet `scr_to_load` à NULL avant `lv_obj_set_pos`) ; les demandes reçues pendant une transition sont mises en file.

**D23. Réglages persistants** (NVS, espace `kg`) : `face` (écrit 5 s après le dernier changement — jamais pendant une transition, pas d'usure à chaque glissé), `brightness`. Vérifié par redémarrage.

**D24. Blocage au réveil (chien de garde) — corrigé.** Symptôme : après quelques cycles veille/réveil en mode atelier, plus de réponse puis `reset=6` (TWDT). Coredump : cœur 0 accaparé par la tâche `wifi`. Cause : `setCpuFrequencyMhz(80)` appelé à chaque tour de boucle écran éteint, et 240 MHz au réveil, **avec le Wi-Fi actif** — ce qui casse la pile Wi-Fi d'Arduino-ESP32. Correctif : fréquence changée seulement sur transition réelle et jamais Wi-Fi allumé ; pas de sommeil léger Wi-Fi allumé. Validé : 30 cycles veille/réveil/cadran sans reset ni coredump (avant : échec en < 3 cycles).

**D25. Outillage de diagnostic :** boucle principale sous chien de garde 8 s en mode panique (coredump au lieu d'un reset muet), nourri pendant l'OTA ; commandes `coredump`, `coredump raw` (décodage par `esp-coredump` avec toutes les piles), `reboot` ; chaque build archive son ELF dans `build-archive/<sha256[:16]>.elf` (l'empreinte inscrite dans les coredumps) — un dump est toujours décodable.

## Tranche 3 — 2026-10-12

**D26. Lanceur = grille de « sous-cadrans »** (2 × 3, boutons ronds creusés comme les compteurs du cadran aviateur, pictogrammes vectoriels dessinés en code) plutôt qu'une liste : cohérent avec l'univers horloger, tout tient sur un écran sans défilement. Gestes : glissé vers le haut depuis un cadran → lanceur ; vers le bas → retour au cadran ; glissé à droite dans une app → lanceur. Transitions par instantanés (module `transition.c`, commun aux cadrans).

**D27. Modèle sans minuteur LVGL** (`model.c`) : chronomètre, minuteur et alarmes sont calculés à partir d'horodatages (tick pour le chrono, heure murale pour minuteur et alarmes) ; ils continuent écran éteint et en sommeil léger. La gestion d'énergie interroge `model_next_event_time()` pour programmer son réveil minuté et rallume l'écran à l'échéance ; le « shell » de l'UI affiche alors l'alerte (vibration + carillon, répétés toutes les 2,2 s, une minute max ; alarme non acquittée → répétition auto à +5 min). Validé sur montre : minuteur lancé écran éteint → réveil `échéance` → écran d'alerte.

**D28. Sons synthétisés** : tâche audio dédiée (cœur 0), sinus avec attaque/relâche de 6 ms (pas de clic), I²S 16 kHz vers le MAX98357A ; l'ampli (LDO3, rail audio de la V1 — pas un rail ESP32) n'est alimenté que pendant la lecture. Vibreur piloté par `esp_timer` (motifs non bloquants), retour haptique de 12 ms sur chaque touche.

**D29. Pas sur 7 jours** : au premier passage d'un nouveau jour, le compte du BMA423 est poussé dans un historique NVS glissant (`st0`…`st6`) avant remise à zéro ; les jours où la montre n'a pas tourné valent 0.

**D30. Wi-Fi saisi sur la montre** (Réglages → Wi-Fi : scan asynchrone, clavier tactile, mot de passe masqué et effacé de l'écran après validation), stocké en NVS (`kgnet`) et prioritaire sur `secrets.h`. Le scan n'allume le Wi-Fi que le temps du scan. Clavier en Montserrat 14 (seule police avec les symboles du clavier LVGL ; nos polices générées sont `const`, sans police de secours).

**D31. Réglages** : luminosité (curseur), mise en veille 5/10/15/30 s, cadran par défaut (appliqué au retour sur les cadrans), format 24 h (cadrans numériques), à propos. Tous persistants (NVS).

**D32. Robustesse (cf. B3)** : restauration des interruptions GPIO après sommeil léger ; anti-rebond USB 3 s ; écritures TCP « tout ou rien » et chien de garde nourri pendant les commandes longues (une capture Wi-Fi dépassait les 8 s du chien de garde — vu au coredump) ; chien de garde RTC + miettes de pain RTC (efficacité limitée en sommeil léger, documentée en B3).

**D33. Sommeil léger fiabilisé (cf. B3) : chien de garde RTC suspendu et interruptions GPIO coupées pendant le sommeil.** Le RWDT ne surveille plus que la montre éveillée ; il est coupé juste avant `esp_light_sleep_start()` et réarmé au réveil, avec l'action `RESET_RTC`. Le laisser compter pendant le sommeil bloquait la montre sur batterie au premier endormissement. Mode de test `battsim 1` (console) : simule la batterie montre branchée, pour observer le sommeil léger sur le câble série.

## Tranche 4 — 2026-10-12

**D34. Réseau par « travaux » à la demande.** Une seule tâche de fond (cœur 0, priorité basse, pile 8 Ko pour TLS) exécute les travaux en attente — synchro NTP, météo, bus — sur une même connexion, puis coupe le Wi-Fi (sauf mode atelier sur USB). Au démarrage : NTP + météo en une connexion. Écran éteint, rien ne déclenche de Wi-Fi ; le sommeil léger reste interdit tant que le Wi-Fi est allumé.

**D35. Météo Open-Meteo en HTTP simple** (pas de clé, coordonnées publiques de Montréal, réponse ~1,3 Ko) : pas de TLS, donc ni mémoire mbedTLS ni poignée de main. Analyseur JSON minimal maison (`src/data/kg_weather.c`, testé sur une vraie réponse : `tests/test_data.c`), pas de dépendance ArduinoJson. Cache persistant en NVS (`kgwx`) : l'app et la complication affichent le dernier relevé dès le démarrage, avec son âge (« RELEVÉ IL Y A 7 MIN », « HORS LIGNE » en ambre si la dernière mise à jour a échoué — rien n'est effacé). Rafraîchissement : à l'ouverture de l'app si > 10 min ; par le shell (écran allumé seulement) si > 30 min.

**D36. Bus STM : GTFS temps réel lu en flux.** Le flux TripUpdates fait ~1 Mo (1 277 entités, la plus grande ~3 Ko) : il n'est jamais stocké. Un `Stream` reçoit les octets de `HTTPClient::writeToStream` (qui gère le découpage « chunked ») et les passe à un analyseur protobuf maison (`src/data/kg_bus.c`) qui met une entité à la fois dans un tampon PSRAM de 16 Ko et ne garde que les passages aux arrêts suivis (12 au plus, triés). HTTPS vérifié avec la racine DigiCert Global Root G2 épinglée (`certs.h`, expire en 2038) : la clé API n'est jamais envoyée à un tiers. Arrêts saisis sur la montre (Réglages → Bus STM, clavier numérique, « . » sépare plusieurs codes) ou via la console (`bus arrets 52345,52346`). Le bus n'est rafraîchi que l'app ouverte (toutes les 60 s) : pas de complication bus sur le cadran, un téléchargement de 1 Mo par minute écran allumé coûterait trop. Sans clé : section Réglages absente, ligne d'indice dans l'app.

**D37. App « Météo » dans la 6e case du lanceur**, plutôt qu'une app Bus séparée : la grille 2 × 3 reste sans défilement. Pictogrammes météo vectoriels (`wx_icon.c`, 9 familles de codes OMM), complication température + pictogramme dans le coin bas gauche du cadran aviateur (symétrique du jour / de la batterie en haut). Polices régénérées avec « ° », le vrai signe moins (U+2212) et À Ç Ê Î Ô au pochoir.

## Tranche 5 — 2026-10-12

**D38. ANCS avec NimBLE-Arduino 2.5.1** plutôt que la pile Bluedroid d'Arduino (≈ 100 Ko de RAM de plus). Hôte NimBLE en PSRAM (`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`), une seule connexion, pas de scan, journaux coupés. La montre est périphérique et annonce la sollicitation du service ANCS (type AD 0x15) : elle apparaît dans Réglages → Bluetooth de l'iPhone. Elle devient client GATT de l'iPhone sur la même connexion (`NimBLEServer::getClient`). Protocole décodé en C pur testé hôte (`src/data/kg_ancs.c` : événements, requête d'attributs, réassemblage de la Data Source, nettoyage UTF-8 vers nos polices, noms d'apps).

**D39. Appairage avec code (MITM) et non « Just Works »** : capacité « affichage seul », code à 6 chiffres tiré par `esp_random()`, affiché en grand sur la montre (l'écran s'allume), saisi sur l'iPhone. Liens chiffrés mémorisés par NimBLE en NVS ; « Oublier l'iPhone » dans les Réglages.

**D40. Place mémoire pour le BLE.** Le contrôleur BLE lié faisait déborder la DRAM (12 Ko) et l'IRAM (2,9 Ko). DRAM : réserve LVGL de 64 Ko déplacée en PSRAM (`LV_MEM_POOL_ALLOC`), RAM statique 38 % → 22 %. IRAM : `esp_ringbuf` placé en flash par un `sections.ld` régénéré à chaque compilation (`tools/ld_ringbuf_flash.py`, équivalent de l'option IDF `RINGBUF_PLACE_FUNCTIONS_INTO_FLASH`, sûr car aucune ISR en IRAM n'utilise de tampon circulaire).

**D41. Énergie : pas de sommeil léger avec le BLE actif.** Cette carte n'a pas de quartz 32 kHz ; le contrôleur BLE d'Arduino-ESP32 prend alors son horloge basse consommation sur le quartz principal (`BTDM_CTRL_LPCLK_SEL_MAIN_XTAL`), ce qui interdit le sommeil léger. Écran éteint, la montre reste en « modem sleep » à 80 MHz. Intervalle de connexion demandé à l'iPhone : 300–330 ms, latence 4. Le Bluetooth se coupe dans les Réglages (retour au sommeil léger). Mesure d'autonomie : voir RAPPORT.

**D42. Mode sans échec** : trois plantages d'affilée dans la première minute → le BLE ne démarre pas (Wi-Fi atelier et OTA restent disponibles). Garde-fou indispensable pour mettre à jour à distance une montre dont le câble USB est peu fiable (B2).

**D43. UI des notifications** : carte plein écran à l'arrivée (vibration courte, écran allumé), « Effacer » (supprime aussi sur l'iPhone, action négative ANCS) ou « Refuser » pour un appel ; liste des 8 dernières par glissé vers le bas depuis un cadran ; pastille orange du nombre sur le cadran aviateur ; pictogrammes de catégorie vectoriels.
