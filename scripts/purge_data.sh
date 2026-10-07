#!/usr/bin/env bash
# Retention / erasure helper for the SQLite database and the crops directory.
#
# The backend has no retention policy of its own (by design: what to keep is a
# deployment decision), so this script exists to make the safe thing the easy thing.
#
#   ./scripts/purge_data.sh <db> <crops-dir> --keep-days N [--person-id ID] [--dry-run] [--yes]
#
# Examples:
#   # preview what 30-day retention would remove
#   ./scripts/purge_data.sh attendance.db crops --keep-days 30 --dry-run
#
#   # erase one person completely (the API's DELETE /api/persons/{id} leaves
#   # work_sessions/breaks/tardiness_log/location_log behind - see docs/PRIVACY.md)
#   ./scripts/purge_data.sh attendance.db crops --person-id 42 --yes
#
# It takes a timestamped backup copy first, prints a before/after count for every table,
# and VACUUMs at the end. Requires the sqlite3 CLI.
set -euo pipefail

die() { echo "error: $*" >&2; exit 1; }

if [[ $# -lt 3 ]]; then
  sed -n '3,20p' "$0"
  exit 2
fi

DB="$1"; CROPS="$2"; shift 2
KEEP_DAYS=""
PERSON_ID=""
DRY_RUN=0
ASSUME_YES=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --keep-days)  KEEP_DAYS="${2:?--keep-days needs a number}"; shift 2 ;;
    --person-id)  PERSON_ID="${2:?--person-id needs a person id}"; shift 2 ;;
    --dry-run)    DRY_RUN=1; shift ;;
    --yes|-y)     ASSUME_YES=1; shift ;;
    *)            die "unknown argument: $1" ;;
  esac
done

command -v sqlite3 >/dev/null 2>&1 || die "sqlite3 CLI not found (apt install sqlite3)"
[[ -f "$DB" ]] || die "database not found: $DB"
if [[ -z "$KEEP_DAYS" && -z "$PERSON_ID" ]]; then
  die "nothing to do: pass --keep-days N and/or --person-id ID"
fi
if [[ -n "$KEEP_DAYS" && ! "$KEEP_DAYS" =~ ^[0-9]+$ ]]; then
  die "--keep-days must be a non-negative integer"
fi

TABLES=(raw_detections unknown_faces location_log attendance_logs work_sessions breaks tardiness_log)

count_all() {
  local total=0 row table
  for table in "${TABLES[@]}"; do
    row=$(sqlite3 "$DB" "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='$table';")
    [[ "$row" == "0" ]] && continue
    n=$(sqlite3 "$DB" "SELECT count(*) FROM $table;")
    printf '  %-16s %s\n' "$table" "$n"
    total=$((total + n))
  done
  printf '  %-16s %s\n' "TOTAL" "$total"
}

echo "== database: $DB"
echo "-- rows before"
count_all

SQL=""
if [[ -n "$KEEP_DAYS" ]]; then
  cutoff=$(date -u -d "-${KEEP_DAYS} days" +%s 2>/dev/null || echo "")
  [[ -n "$cutoff" ]] || die "could not compute the cutoff date (needs GNU date)"
  echo "-- cutoff: $(date -u -d "@$cutoff" -Is) (keeping ${KEEP_DAYS} days)"
  SQL+="DELETE FROM raw_detections  WHERE timestamp < $cutoff;
DELETE FROM unknown_faces    WHERE timestamp < $cutoff;
DELETE FROM location_log     WHERE timestamp < $cutoff;
DELETE FROM attendance_logs  WHERE timestamp < $cutoff;
DELETE FROM breaks           WHERE break_start IS NOT NULL AND break_start < $cutoff;
DELETE FROM work_sessions    WHERE date < date('$cutoff','unixepoch');
DELETE FROM tardiness_log    WHERE date < date('$cutoff','unixepoch');
-- orphaned crops: any image file referenced by a row we just removed
"
fi

if [[ -n "$PERSON_ID" ]]; then
  [[ "$PERSON_ID" =~ ^[0-9]+$ ]] || die "--person-id must be an integer"
  echo "-- erasing person id $PERSON_ID"
  # unknown_faces has to go first: it is matched through raw_detections, and those
  # rows are deleted below, so a later statement would find an empty subquery.
  SQL+="DELETE FROM unknown_faces    WHERE track_id IN (SELECT track_id FROM raw_detections WHERE person_id = $PERSON_ID);
DELETE FROM reference_faces  WHERE person_id = $PERSON_ID;
DELETE FROM raw_detections   WHERE person_id = $PERSON_ID;
DELETE FROM attendance_logs  WHERE person_id = $PERSON_ID;
DELETE FROM person_schedules WHERE person_id = $PERSON_ID;
DELETE FROM work_sessions    WHERE person_id = $PERSON_ID;
DELETE FROM breaks           WHERE person_id = $PERSON_ID;
DELETE FROM tardiness_log    WHERE person_id = $PERSON_ID;
DELETE FROM location_log     WHERE person_id = $PERSON_ID;
DELETE FROM persons          WHERE id = $PERSON_ID;
"
fi

if [[ $DRY_RUN -eq 1 ]]; then
  echo "-- dry run: no changes written. Statements that would run:"
  printf '%s\n' "$SQL" | sed '/^--/d;/^$/d' | sed 's/^/    /'
  exit 0
fi

if [[ $ASSUME_YES -ne 1 ]]; then
  read -r -p "This deletes rows from $DB and prunes crops. Continue? [y/N] " reply
  [[ "$reply" == "y" || "$reply" == "Y" ]] || { echo "aborted"; exit 1; }
fi

backup="${DB}.$(date -u +%Y%m%dT%H%M%SZ).bak"
cp -a "$DB" "$backup"
echo "-- backup: $backup"

sqlite3 "$DB" "PRAGMA foreign_keys=off; BEGIN; ${SQL} COMMIT; VACUUM;"

echo "-- rows after"
count_all

# Crop files whose row no longer exists. Only files directly under $CROPS are considered,
# and the delete is limited to image extensions, so this cannot wander the filesystem.
if [[ -d "$CROPS" ]]; then
  removed=0
  while IFS= read -r -d '' file; do
    name=$(basename "$file")
    still=$(sqlite3 "$DB" "SELECT count(*) FROM raw_detections WHERE image_path LIKE '%${name}';")
    unknown=$(sqlite3 "$DB" "SELECT count(*) FROM unknown_faces WHERE image_path LIKE '%${name}';")
    persons=$(sqlite3 "$DB" "SELECT count(*) FROM persons WHERE picture_path LIKE '%${name}';")
    if [[ "$still" == "0" && "$unknown" == "0" && "$persons" == "0" ]]; then
      rm -f -- "$file"
      removed=$((removed + 1))
    fi
  done < <(find "$CROPS" -maxdepth 1 -type f \( -name '*.jpg' -o -name '*.jpeg' -o -name '*.png' \) -print0)
  echo "-- orphaned crop files removed: $removed"
fi

echo "-- done."
echo "   If the server is running, restart it (or use the API's delete/enrol endpoints, which"
echo "   call face_db_rebuild_index) so the in-memory FAISS index drops the removed vectors."
