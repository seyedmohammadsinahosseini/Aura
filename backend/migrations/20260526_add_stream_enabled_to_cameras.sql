-- Migration: separate camera stream availability from AI processing state

ALTER TABLE cameras ADD COLUMN stream_enabled INTEGER NOT NULL DEFAULT 1;
