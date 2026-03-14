# Progress & TODO - ESP32-C3 WiFi Repeater

## Sesión 1 - 2026-03-12

### ✅ Completado
- [x] Verificado ESP-IDF v6.1-dev instalado en `/home/yo/esp/esp-idf/`
- [x] Creado AGENTS.md con descripción del proyecto y estructura
- [x] Creado PROGRESS.md (este archivo)
- [x] Creado estructura del proyecto ESP-IDF
- [x] Implementado `main.c` - Entry point con inicialización NVS, WiFi, Web
- [x] Implementado `config_storage.c/h` - Persistencia NVS (SSID, pass, AP config)
- [x] Implementado `wifi_manager.c/h` - WiFi STA+AP simultáneo con NAPT
- [x] Implementado `dns_server.c/h` - Captive portal DNS server
- [x] Implementado `web_server.c/h` - HTTP server + API REST (scan, config, status, clients)
- [x] Implementado `index.html` - SPA profesional dark theme
- [x] Implementado `styles.css` - Estilos responsive mobile-first
- [x] Implementado `app.js` - Lógica frontend (scan, config, stats real-time)
- [x] Configurado `CMakeLists.txt` (raíz + main)
- [x] Configurado `sdkconfig.defaults` para ESP32-C3 con NAPT
- [x] Configurado `partitions.csv` personalizado
- [x] Creado `README.md` con documentación completa

- [x] Compilación exitosa - firmware 880KB (52% libre), target ESP32-C3

## Sesión 2 - 2026-03-12 (Fix NAPT + Internet)

### ✅ Completado
- [x] **BUG FIX: NAPT se habilitaba en interfaz STA en vez de AP** — corregido usando `esp_netif_napt_enable(ap_netif)` en la interfaz AP (así los clientes del AP salen por STA)
- [x] **BUG FIX: DNS no se propagaba al DHCP del AP** — añadido `set_dhcps_dns()` que copia el DNS upstream del STA al DHCP server del AP (los clientes obtienen DNS real por DHCP)
- [x] **BUG FIX: Captive portal DNS interceptaba TODO siempre** — ahora se para el DNS captive cuando STA conecta, y se reactiva si STA se desconecta
- [x] **BUG FIX: STA no se marcaba como netif por defecto** — añadido `esp_netif_set_default_netif(sta_netif)` para routing correcto
- [x] Añadido endpoint `/api/ping` con `esp_ping` (ICMP + resolución DNS)
- [x] Añadido panel "Connectivity Test" en dashboard con ping interactivo
- [x] Compilación exitosa - firmware 905KB (51% libre)

### ✅ Verificado en hardware real
- [x] Probar flash en hardware real y verificar acceso a internet
- [x] Ajustar parámetros NAPT si hay problemas de rendimiento

## Sesión 3 - 2026-03-13 (OTA + Web Auth)

### ✅ Completado
- [x] **Particiones OTA**: cambiado de factory a ota_0/ota_1 (2x ~1.94MB) + otadata para 4MB flash
- [x] **Flash size**: corregido de 2MB a 4MB en sdkconfig.defaults
- [x] **OTA rollback**: habilitado `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, validación en main.c
- [x] **OTA via web**: endpoint `POST /api/ota` con upload binario raw, streaming por chunks 4KB
- [x] **Autenticación web**: HTTP Basic Auth en todos los endpoints `/api/*`
- [x] **Credenciales únicas**: password por defecto generado desde MAC (ej: `espXXXXXXXX`), impreso en serial log
- [x] **Login overlay**: pantalla de login en frontend con soporte sessionStorage
- [x] **Cambio de credenciales**: endpoint `POST /api/auth/change` + UI en pestaña System
- [x] **Frontend actualizado**: nueva pestaña "System" con sección OTA (progress bar) y gestión de credenciales
- [x] **Persistencia NVS**: campos web_user/web_pass añadidos a repeater_config_t
- [x] **base64 decode**: usando mbedtls_base64_decode (ya incluido en ESP-IDF)
- [x] Compilación exitosa - firmware 929KB (54% libre), particiones OTA 4MB

## Sesión 4 - 2026-03-14 (Factory Reset)

### ✅ Completado
- [x] **Factory Reset**: endpoint `POST /api/factory-reset` que borra NVS completa y reinicia
- [x] **UI Factory Reset**: sección en pestaña System con icono, descripción y doble confirmación
- [x] **Restart Device mejorado**: movido a sección propia con icono y descripción (consistencia visual)
- [x] **WiFi-Repeater/ actualizado**: copiados todos los archivos fuente, firmware y binarios
- [x] **README WiFi-Repeater actualizado**: añadidas secciones OTA, Auth, Factory Reset, credenciales, API completa
- [x] **README.md raíz actualizado**: añadido factory reset a características y tabla API
- [x] Compilación exitosa

### 🔲 TODO (próximas sesiones)
- [x] Probar OTA en hardware real (flash serial la primera vez por cambio de particiones)
- [x] Testing de estabilidad prolongada (24h+)
- [x] Captura de pantalla `system.png` para el README
- [ ] **mDNS** — Acceder por `repeater.local` en vez de IP
- [ ] **Auto-reconnect con backoff** — Reintentar conexión STA con delays progresivos ante desconexiones
- [ ] **Hostname configurable** — Nombre del dispositivo visible en la lista DHCP del router
- [ ] **Info de versión firmware** — Mostrar versión actual en System (build date, app version, partición activa)
- [ ] **Bandwidth/tráfico** — Contadores TX/RX bytes en dashboard (sin gráficas, solo números)
- [ ] **Log viewer** — Últimos N logs del ESP en la UI vía ring buffer
- [ ] **Watchdog + health check** — Reiniciar automático si pierde STA durante X minutos
- [ ] **Static IP** — Opción de configurar IP estática para STA (gateway, mask, DNS)
- [ ] **WiFi TX power configurable** — Ajustar potencia de transmisión desde la UI
- [ ] **Export/Import config** — Descargar/subir JSON de configuración completa

---

## Notas Técnicas
- ESP32-C3 soporta STA+AP simultáneo de forma nativa
- NAPT (Network Address Port Translation) permite routing entre STA y AP
- lwIP debe tener IP_NAPT y IP_FORWARD habilitados en sdkconfig
- La web UI se embebe en flash usando EMBED_FILES en CMake
- El captive portal redirige DNS queries al IP del AP (192.168.4.1)
