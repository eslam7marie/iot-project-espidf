"""
Sprint 2: MQTT -> Postgres bridge.

What this script does, in plain terms:
1. Connects to the Mosquitto broker as a subscriber.
2. Subscribes to devices/+/telemetry (the '+' matches any device_id).
3. Every time a telemetry message arrives, parses the JSON and inserts a
   row into the `telemetry` table in Neon (Postgres).
4. Runs forever, like firmware's loop() -- you stop it with Ctrl+C.

No Flask, no REST API yet -- this script's only job is proving
"MQTT message in -> durable row in the database", in isolation.
"""

import json
import os
import sys
from datetime import datetime, timezone

import psycopg2
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

# ---------------------------------------------------------------------------
# 1. Load configuration from .env
# ---------------------------------------------------------------------------
# load_dotenv() reads the .env file in the current directory and copies its
# key=value pairs into the process's environment variables. os.getenv() then
# reads them back out. If .env is missing a key, os.getenv() returns None --
# we check for that below and fail loudly with a clear message, rather than
# limping along with a broken config.
load_dotenv()

DATABASE_URL = os.getenv("DATABASE_URL")
MQTT_BROKER_HOST = os.getenv("MQTT_BROKER_HOST")
MQTT_BROKER_PORT = int(os.getenv("MQTT_BROKER_PORT", "1883"))

if not DATABASE_URL:
    print("ERROR: DATABASE_URL is not set. Check your .env file.")
    sys.exit(1)
if not MQTT_BROKER_HOST:
    print("ERROR: MQTT_BROKER_HOST is not set. Check your .env file.")
    sys.exit(1)

TELEMETRY_TOPIC_FILTER = "devices/+/telemetry"

# ---------------------------------------------------------------------------
# 2. Open ONE database connection, reused for every insert
# ---------------------------------------------------------------------------
# psycopg2.connect() does the TCP handshake + TLS + auth with Neon, once, at
# startup. We keep this connection object alive for the whole life of the
# script and reuse it for every message that comes in -- see the "why" notes
# above this code block in the chat explanation.
print("Connecting to Postgres...")
db_conn = psycopg2.connect(DATABASE_URL)
db_conn.autocommit = True  # each INSERT commits immediately; fine for this simple script
print("Connected to Postgres.")


def insert_telemetry(device_id: str, temp_c: float, humidity: float, relay_state: str):
    """
    Inserts one telemetry row. Uses a parameterized query (the %s placeholders)
    so psycopg2 safely escapes the values -- never build SQL with f-strings
    or string concatenation using values that came from outside your program
    (like an MQTT payload).
    """
    recorded_at = datetime.now(timezone.utc).replace(tzinfo=None)

    with db_conn.cursor() as cur:
        cur.execute(
            """
            INSERT INTO telemetry (device_id, temp_c, humidity, relay_state, recorded_at)
            VALUES (%s, %s, %s, %s, %s)
            """,
            (device_id, temp_c, humidity, relay_state, recorded_at),
        )


# ---------------------------------------------------------------------------
# 3. MQTT callbacks
# ---------------------------------------------------------------------------
# These functions are never called directly by us -- we hand them to the
# paho-mqtt client below, and the library's internal network loop calls them
# automatically when the corresponding event happens (connect succeeding,
# a message arriving, etc). This is the "event handler" pattern mentioned
# in the chat explanation.

def on_connect(client, userdata, flags, rc):
    """Called once, when the client successfully (or unsuccessfully) connects."""
    if rc == 0:
        print(f"Connected to MQTT broker at {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}")
        # Subscribing here (not outside this function) guarantees we only
        # subscribe once we KNOW the connection succeeded.
        client.subscribe(TELEMETRY_TOPIC_FILTER, qos=1)
        print(f"Subscribed to: {TELEMETRY_TOPIC_FILTER}")
    else:
        print(f"Failed to connect, return code {rc}")


def on_message(client, userdata, msg):
    """
    Called every time a message arrives on a topic we're subscribed to.
    This is where the "one bad message shouldn't kill the script" logic
    lives -- notice the whole body is wrapped in a try/except.
    """
    try:
        payload_text = msg.payload.decode("utf-8")
        payload = json.loads(payload_text)

        # .get() returns None instead of raising an error if a key is
        # missing, which lets us check for missing fields deliberately
        # below instead of getting a confusing crash from payload["temp_c"].
        device_id = payload.get("device_id")
        temp_c = payload.get("temp_c")
        humidity = payload.get("humidity")
        relay_state = payload.get("relay", "off")

        if device_id is None or temp_c is None or humidity is None:
            print(f"Skipping malformed message (missing fields): {payload_text}")
            return

        insert_telemetry(device_id, float(temp_c), float(humidity), relay_state)
        print(f"Stored: device={device_id} temp={temp_c} humidity={humidity} relay={relay_state}")

    except json.JSONDecodeError:
        print(f"Skipping non-JSON payload on topic {msg.topic}: {msg.payload!r}")
    except psycopg2.Error as db_err:
        # Common cause: device_id isn't in the `devices` table yet (foreign
        # key violation). We log it and move on rather than crashing --
        # see the Sprint 2 design note about this in the chat.
        print(f"Database error while storing message: {db_err}")
    except Exception as e:
        # Catch-all so a truly unexpected error still doesn't kill the
        # whole subscriber -- we'd rather log and keep listening.
        print(f"Unexpected error handling message: {e}")


def on_disconnect(client, userdata, rc):
    print(f"Disconnected from MQTT broker (rc={rc}).")


# ---------------------------------------------------------------------------
# 4. Create the MQTT client, wire up the callbacks, connect, and run forever
# ---------------------------------------------------------------------------
client = mqtt.Client(client_id="sprint2-mqtt-to-db")
client.on_connect = on_connect
client.on_message = on_message
client.on_disconnect = on_disconnect

client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, keepalive=30)

print("Starting MQTT network loop. Press Ctrl+C to stop.")
try:
    # loop_forever() blocks here, handling all network I/O and calling your
    # callbacks as events happen. This is the "wait for messages" loop
    # mentioned in the chat explanation -- you don't write it, the library
    # provides it.
    client.loop_forever()
except KeyboardInterrupt:
    print("\nStopping (Ctrl+C received)...")
finally:
    client.disconnect()
    db_conn.close()
    print("Cleaned up MQTT and DB connections. Bye.")
