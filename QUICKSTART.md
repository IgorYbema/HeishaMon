# HeishaMon quick start

**HeishaMon small** (ESP8266) and **HeishaMon large** (ESP32-S3) connect to your Panasonic Aquarea heat pump. They make its data available over MQTT and a web page, and they let you control it.

## 1. What you need
- A HeishaMon board (small or large).
- A cable for the heat pump's **CN-CNT** connector (or **CN-NMODE**, see below). The connector supplies the power, so the board doesn't need a separate power supply.
- A WiFi network (the large board can also use Ethernet).
- Optional: an MQTT broker, such as Mosquitto or the Home Assistant add-on.

## 2. Connect it to the heat pump
1. **Switch off the heat pump at the breaker** before opening it.
2. Find the **CN-CNT** socket on the heat pump's main PCB.
3. Plug the HeishaMon cable into CN-CNT.
4. Switch the heat pump back on. HeishaMon powers up from the heat pump and the LED lights up.

See the [connection details](README.md#connection-details) for the CN-CNT and CN-NMODE pin-outs.

### If you already have a Panasonic CZ-TAW1 WiFi module
- **Large board:** plug the CZ-TAW1 into HeishaMon's proxy port. Both then work at the same time, and HeishaMon can still send commands.
- **Small board, replacing the CZ-TAW1:** unplug the CZ-TAW1 from CN-CNT and plug HeishaMon in instead.
- **Small board, keeping the CZ-TAW1:** leave the CZ-TAW1 on **CN-CNT** and connect HeishaMon to the **CN-NMODE** socket instead. Then turn on *Listen only* in the HeishaMon settings. HeishaMon reads all data, but can't send commands or use the optional PCB emulation. Mind the CN-NMODE pin numbering: as printed on the PCB, the left pin is pin 4.

## 3. First-time setup
1. On your phone or laptop, connect to the open WiFi network **`HeishaMon-Setup`**.
2. Open **http://192.168.4.1**.
3. Enter:
   - your WiFi network name and password
   - your MQTT server address, and a username and password if the broker needs them
   - an update password (default: `heisha`)
4. Save. HeishaMon reboots and joins your network.

## 4. Check that it works
- Open **http://heishamon.local**, or use the IP address your router gave it. The values from the heat pump should show up within a minute.
- MQTT data is published under `panasonic_heat_pump/...`. [MQTT-Topics.md](MQTT-Topics.md) lists every topic.
- The raw data as JSON is at `http://heishamon.local/json`.

## 5. Next steps
- **Home Assistant, Domoticz, openHAB, Node-RED:** see the [Integrations](Integrations/) folder.
- **Firmware updates:** use the web page's *Firmware* menu (user `admin`, with your update password). Use the [model-type-small](binaries/model-type-small) binaries for the small board and [model-type-large](binaries/model-type-large) for the large board.
- **Extras:** DS18B20 temperature sensors (1-wire), S0 kWh meters and the built-in [rules engine](README.md#rules-functionality). On the large board also relays, OpenTherm and Ethernet.

## Troubleshooting
- **No values:** check the cable and the connector orientation. In *Listen only* mode, check that the CZ-TAW1 is connected and active, because HeishaMon only listens to that traffic.
- **WiFi lost or settings wrong:** hold the boot/flash button for more than 10 seconds to factory-reset. The `HeishaMon-Setup` network will appear again. See [Factory reset](README.md#factory-reset).
- **Heat pump more than about 5 m from where HeishaMon sits:** use an RS485 extension and power HeishaMon over USB. See [Long distance connection](README.md#long-distance-connection).
