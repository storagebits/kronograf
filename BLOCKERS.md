# Blocages

## B1 — Montre non détectée en USB (résolu, 2026-10-12)

**Symptôme :** aucun périphérique série USB. `system_profiler SPUSBDataType` ne liste qu'un hub, un récepteur Logitech et un adaptateur DisplayPort. Aucun `/dev/cu.usbserial-*`, `cu.wchusbserial*` ni `cu.SLAB_USBtoUART*`.

**Hypothèses testées :**
1. Pilote absent → non : `com.silabs.cp210x` 6.0.2 est chargé ; les CH9102 sont gérés nativement par macOS récent.
2. Montre éteinte → peu probable comme seule cause : le pont USB-série est alimenté par le bus et s'énumère même montre éteinte.
3. Restent : câble « charge seule », port / hub défaillant, connecteur micro-USB de la montre.

**Conséquence (règle 2) :** sauvegarde de la flash impossible → **aucune écriture sur la montre**. Le travail continue en simulateur (Tranche 0 : audit, chaîne de compilation, simulateur, captures) ; la vérification matérielle reprendra dès que le port apparaîtra.

**Piste suivante :** câble USB data connu bon, branché directement sur le Mac (sans hub) ; vérifier avec `ls /dev/cu.*`.

**Mise à jour 2026-10-12 :** après changement de câble, le port est apparu (`/dev/cu.usbserial-XXXX`, CP210x). `chip_id`/`flash_id` ont réussi : **ESP32-D0WDQ6 rev v1.0**, 40 MHz, flash Winbond (ef/4018) **16 Mo**. Puis le port a **disparu** pendant le `read_flash` à 921600 bauds et n'est pas revenu → connexion intermittente (fiche micro-USB mal enfoncée ou câble fragile). Aucune écriture effectuée ; aucune sauvegarde partielle conservée.

**Résolu :** fiche réenfoncée ; sauvegarde complète réussie à 230 400 bauds (cf. DECISIONS D8).

## B2 — Décrochages USB récurrents (ouvert, 2026-10-12)

**Symptôme :** pendant un téléversement, le CP210x disparaît du bus USB ; les nœuds `/dev/cu.usbserial-XXXX` et `cu.SLAB_USBtoUART` restent mais toute ouverture échoue (`termios.error: (22, 'Invalid argument')`).
**Cause probable :** même faux contact que B1 (fiche micro-USB de la montre).
**Contournement :** travail sans montre (simulateur), flash groupé au retour du port ; l'utilisateur réenfonce la fiche. Piste durable : câble court, montre posée à plat, ou OTA Wi-Fi (Tranche 4) pour ne plus dépendre du câble.

**Mise à jour (2026-10-12) :** les décrochages surviennent aussi **pendant un flash**, ESP32 dans son chargeur ROM (notre firmware ne tourne pas) : c'est donc matériel (connecteur micro-USB / câble), pas logiciel. Ils ne redémarrent pas la montre (vu : décrochage à 10:56:49, montre restée éveillée, `uptime` continu). Contournements : flash par blocs de 64 Ko vérifiés et repris (`tools/flash_chunks.py`), OTA en mode atelier dès que possible, console TCP.

## B3 — Gel au réveil d'une échéance, montre figée jusqu'à extinction forcée (2026-10-12)

**Symptôme :** pendant un test de minuteur (écran éteint, USB instable), la montre ne répondait plus ni en Wi-Fi ni en série, bouton sans effet ; extinction forcée nécessaire. Aucun coredump, aucun reset de chien de garde (`reset=1` après rallumage).
**Hypothèses et correctifs appliqués :**
1. `gpio_wakeup_disable()` après le sommeil léger remettait les broches AXP202/BMA423 en « pas d'interruption » : après un premier sommeil léger, bouton et poignet ne réveillaient plus rien tant que la montre restait éveillée. **Corrigé** (restauration NEGEDGE/POSEDGE).
2. Micro-coupures USB (connecteur) : bascule Wi-Fi atelier on/off en boucle et sommeil léger entre deux. **Corrigé** (anti-rebond 3 s, `power_usb_stable()`).
3. L'échéance n'était programmée que par l'UI (écran allumé). **Corrigé** (la gestion d'énergie interroge le modèle).
**Filet de sécurité tenté :** chien de garde RTC (90 s) + miettes de pain en mémoire RTC. Testé par un sommeil léger sans source de réveil : **la montre n'a pas redémarré**, même après avoir effacé `RTC_CNTL_WDT_PAUSE_IN_SLP` (le code de sommeil d'ESP-IDF semble reprendre la main sur le RWDT, ou le redémarrage reste bloqué au boot — non vérifié). Le RWDT est conservé pour les gels éveillés ; le scénario testé (sommeil sans réveil) ne peut pas se produire dans Kronograf, qui programme toujours un réveil minuté ≤ 60 s.
**Piste suivante :** si un gel se reproduit, lire les miettes de pain (`status` → ligne « garde ») ; envisager le passage à ESP-IDF pur pour maîtriser la configuration du RWDT en sommeil.

**Mise à jour (2026-10-12, 11 h) — cause principale trouvée et corrigée.** Sur batterie, la montre se bloquait dès le premier sommeil léger (écran éteint ~20 s après usage) : noir complet, bouton sans effet, extinction forcée nécessaire. Ce chemin n'avait jamais tourné sur la montre (l'USB empêche le sommeil léger). Cause retenue : le RWDT armé par `guard.cpp` (action `RESET_SYSTEM`, `WDT_PAUSE_IN_SLP` effacé) restait actif pendant `esp_light_sleep_start()`. **Correctif** : RWDT suspendu autour du sommeil léger (`guard_suspend`), réarmé au réveil ; action `RESET_RTC` ; bit `PAUSE_IN_SLP` laissé à sa valeur par défaut. **Validé** sur montre en mode `battsim 1` (comportement batterie, journal série) : cycles sommeil léger → réveil minuté répétés sans blocage (`event lsleep in/out`).

**Mise à jour (11 h 20) — second défaut, corrigé.** Une fois le RWDT écarté, les réveils minutés fonctionnaient mais **un appui sur le bouton figeait la montre** (dernière trace : `lsleep in`, jamais de `lsleep out`). Cause : `gpio_wakeup_enable()` passe la broche en interruption *de niveau* alors que l'ISR Arduino reste active ; au réveil la ligne INT de l'AXP202 reste basse jusqu'à l'acquittement I²C, donc l'interruption se redéclenche sans fin sur le cœur 1 (tempête), qui ne revient jamais au code. **Correctif** : `gpio_intr_disable()` sur les deux broches avant le sommeil, types front + `gpio_intr_enable()` après. Validé par l'utilisateur (montre en `battsim 1`) : réveils au bouton répétés sans blocage. Reste à confirmer sur vraie batterie et pour le réveil poignet/double tape (même chemin).
