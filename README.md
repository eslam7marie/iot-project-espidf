# ESP32 IoT platform — ESP-IDF learning project

Rebuild the existing Arduino telemetry/control platform with native ESP-IDF in
VS Code, using the same classic ESP32-S 38-pin board.

**Current state: milestone 3, local MQTT implemented; board/broker checks pending.**
The ESP32 publishes potentiometer readings and receives relay commands through
Mosquitto. With MQTT enabled, the LED holds the commanded state instead of
blinking automatically. See [the milestone 3 guide](docs/MILESTONE_3.md) for
configuration and CMD pub/sub tests. HTTP is the next milestone.

Target flow:

```text
ESP32 (potentiometer + relay/LED) <-> Mosquitto <-> Python subscriber -> PostgreSQL
             |                                   |
       local HTTP                            future Flask API
                                                  |
                                            future dashboard
```

## Start in VS Code

1. Open this repository root (the folder containing CMakeLists.txt).
2. Use the Espressif ESP-IDF extension and select the installed ESP-IDF v6.1 environment.
3. Open an ESP-IDF terminal and run:

```powershell
idf.py set-target esp32
idf.py build
```

To flash when ready, select the board's current serial port in the extension,
then use Flash and Monitor. In an ESP-IDF terminal, replace PORT with that port:

```powershell
idf.py -p PORT flash monitor
```

The new program replaces the Arduino firmware on the board. Expected
output: `Milestone 3: local MQTT telemetry and relay commands`, ADC readings and
relay state every second by default.
Exit the monitor with Ctrl+]. Wi-Fi credentials and the PC broker URI are needed for MQTT. Local sensor
readings continue while disconnected. The relay starts OFF and waits for commands.

## Where to work

- firmware/main/app_main.c: native ESP-IDF entry point.
- backend/: preserved MQTT-to-PostgreSQL bridge and dependency versions.
- db/schema.sql: preserved devices, telemetry and commands schema.
- docs/PROJECT_STUDY.md: evidence, contracts and differences found during review.
- docs/MIGRATION_PLAN.md: incremental learning milestones and acceptance checks.
- docs/WIRING.md: existing pins and simulated sensor mapping.
- docs/reference/: supplied handbook and sanitized Arduino reference.
- docs/VALIDATION.md: checks performed for this baseline.

## Backend baseline

The bridge is retained to verify firmware compatibility during migration. In a
PowerShell terminal from this repository root:

```powershell
python -m venv backend/.venv
backend/.venv/Scripts/python.exe -m pip install -r backend/requirements.txt
Copy-Item backend/.env.example backend/.env
```

Edit backend/.env with your existing broker and database configuration. Run the
schema only against the intended database if it is not already provisioned,
then start the bridge with `backend/.venv/Scripts/python.exe backend/mqtt_to_db.py`.
The copied bridge retains its existing reconnect and validation limitations;
see the study. It will receive data only once a publisher is running.

Generated sdkconfig, local secrets, certificates, virtual environments and build
outputs are ignored by Git. Do not put credentials in sdkconfig.defaults.
This is a new local repository; remote hosting can be configured separately.
