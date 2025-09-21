# jpg

## Initialisation USB CDC

La fonction `comm_usb_init()` installe le pilote TinyUSB, enregistre le callback de réception CDC puis initialise le port. Si l'enregistrement du callback échoue, l'initialisation est interrompue et deux traces d'erreur apparaissent dans le journal :

1. `Failed to register CDC RX callback: <esp_err_name>` pour fournir la cause exacte remontée par `tinyusb_cdcacm_register_callback()`.
2. `cdc cb failed` (via `ESP_RETURN_ON_ERROR`) indiquant que l'initialisation du canal série USB s'est arrêtée.

Dans ce cas, vérifier la configuration TinyUSB (alim, câblage USB, enumération hôte) avant de relancer le firmware.
