# ESP32 Projects

**English** | [Español](README.es.md)

A collection of ESP-IDF firmware projects for ESP32-C3, ESP32-C5, and
ESP32-S3 boards. The repository contains three distinct types of device:

- Wi-Fi repeaters that share an existing network through a new access point;
- configurable buttons and digital-sensor nodes that trigger HTTP, MQTT, or
  WebSocket actions;
- experimental devices that detect changes in Wi-Fi propagation compatible
  with movement.

Each folder is a self-contained project with its own hardware requirements,
flashing instructions, configuration guide, and limitations.

## Choose a project

| I want to... | Recommended project | Board |
|---|---|---|
| Extend a home WPA2-Personal Wi-Fi network | [WiFi-Repeater](./WiFi-Repeater) | ESP32-C3 SuperMini |
| Extend an enterprise network that uses a username and password | [WiFi-Repeater-WPA2-Enterprise](./WiFi-Repeater-WPA2-Enterprise) | ESP32-C3 SuperMini |
| Trigger two HTTP or MQTT actions with physical buttons | [Smartbutton ESP32-C3 SuperMini](./Smartbutton-esp32-C3-Supermini) | ESP32-C3 SuperMini |
| Trigger five HTTP or MQTT actions | [Smartbutton ESP32-C3 SuperMini Plus 5](./Smartbutton-esp32-C3-Supermini-Plus-5) | ESP32-C3 SuperMini Plus |
| Connect PIR, microwave-radar, button, or other digital-output sensors | [Smartbutton ESP32-C3 Sensors](./Smartbutton-esp32-C3-Supermini-SENSORS) | ESP32-C3 SuperMini |
| Use a two-button node on dual-band Wi-Fi 6 | [Smartbutton ESP32-C5](./Smartbutton-esp32-c5) | ESP32-C5 |
| Add WebSocket actions to the ESP32-C5 button node | [Smartbutton ESP32-C5 WS](./Smartbutton-esp32-c5-ws) | ESP32-C5 |
| Experiment with motion sensing through changes in a router-to-ESP32 radio link | [WiFi Motion RSSI ESP32-C3](./WiFi-Motion-RSSI-C3-Supermini) | ESP32-C3 SuperMini |
| Run the same experiment with a small local display | [WiFi Motion RSSI ESP32-C3 OLED](./WiFi-Motion-RSSI-ESP32-C3-OLED-0.42) | 01Space ESP32-C3 0.42 OLED |
| Run the radio-link experiment on an ESP32-S3 | [WiFi Motion RSSI ESP32-S3](./WiFi-Motion-RSSI-ESP32-S3) | ESP32-S3 |
| Observe several nearby access points without joining them | [WiFi Motion RSSI C3 AP Scanner](./WiFi-Motion-RSSI-C3-AP-Scanner) | ESP32-C3 SuperMini |

## Wi-Fi repeaters

These projects connect to an existing Wi-Fi network as a station and create a
separate access point for client devices. NAPT routes traffic between both
networks.

### WiFi-Repeater

[WiFi-Repeater](./WiFi-Repeater) extends a conventional WPA2-Personal network.
It provides a captive setup portal, a responsive web dashboard, network
scanning, connectivity tests, persistent configuration, and web-based firmware
updates.

### WiFi-Repeater-WPA2-Enterprise

[WiFi-Repeater-WPA2-Enterprise](./WiFi-Repeater-WPA2-Enterprise) is the variant
for networks that use EAP-PEAP or EAP-TTLS authentication, commonly found in
companies and educational institutions. It also adds TCP/UDP port forwarding
and a web log viewer.

## Smart buttons and sensor inputs

These devices join your Wi-Fi network and execute a configured action when a
physical input changes. Depending on the project, an input can send an HTTP
GET/POST request, publish an MQTT message, or send a WebSocket payload.

