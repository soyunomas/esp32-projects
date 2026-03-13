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

### 🔲 TODO (próximas sesiones)
- [ ] Probar flash en hardware real y verificar acceso a internet
- [ ] Ajustar parámetros NAPT si hay problemas de rendimiento
- [ ] Añadir OTA updates via web
- [ ] Añadir gráficas de tráfico en tiempo real (Chart.js embebido o canvas)
- [ ] Añadir log viewer en la web UI
- [ ] Añadir autenticación para el panel web (usuario/contraseña)
- [ ] Añadir mDNS para acceder por nombre (ej: repeater.local)
- [ ] Optimizar uso de memoria (heap monitoring)
- [ ] Añadir watchdog y recovery automático ante desconexiones
- [ ] Testing de estabilidad prolongada (24h+)

---

## Notas Técnicas
- ESP32-C3 soporta STA+AP simultáneo de forma nativa
- NAPT (Network Address Port Translation) permite routing entre STA y AP
- lwIP debe tener IP_NAPT y IP_FORWARD habilitados en sdkconfig
- La web UI se embebe en flash usando EMBED_FILES en CMake
- El captive portal redirige DNS queries al IP del AP (192.168.4.1)
