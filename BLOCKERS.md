# Blocages

## B1 — Montre non détectée en USB (ouvert, 2026-10-12)

**Symptôme :** aucun périphérique série USB. `system_profiler SPUSBDataType` ne liste qu'un hub, un récepteur Logitech et un adaptateur DisplayPort. Aucun `/dev/cu.usbserial-*`, `cu.wchusbserial*` ni `cu.SLAB_USBtoUART*`.

**Hypothèses testées :**
1. Pilote absent → non : `com.silabs.cp210x` 6.0.2 est chargé ; les CH9102 sont gérés nativement par macOS récent.
2. Montre éteinte → peu probable comme seule cause : le pont USB-série est alimenté par le bus et s'énumère même montre éteinte.
3. Restent : câble « charge seule », port / hub défaillant, connecteur micro-USB de la montre.

**Conséquence (règle 2) :** sauvegarde de la flash impossible → **aucune écriture sur la montre**. Le travail continue en simulateur (Tranche 0 : audit, chaîne de compilation, simulateur, captures) ; la vérification matérielle reprendra dès que le port apparaîtra.

**Piste suivante :** câble USB data connu bon, branché directement sur le Mac (sans hub) ; vérifier avec `ls /dev/cu.*`.

**Mise à jour 2026-10-12 :** après changement de câble, le port est apparu (`/dev/cu.usbserial-XXXX`, CP210x). `chip_id`/`flash_id` ont réussi : **ESP32-D0WDQ6 rev v1.0**, 40 MHz, flash Winbond (ef/4018) **16 Mo**. Puis le port a **disparu** pendant le `read_flash` à 921600 bauds et n'est pas revenu → connexion intermittente (fiche micro-USB mal enfoncée ou câble fragile). Aucune écriture effectuée ; aucune sauvegarde partielle conservée.
