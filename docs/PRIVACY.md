# Privacy and biometric data

Face recognition is biometric processing. Biometric data is personal data that cannot be rotated
after a leak, is uniquely identifying across systems, and is regulated specifically in most
jurisdictions (GDPR Art. 9 in the EU, plus national carve-outs for employee monitoring, and state-level
biometric laws such as BIPA in Illinois). This document states what the software actually stores and
what a deployment is responsible for. It is not legal advice.

## 1. What the system collects and keeps

| Store | Contents | Sensitivity |
|-------|----------|-------------|
| `persons` | Name, code, `picture_path`, access level, created_at | Directly identifying. |
| `reference_faces` | 512 × float32 embedding per enrolled face | **Biometric template.** From the template an image cannot be reconstructed, but the template is a permanent identifier for that face across any system using a compatible model. |
| `unknown_faces`, `raw_detections` | Embedding + track id + camera uid + timestamp + crop path for every accepted detection, including people who were never enrolled | Biometric data of non-consenting passers-by; grows continuously. |
| `crops/` on disk | JPEG crops of faces | Raw biometric source images — the most sensitive artifact in the system. |
| `attendance_logs`, `work_sessions`, `breaks`, `tardiness_log`, `location_log` | When and where each identified person was present | A behavioural record about identifiable people (working time, movement between zones). |
| `cameras` | Host, stream URI, ONVIF xaddrs, username, **plaintext password** | Infrastructure secrets. |

There is **no** audio capture, no plate recognition, and no cross-deployment data exchange.

## 2. What this repository does *not* contain

Verified by `scripts/check_repo_hygiene.py` in CI, not just asserted here:

- no images of real people (`*.jpg`/`*.jpeg`/`*.png` are ignored, and the only PNGs in the repo are
  generated diagrams under `docs/images/`);
- no SQLite database, no attendance records, no embeddings;
- no model weights, no calibration data derived from real footage (`int8_calibration.txt` holds
  TensorRT layer calibration hashes only);
- no camera credentials, hostnames or LAN addresses in source; `config.json` and `license.json` are
  ignored, only the `*.example.json` templates are tracked.

## 3. Design choices that limit harm

- **Review queue instead of auto-attribution.** A face below `recognition.faiss_threshold` is never
  recorded as a person. It goes to `unknown_faces` for a human to enrol, merge or discard, so a
  false match cannot silently become an attendance record or an access decision.
- **Quality gates before processing.** Blur/side-profile/far-distance gates drop unusable crops,
  which reduces both error rates and the number of stored images of people who were just walking by.
- **Credential redaction.** `/api/cameras` and the FFI camera reads return `hasPassword`, never the
  password.
- **Path-confined file serving.** `/api/files/{path}` is canonicalised against the crops directory
  and rejects traversal, so the API cannot be used to exfiltrate arbitrary files.
- **Data-local deployment.** Everything runs on the customer's LAN against SQLite; no telemetry, no
  cloud calls, no third-party endpoints in the code.

## 4. What a real deployment must add

These are obligations of the operator, listed here so nobody has to discover them later:

1. **Lawful basis and notice.** Signage at every entrance, a written policy, and a documented basis
   (consent, or employment/health-and-safety necessity with a balancing test). Employee monitoring
   additionally requires works council / consultation processes in many jurisdictions.
2. **Retention limits.** Cap `raw_detections`, `unknown_faces`, `location_log` and `crops/`. The
   provided script shows the shape of such a job:
   ```bash
   ./scripts/purge_data.sh attendance.db crops --keep-days 7 --dry-run
   ```
3. **Access control on the API.** Add tokens and TLS before anything leaves a machine room
   ([SECURITY.md](../SECURITY.md)); today anyone on the LAN can read the entire attendance history.
4. **Encryption at rest, or a secret store**, for `attendance.db` (it contains templates and camera
   passwords in plaintext).
5. **Deletion path — and its current hole.** `DELETE /api/persons/{id}` removes the person, their
   reference embeddings, their `raw_detections`, `attendance_logs` and `person_schedules` rows
   (`FaceDatabase::deletePerson`), and `face_db_rebuild_index()` updates the live FAISS index.
   It does **not** delete `work_sessions`, `breaks`, `tardiness_log` or `location_log`, and it does not
   touch crop files on disk. That is enough to be a real problem for an erasure request, so until the
   delete path covers those tables, honour erasure by deleting the person *and* running
   [`scripts/purge_data.sh`](../scripts/purge_data.sh) with `--person-id`, or by removing the database
   row set manually. Listed in [SECURITY_AUDIT.md](SECURITY_AUDIT.md#5-data-lifecycle-findings).
6. **A register of processing activities** and a DPIA: large-scale, systematic biometric processing of
   employees is a mandatory-DPIA category under GDPR Art. 35.
7. **Accuracy measurement on the actual population.** Recognition error rates differ across
   demographic groups and are driven by pose/illumination in the installed camera position. Before
   going live, measure FAR/FRR on the site's own enrolled cohort and set
   `recognition.faiss_threshold` from that measurement rather than from a paper default.
8. **Human review for consequential decisions.** Attendance disputes, pay and access denial should
   be resolved by a person looking at `GET /api/attendance/timeline` and the stored crop — the API
   exposes exactly what the model decided *and* why, deliberately.

## 5. Scope I will not extend

I built this for presence/attendance and access control in a managed facility, where the people
being recognised are informed and have an alternative (a badge). I am not going to grow it into
surveillance of uninvolved third parties, tracking of people across public space, or emotion/attribute
inference, and I would not accept a pull request whose purpose is one of those.

## 6. Reference points

- GDPR Art. 9 (special categories), Recital 51 (biometric data), Art. 35 (DPIA).
- EU AI Act: remote biometric identification systems are a high-risk category with documentation,
  accuracy and human-oversight duties — relevant for anyone reusing this code in a product.
- Illinois BIPA and similar state laws: consent + a published retention schedule, private right of action.
- NIST FRVT/FRTE reports for the accuracy landscape of face-recognition algorithms generally.
