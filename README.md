# jpg

## Objectif
Système d'affichage d'images JPEG pour ESP32-S3 avec interface tactile LVGL, stockage externe sur carte SD et liaisons de communication USB CDC, CAN et RS485.

## Architecture matérielle
### Résumé MCU et alimentations
- **SoC** : ESP32-S3 avec PSRAM octale activée (16 MB Flash, SPIRAM initialisée au boot) selon `sdkconfig.defaults`.
- **Horloges LCD** : pixel clock 51,2 MHz, double frame buffer, synchronisation H/V conforme aux timings 1024×600 (`APP_LCD_*`).

### Bus LCD RGB 16 bits
| Fonction | GPIO ESP32-S3 |
| --- | --- |
| HSYNC | GPIO46 |
| VSYNC | GPIO3 |
| DE | GPIO5 |
| PCLK | GPIO7 |
| Data D0…D15 | GPIO14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40 |
Ces liaisons alimentent `esp_lcd_rgb_panel` avec double buffering LVGL situé en PSRAM.

### Expander I²C CH422 (lignes critiques)
| Fonction | Broche CH422 |
| --- | --- |
| Sélection USB/CAN | PIN4 |
| Reset tactile GT911 | PIN0 |
| Reset LCD | PIN2 |
| Alimentation LCD | PIN5 |
| PWM rétroéclairage | PIN1 |
| CS carte SD | PIN3 |
Les broches sont pilotées via `ch422_driver`, assurant mise sous tension LCD, impulsions de reset et sélection USB ↔ CAN.

### Bus I²C & tactile
- Hôte I²C0 @ 400 kHz sur GPIO8 (SDA) et GPIO9 (SCL).
- Interruption tactile GT911 sur GPIO4, communication via `esp_lcd_touch_gt911` et `esp_lcd_panel_io_i2c`.

### Carte SD SPI + CH422
- Bus SPI2 : MOSI GPIO11, MISO GPIO13, CLK GPIO12, DMA automatique. CS géré par `CH422_PIN_SD_CS` pour libérer les GPIOs.
- Montage via `esp_vfs_fat_sdspi_mount` et hôte personnalisé `sdspi_ch422_host`.

### Interfaces de communication
- **USB CDC** : TinyUSB CDC ACM0, sélectionnée par défaut (`APP_USB_SEL_ACTIVE_USB = 0`).
- **CAN (TWAI)** : TX GPIO20, RX GPIO19, bascule via CH422 lorsque `comm_can_start` est appelé (niveau logique = 1).
- **RS485 half-duplex** : UART1 TX GPIO15, RX GPIO16, DE non câblé (GPIO_NC), débit par défaut 115200 8N1.

## Pré-requis ESP-IDF
1. Installer ESP-IDF ≥ 5.3 (contrainte `idf_component.yml`).
2. Initialiser le projet :
   ```bash
   idf.py set-target esp32s3
   idf.py fullclean  # recommandé lors du premier portage
   ```
3. Configurer l'environnement (`. ./export.sh`) avant `idf.py`. Les options par défaut sont injectées via `sdkconfig.defaults` (flash QIO 80 MHz, PSRAM octale, console USB Serial/JTAG, FATFS optimisé, LVGL log).

## Configuration LVGL & PSRAM
- PSRAM obligatoire : `CONFIG_SPIRAM=y`, allocations LVGL redirigées vers `app_lvgl_psram_alloc()` avec 1 MiB réservé.
- LVGL 9.x, double buffer DMA en PSRAM (`fb_in_psram=1`, `buff_spiram=1`), couleurs RGB565, rotation désactivée, `LV_USE_LOG` et compteurs de performance activés par défaut.
- Interaction LVGL ↔ FATFS garantie par le correctif de liaison dans `main/CMakeLists.txt` (defines `LV_FS_FATFS_LETTER='S'` et `LV_FS_FATFS_PATH="/sdcard"`).

## Dépendances de composants (`idf_component.yml`)
- `idf >= 5.3.0` : APIs ESP-IDF modernes (esp_lcd, esp_lvgl_port, tinyUSB, TWAI).
- `lvgl/lvgl ^9.1.0` & `espressif/esp_lvgl_port ^2.6.1` : moteur graphique et glue code.
- `espressif/esp_lcd_touch(_gt911)` : pilote tactile capacitif.
- `espressif/esp_tinyusb ^1.4.0` : USB CDC ACM.
Ces dépendances sont résolues automatiquement par `idf.py` au moment du `CMake configure`/`build`.