| Project | Inputs | Actions | Local feedback |
|---|---:|---|---|
| [ESP32-C3 SuperMini](./Smartbutton-esp32-C3-Supermini) | 2 buttons | HTTP, MQTT | Onboard blue LED |
| [ESP32-C3 SuperMini Plus 5](./Smartbutton-esp32-C3-Supermini-Plus-5) | 5 buttons | HTTP, MQTT | Onboard WS2812 RGB LED |
| [ESP32-C3 Sensors](./Smartbutton-esp32-C3-Supermini-SENSORS) | 3 configurable digital inputs | HTTP, MQTT | Onboard blue LED |
| [ESP32-C5](./Smartbutton-esp32-c5) | 2 buttons | HTTP, MQTT | WS2812 RGB LED and two button LEDs |
| [ESP32-C5 WS](./Smartbutton-esp32-c5-ws) | 2 buttons | HTTP, MQTT, WebSocket | WS2812 RGB LED and two button LEDs |

The **Sensors** variant is not itself a PIR or radar detector. It is an
ESP32-C3 interface for up to three external devices with a digital output, such
as a PIR sensor, microwave radar, or mechanical button. It adds a startup
stabilization period and a cooldown to reduce repeated triggers.

## Experimental motion sensing over Wi-Fi

These projects measure how a Wi-Fi radio signal changes over time. Movement of
a person or object can alter reflections and paths between a transmitter and
the ESP32; the firmware classifies sufficiently large changes as
motion-compatible events.

> [!IMPORTANT]
> These devices detect **changes in radio propagation**, not people. They
> cannot identify or count occupants, may not detect a motionless person, and
> can react to other environmental changes. They are experiments, not
> certified presence sensors or security alarms.

### Router-linked variants

The following variants join a configured Wi-Fi network and analyze RSSI, CSI,
or both. They include calibration, a bilingual web interface, live charts,
Telegram notifications, and experimental telemetry.

| Project | What makes it different |
|---|---|
| [ESP32-C3 SuperMini](./WiFi-Motion-RSSI-C3-Supermini) | Base compact ESP32-C3 implementation |
| [ESP32-C3 OLED 0.42](./WiFi-Motion-RSSI-ESP32-C3-OLED-0.42) | Adds a 72×40 OLED for local state, IP address, RSSI, and scores |
| [ESP32-S3](./WiFi-Motion-RSSI-ESP32-S3) | Port for conventional ESP32-S3 boards, with console and telemetry over UART0 |

### Standalone AP scanner

[WiFi Motion RSSI C3 AP Scanner](./WiFi-Motion-RSSI-C3-AP-Scanner) looks for
motion-compatible changes in the RSSI of several nearby Wi-Fi access points. It
does **not** connect to those networks and does not need or store their
passwords. Instead, it:

- automatically or manually selects up to eight SSIDs and tracks their BSSIDs;
- learns a baseline from stable nearby access points;
- combines signal deviations using an adaptive threshold;
- creates its own local Wi-Fi network with a captive dashboard;
- reports insufficient reference coverage separately from motion.

This variant works locally and includes ready-to-flash USB firmware. It does
not provide Telegram notifications or OTA updates.

## Related external repositories

These ESP32-S3 projects are maintained in separate repositories and are listed
here for discovery:

- [esp32-s3-tailscale-enterprise](https://github.com/soyunomas/esp32-s3-tailscale-enterprise):
  WPA2-Enterprise repeater, NAPT, web UI, and Tailscale subnet routing.
- [esp32-s3-tailscale-marauder](https://github.com/soyunomas/esp32-s3-tailscale-marauder):
  adds authorized lab-oriented HID automation, DuckyScript support, stored
  macros, and scheduled execution.

## Before flashing

Open the selected project's README before connecting or flashing a board.
Pinouts, ESP-IDF versions, flash offsets, prebuilt images, initial credentials,
and update methods differ between projects.

In general:

- configuration is stored in NVS where documented;
- some projects support OTA updates, while others must be updated over USB;
- smart buttons may use deep sleep, but Wi-Fi motion experiments must remain
  active to collect measurements;
- default access-point and web credentials should be changed before regular
  use.

## License

This repository is distributed under the [MIT License](LICENSE). Check the
license file inside an individual project when present.
