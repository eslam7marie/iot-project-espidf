-- IoT platform schema (Postgres / Neon)
-- Safe to re-run: every statement is idempotent.

CREATE TABLE IF NOT EXISTS devices (
    id            SERIAL PRIMARY KEY,
    device_id     VARCHAR(64) UNIQUE NOT NULL,
    display_name  VARCHAR(128) NOT NULL,
    status        VARCHAR(16) NOT NULL DEFAULT 'offline',
    last_seen_at  TIMESTAMP
);

-- temp_c / humidity are named for the eventual DHT22, not the current
-- potentiometer stand-in — the firmware maps the pot's raw ADC reading onto
-- these same two fields, so nothing downstream needs to change when the
-- real sensor is wired in later.
CREATE TABLE IF NOT EXISTS telemetry (
    id           BIGSERIAL PRIMARY KEY,
    device_id    VARCHAR(64) NOT NULL REFERENCES devices(device_id),
    temp_c       REAL NOT NULL,
    humidity     REAL NOT NULL,
    relay_state  VARCHAR(8) NOT NULL DEFAULT 'off',
    recorded_at  TIMESTAMP NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_telemetry_device_time
    ON telemetry (device_id, recorded_at DESC);

CREATE TABLE IF NOT EXISTS commands (
    id         BIGSERIAL PRIMARY KEY,
    device_id  VARCHAR(64) NOT NULL REFERENCES devices(device_id),
    command    VARCHAR(32) NOT NULL,
    value      VARCHAR(32) NOT NULL,
    issued_at  TIMESTAMP NOT NULL DEFAULT now()
);

-- Seed the one device this project starts with.
INSERT INTO devices (device_id, display_name)
VALUES ('esp32-livingroom-01', 'ESP32-S 38pin (bench)')
ON CONFLICT (device_id) DO UPDATE SET display_name = EXCLUDED.display_name;