## Préparation des supports de stockage
### Carte SD externe
1. Formater la carte en FAT32 (allocation 4–32 KiB). Si le firmware ne peut pas monter la carte, activer `format_if_mount_failed=true` dans `sd_card_config_t` ou reformater (voir logs `sdcard`).
2. Créer/copier la structure suivante :
   ```text
   /sdcard/gallery        # Images .jpg / .jpeg (max 512)
   /sdcard/.thumbnails    # Généré automatiquement, peut être vidé pour forcer la régénération
   ```
   Les dossiers sont créés au démarrage via `ensure_directory()`, mais il est conseillé de les préparer hors-ligne pour contrôler les droits POSIX et accélérer le premier scan.
3. Déposer les images JPEG (jusqu'à 1024×600). Le redimensionnement et la génération de vignettes s'effectuent en tâche de fond.

### Partition interne `storage` (FAT)
- Partition de 7 MiB déclarée dans `partitions.csv`, dédiée à des ressources persistantes (paramètres utilisateur, assets).
- Générer une image optionnelle :
  ```bash
  python $IDF_PATH/components/fatfs/fatfsgen.py --size 7M storage_dir storage.bin
  esptool.py --chip esp32s3 write_flash <offset_storage> storage.bin
  ```
  Obtenir `offset_storage` via `idf.py partition-table` avant écriture.

## Procédure de flash
1. Construire : `idf.py build`.
2. Flasher l'application `factory` :
   ```bash
   idf.py -p /dev/ttyACM0 erase-flash   # première programmation
   idf.py -p /dev/ttyACM0 flash
   ```
   Le binaire est placé dans la partition `factory` (6 MiB).
3. (Optionnel) Injecter la partition FAT `storage` : `esptool.py write_flash <offset_storage> storage.bin`.
4. Surveiller : `idf.py -p /dev/ttyACM0 monitor` pour visualiser les logs UART/USB.

## Guide utilisateur
### Navigation LVGL
1. **Accueil** : bouton « Galerie » vers l'écran de miniatures.
2. **Galerie** : grille responsive. Appui sur une vignette charge l'image, geste gauche/droite dans la visionneuse pour passer à l'image suivante/précédente.
3. **Visionneuse** : overlay supérieur avec retour accueil, zoom (×1 ↔ ×2) et rotation par pas de 90°. Gestes tactiles pour la navigation séquentielle.
4. **Réglages** : curseur de luminosité (PWM CH422) et interrupteur slideshow. Modifications propagées immédiatement à l'afficheur et au timer de slideshow.

### Commande slideshow
- Intervalle par défaut : 6 s (`APP_GALLERY_SLIDESHOW_INTERVAL_MS`). Activation/désactivation via l'interrupteur LVGL ou l'API `gallery_set_slideshow_enabled()` (utile pour scripts internes). Les vignettes sont rafraîchies automatiquement lors de l'activation.

### Interfaces USB / CAN / RS485
- **USB CDC** : exposé sur TinyUSB ACM0, buffers 512 octets. Tout paquet reçu est journalisé (`app: USB RX …`). Utiliser un terminal série 115200 8N1 via le port USB principal.
- **CAN (TWAI)** : lancement via `comm_can_start()` sélectionne physiquement la voie CAN et configure 500 kbit/s (250/125 kbit/s disponibles). Les trames reçues sont loguées. En cas d'échec d'initialisation, la sélection CH422 est rétablie sur USB.
- **RS485** : UART1 half-duplex 115200 bps. Un thread FreeRTOS lit en continu et relaie les données au callback utilisateur (`RS485 RX …`). Pour piloter la direction DE, connecter la broche à une E/S libre et mettre à jour `APP_RS485_DE_GPIO`.

## Diagnostic & résolution d'incidents
### Journaux série
- Console par USB Serial/JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). Utiliser `idf.py monitor` ou tout terminal sur le port USB-JTAG.
- Tags principaux : `app`, `disp`, `sdcard`, `gallery`, `usb`, `can`, `rs485`. Ajuster le niveau via `esp_log_level_set()` si besoin.

### Erreurs fréquentes
| Message | Cause probable / Action |
| --- | --- |
| `Failed to register CDC RX callback` + `cdc cb failed` | TinyUSB CDC non initialisé (alimentation USB, câble, drivers). Vérifier la pile TinyUSB avant redémarrage.
| `sdcard: failed to mount card` | Carte non formatée ou bus SPI mal câblé. Reformater (FAT32) ou activer `format_if_mount_failed`.
| `Gallery start failed` / `thumbnail decode failed` | Fichiers corrompus ou hors limites. Supprimer la vignette dans `/sdcard/.thumbnails` ou vérifier la résolution/extension.
| `CAN start failed` | Bus absent/mauvaise charge. Inspecter la terminaison 120 Ω et vérifier l'alimentation du transceiver. Le firmware repasse automatiquement en mode USB.

Pour un diagnostic approfondi, activer `CONFIG_LV_LOG_LEVEL_DEBUG` et `CONFIG_TINYUSB_DEBUG` via `menuconfig`, puis reconstruire.
