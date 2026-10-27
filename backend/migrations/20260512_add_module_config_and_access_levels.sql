-- Migration: add access_level to persons, required_access_level to zones, and camera_module_config table
BEGIN TRANSACTION;

-- Add access_level to persons if not exists
PRAGMA foreign_keys=off;

CREATE TABLE IF NOT EXISTS _persons_new (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    code TEXT UNIQUE,
    created_at INTEGER,
    is_known INTEGER DEFAULT 1,
    picture_path TEXT,
    access_level INTEGER NOT NULL DEFAULT 2
);

INSERT INTO _persons_new(id, name, code, created_at, is_known, picture_path)
SELECT id, name, code, created_at, is_known, picture_path FROM persons;

DROP TABLE IF EXISTS persons;
ALTER TABLE _persons_new RENAME TO persons;

-- Add required_access_level to zones
CREATE TABLE IF NOT EXISTS _zones_new (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT,
    created_at INTEGER,
    required_access_level INTEGER DEFAULT 2
);

INSERT INTO _zones_new(id, name, description, created_at)
SELECT id, name, description, created_at FROM zones;

DROP TABLE IF EXISTS zones;
ALTER TABLE _zones_new RENAME TO zones;

-- Create camera_module_config table if not exists
CREATE TABLE IF NOT EXISTS camera_module_config (
    camera_uid TEXT NOT NULL,
    module_name TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    config_json TEXT NOT NULL DEFAULT '{}',
    PRIMARY KEY (camera_uid, module_name),
    FOREIGN KEY (camera_uid) REFERENCES cameras(camera_uid)
);

PRAGMA foreign_keys=on;
COMMIT;
