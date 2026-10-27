#include "services/storage/FaceDatabase.h"

#include <sqlite3.h>
#include <opencv2/opencv.hpp>
#include "modules/recognition/Utils.h"

#include <ctime>
#include <stdexcept>

namespace {

constexpr size_t kEmbeddingDimension = 512;

class Statement final {
public:
    Statement(sqlite3* db, const std::string& sql) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    }

    ~Statement() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }

    sqlite3_stmt* get() const { return stmt_; }

private:
    sqlite3_stmt* stmt_{nullptr};
};

int64_t currentUnixTimestamp() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string columnText(sqlite3_stmt* stmt, const int index) {
    const auto* value = sqlite3_column_text(stmt, index);
    return value == nullptr ? "" : reinterpret_cast<const char*>(value);
}

bool columnBool(sqlite3_stmt* stmt, const int index) {
    return sqlite3_column_int(stmt, index) != 0;
}

face::CameraType cameraTypeFromText(const std::string& value) {
    if (value == "ENTRY") {
        return face::CameraType::ENTRY;
    }
    if (value == "EXIT") {
        return face::CameraType::EXIT;
    }
    return face::CameraType::BOTH;
}

std::string cameraTypeToText(const face::CameraType type) {
    switch (type) {
        case face::CameraType::ENTRY:
            return "ENTRY";
        case face::CameraType::EXIT:
            return "EXIT";
        case face::CameraType::BOTH:
        default:
            return "BOTH";
    }
}

face::WorkSession readWorkSession(sqlite3_stmt* stmt) {
    face::WorkSession session;
    session.id = sqlite3_column_int64(stmt, 0);
    session.personId = sqlite3_column_int64(stmt, 1);
    session.date = columnText(stmt, 2);
    session.arrivalTime = sqlite3_column_int64(stmt, 3);
    session.departureTime = sqlite3_column_int64(stmt, 4);
    session.totalWorkSeconds = sqlite3_column_int64(stmt, 5);
    session.totalBreakSeconds = sqlite3_column_int64(stmt, 6);
    session.isComplete = columnBool(stmt, 7);
    return session;
}

face::BreakRecord readBreakRecord(sqlite3_stmt* stmt) {
    face::BreakRecord record;
    record.id = sqlite3_column_int64(stmt, 0);
    record.sessionId = sqlite3_column_int64(stmt, 1);
    record.personId = sqlite3_column_int64(stmt, 2);
    record.breakStart = sqlite3_column_int64(stmt, 3);
    record.breakEnd = sqlite3_column_int64(stmt, 4);
    record.breakSeconds = sqlite3_column_int64(stmt, 5);
    return record;
}

}  // namespace

namespace face {

FaceDatabase::FaceDatabase(const std::string& dbPath) {
    open(dbPath);
    createTables();
}

FaceDatabase::~FaceDatabase() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

void FaceDatabase::open(const std::string& dbPath) {
    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        const std::string message = db_ != nullptr ? sqlite3_errmsg(db_) : "failed to open sqlite database";
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error(message);
    }
}

void FaceDatabase::createTables() {
    execute(
        "CREATE TABLE IF NOT EXISTS persons ("
        "id INTEGER PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "code TEXT UNIQUE,"
        "created_at INTEGER,"
        "is_known INTEGER DEFAULT 1,"
        "picture_path TEXT,"
        "access_level INTEGER NOT NULL DEFAULT 2"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS reference_faces ("
        "id INTEGER PRIMARY KEY,"
        "person_id INTEGER,"
        "embedding BLOB,"
        "created_at INTEGER,"
        "FOREIGN KEY(person_id) REFERENCES persons(id)"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS unknown_faces ("
        "id INTEGER PRIMARY KEY,"
        "embedding BLOB NOT NULL,"
        "track_id TEXT NOT NULL,"
        "camera_uid TEXT NOT NULL,"
        "timestamp INTEGER,"
        "image_path TEXT"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS attendance_logs ("
        "id INTEGER PRIMARY KEY,"
        "person_id INTEGER,"
        "track_id TEXT,"
        "event_type TEXT,"
        "timestamp INTEGER,"
        "FOREIGN KEY(person_id) REFERENCES persons(id)"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS raw_detections ("
        "id INTEGER PRIMARY KEY,"
        "person_id INTEGER,"
        "track_id TEXT NOT NULL,"
        "embedding BLOB NOT NULL,"
        "image_path TEXT,"
        "timestamp INTEGER,"
        "status TEXT,"
        "FOREIGN KEY(person_id) REFERENCES persons(id)"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS system_settings ("
        "key TEXT PRIMARY KEY,"
        "value TEXT,"
        "description TEXT,"
        "updated_at INTEGER"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS person_schedules ("
        "id INTEGER PRIMARY KEY,"
        "person_id INTEGER NOT NULL UNIQUE,"
        "expected_arrival TEXT NOT NULL DEFAULT '08:00',"
        "expected_departure TEXT NOT NULL DEFAULT '17:00',"
        "grace_minutes INTEGER NOT NULL DEFAULT 15,"
        "work_days TEXT NOT NULL DEFAULT '1,2,3,4,5',"
        "updated_at INTEGER,"
        "FOREIGN KEY(person_id) REFERENCES persons(id)"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS zones ("
        "id INTEGER PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "description TEXT,"
        "created_at INTEGER,"
        "required_access_level INTEGER DEFAULT 2"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS cameras ("
        "id INTEGER PRIMARY KEY,"
        "camera_uid TEXT UNIQUE NOT NULL,"
        "name TEXT,"
        "zone_id INTEGER,"
        "type TEXT NOT NULL,"
        "stream_enabled INTEGER NOT NULL DEFAULT 1,"
        "ai_enabled INTEGER NOT NULL DEFAULT 1,"
        "connection_type TEXT NOT NULL DEFAULT 'RTSP',"
        "username TEXT DEFAULT '',"
        "password TEXT DEFAULT '',"
        "stream_uri TEXT DEFAULT '',"
        "encoding TEXT DEFAULT '',"
        "onvif_xaddrs TEXT DEFAULT '',"
        "transport_protocol TEXT DEFAULT 'tcp',"
        "created_at INTEGER,"
        "FOREIGN KEY(zone_id) REFERENCES zones(id)"
        ");");

    if (!hasColumn("cameras", "stream_enabled")) {
        execute("ALTER TABLE cameras ADD COLUMN stream_enabled INTEGER NOT NULL DEFAULT 1;");
    }
    if (!hasColumn("cameras", "connection_type")) {
        execute("ALTER TABLE cameras ADD COLUMN connection_type TEXT NOT NULL DEFAULT 'RTSP';");
    }
    if (!hasColumn("cameras", "username")) {
        execute("ALTER TABLE cameras ADD COLUMN username TEXT DEFAULT '';");
    }
    if (!hasColumn("cameras", "password")) {
        execute("ALTER TABLE cameras ADD COLUMN password TEXT DEFAULT '';");
    }
    if (!hasColumn("cameras", "stream_uri")) {
        execute("ALTER TABLE cameras ADD COLUMN stream_uri TEXT DEFAULT '';");
    }
    if (!hasColumn("cameras", "encoding")) {
        execute("ALTER TABLE cameras ADD COLUMN encoding TEXT DEFAULT '';");
    }
    if (!hasColumn("cameras", "onvif_xaddrs")) {
        execute("ALTER TABLE cameras ADD COLUMN onvif_xaddrs TEXT DEFAULT '';");
    }
    if (!hasColumn("cameras", "transport_protocol")) {
        execute("ALTER TABLE cameras ADD COLUMN transport_protocol TEXT DEFAULT 'tcp';");
    }

    execute(
        "CREATE TABLE IF NOT EXISTS camera_module_config ("
        "camera_uid TEXT NOT NULL,"
        "module_name TEXT NOT NULL,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "config_json TEXT NOT NULL DEFAULT '{}',"
        "PRIMARY KEY (camera_uid, module_name),"
        "FOREIGN KEY(camera_uid) REFERENCES cameras(camera_uid)"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS work_sessions ("
        "id INTEGER PRIMARY KEY,"
        "person_id INTEGER NOT NULL,"
        "date TEXT NOT NULL,"
        "arrival_time INTEGER,"
        "departure_time INTEGER,"
        "total_work_seconds INTEGER DEFAULT 0,"
        "total_break_seconds INTEGER DEFAULT 0,"
        "is_complete INTEGER DEFAULT 0,"
        "created_at INTEGER,"
        "UNIQUE(person_id, date),"
        "FOREIGN KEY(person_id) REFERENCES persons(id)"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS breaks ("
        "id INTEGER PRIMARY KEY,"
        "session_id INTEGER NOT NULL,"
        "person_id INTEGER NOT NULL,"
        "break_start INTEGER,"
        "break_end INTEGER,"
        "break_seconds INTEGER DEFAULT 0,"
        "FOREIGN KEY(session_id) REFERENCES work_sessions(id),"
        "FOREIGN KEY(person_id) REFERENCES persons(id)"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS location_log ("
        "id INTEGER PRIMARY KEY,"
        "person_id INTEGER NOT NULL,"
        "zone_id INTEGER NOT NULL,"
        "camera_id INTEGER NOT NULL,"
        "timestamp INTEGER NOT NULL,"
        "FOREIGN KEY(person_id) REFERENCES persons(id),"
        "FOREIGN KEY(zone_id) REFERENCES zones(id),"
        "FOREIGN KEY(camera_id) REFERENCES cameras(id)"
        ");");

    execute(
        "CREATE TABLE IF NOT EXISTS tardiness_log ("
        "id INTEGER PRIMARY KEY,"
        "person_id INTEGER NOT NULL,"
        "date TEXT NOT NULL,"
        "expected_arrival INTEGER,"
        "actual_arrival INTEGER,"
        "tardiness_seconds INTEGER DEFAULT 0,"
        "is_absent INTEGER DEFAULT 0,"
        "FOREIGN KEY(person_id) REFERENCES persons(id)"
        ");");

}

void FaceDatabase::execute(const std::string& sql) {
    char* errorMessage = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        const std::string message = errorMessage != nullptr ? errorMessage : "sqlite execution failed";
        sqlite3_free(errorMessage);
        throw std::runtime_error(message);
    }
}

bool FaceDatabase::hasColumn(const std::string& tableName, const std::string& columnName) {
    Statement stmt(db_, "PRAGMA table_info(" + tableName + ");");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        if (columnText(stmt.get(), 1) == columnName) {
            return true;
        }
    }
    return false;
}

void FaceDatabase::validateEmbeddingSize(const std::vector<float>& emb) {
    if (emb.size() != kEmbeddingDimension) {
        throw std::runtime_error("face embedding must contain exactly 512 values");
    }
}

int64_t FaceDatabase::addPerson(const std::string& name, const std::string& code, const bool isKnown, const std::string& picturePath) {
    Statement stmt(db_, "INSERT INTO persons(name, code, created_at, is_known, picture_path) VALUES(?, ?, ?, ?, ?);");
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (code.empty()) {
        sqlite3_bind_null(stmt.get(), 2);
    } else {
        sqlite3_bind_text(stmt.get(), 2, code.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int64(stmt.get(), 3, currentUnixTimestamp());
    sqlite3_bind_int(stmt.get(), 4, isKnown ? 1 : 0);
    sqlite3_bind_text(stmt.get(), 5, picturePath.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return sqlite3_last_insert_rowid(db_);
}

int64_t FaceDatabase::addPerson(const std::string& name, const std::string& code, bool isKnown, const cv::Mat& image, const std::string& cropsDir) {
    const std::string picturePath = buildCropPath(cropsDir, currentUnixTimestamp(), -1, "manual");
    ensureDirectoryExists(cropsDir);
    cv::imwrite(picturePath, image);
    return addPerson(name, code, isKnown, picturePath);
}

bool FaceDatabase::getPerson(int64_t id, Person& out) {
    Statement stmt(db_, "SELECT id, name, code, created_at, picture_path, access_level FROM persons WHERE id = ?;");
    sqlite3_bind_int64(stmt.get(), 1, id);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return false;
    }

    out.id = sqlite3_column_int64(stmt.get(), 0);
    out.name = columnText(stmt.get(), 1);
    out.code = columnText(stmt.get(), 2);
    out.createdAt = sqlite3_column_int64(stmt.get(), 3);
    out.picturePath = columnText(stmt.get(), 4);
    out.access_level = sqlite3_column_int(stmt.get(), 5);
    return true;
}

void FaceDatabase::addReferenceEmbedding(int64_t personId, const std::vector<float>& emb) {
    validateEmbeddingSize(emb);

    Statement stmt(db_, "INSERT INTO reference_faces(person_id, embedding, created_at) VALUES(?, ?, ?);");
    sqlite3_bind_int64(stmt.get(), 1, personId);
    sqlite3_bind_blob(stmt.get(), 2, emb.data(), static_cast<int>(emb.size() * sizeof(float)), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 3, currentUnixTimestamp());

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

void FaceDatabase::addUnknownFace(const std::vector<float>& embedding, const std::string& trackId, const std::string& cameraUid, int64_t timestamp, const std::string& imagePath) {
    validateEmbeddingSize(embedding);
    Statement stmt(db_, "INSERT INTO unknown_faces(embedding, track_id, camera_uid, timestamp, image_path) VALUES(?, ?, ?, ?, ?);");
    sqlite3_bind_blob(stmt.get(), 1, embedding.data(), static_cast<int>(embedding.size() * sizeof(float)), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, trackId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 4, timestamp);
    sqlite3_bind_text(stmt.get(), 5, imagePath.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

bool FaceDatabase::unknownFaceExists(const std::string& trackId, const std::string& cameraUid) {
    Statement stmt(db_, "SELECT COUNT(*) FROM unknown_faces WHERE track_id = ? AND camera_uid = ?;");
    sqlite3_bind_text(stmt.get(), 1, trackId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        return sqlite3_column_int(stmt.get(), 0) > 0;
    }
    return false;
}

std::vector<std::pair<int64_t, std::vector<float>>> FaceDatabase::getAllReferenceEmbeddings() {
    Statement stmt(db_, "SELECT rf.person_id, rf.embedding FROM reference_faces rf JOIN persons p ON rf.person_id = p.id WHERE p.is_known = 1;");
    std::vector<std::pair<int64_t, std::vector<float>>> all;

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const auto personId = sqlite3_column_int64(stmt.get(), 0);
        const auto* blob = sqlite3_column_blob(stmt.get(), 1);
        const auto blobBytes = sqlite3_column_bytes(stmt.get(), 1);
        if (blob == nullptr || blobBytes != static_cast<int>(kEmbeddingDimension * sizeof(float))) {
            continue;
        }
        const auto* data = static_cast<const float*>(blob);
        all.emplace_back(personId, std::vector<float>(data, data + kEmbeddingDimension));
    }

    return all;
}

int64_t FaceDatabase::addRawDetection(const RawDetection& detection) {
    validateEmbeddingSize(detection.embedding);

    Statement stmt(
        db_,
        "INSERT INTO raw_detections(person_id, track_id, embedding, image_path, timestamp, status) "
        "VALUES(?, ?, ?, ?, ?, ?);");
    if (detection.personId < 0) {
        sqlite3_bind_null(stmt.get(), 1);
    } else {
        sqlite3_bind_int64(stmt.get(), 1, detection.personId);
    }
    sqlite3_bind_text(stmt.get(), 2, detection.trackId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(
        stmt.get(),
        3,
        detection.embedding.data(),
        static_cast<int>(detection.embedding.size() * sizeof(float)),
        SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, detection.imagePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 5, detection.timestamp);
    sqlite3_bind_text(stmt.get(), 6, detection.status.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }

    return sqlite3_last_insert_rowid(db_);
}

void FaceDatabase::addAttendanceLog(int64_t personId,
                                    const std::string& trackId,
                                    const std::string& eventType,
                                    int64_t timestamp) {
    Statement stmt(
        db_,
        "INSERT INTO attendance_logs(person_id, track_id, event_type, timestamp) VALUES(?, ?, ?, ?);");
    sqlite3_bind_int64(stmt.get(), 1, personId);
    sqlite3_bind_text(stmt.get(), 2, trackId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, eventType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 4, timestamp);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

std::vector<RawDetection> FaceDatabase::getUnknownDetectionsNotProcessed() {
    // TODO: implement retrieval and filtering once the offline clustering flow is defined.
    return {};
}

void FaceDatabase::updateRawDetectionPersonId(int64_t detectionId, int64_t personId) {
    Statement stmt(db_, "UPDATE raw_detections SET person_id = ? WHERE id = ?;");
    sqlite3_bind_int64(stmt.get(), 1, personId);
    sqlite3_bind_int64(stmt.get(), 2, detectionId);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

std::vector<UnknownFace> FaceDatabase::getUnknownFaces() const {
    Statement stmt(db_, "SELECT id, embedding, track_id, camera_uid, timestamp, image_path FROM unknown_faces ORDER BY timestamp DESC;");
    std::vector<UnknownFace> faces;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        UnknownFace face;
        face.id = sqlite3_column_int64(stmt.get(), 0);
        const auto* blob = sqlite3_column_blob(stmt.get(), 1);
        const auto blobBytes = sqlite3_column_bytes(stmt.get(), 1);
        if (blob != nullptr && blobBytes == static_cast<int>(kEmbeddingDimension * sizeof(float))) {
            const auto* data = static_cast<const float*>(blob);
            face.embedding.assign(data, data + kEmbeddingDimension);
        }
        face.trackId = columnText(stmt.get(), 2);
        face.cameraUid = columnText(stmt.get(), 3);
        face.timestamp = sqlite3_column_int64(stmt.get(), 4);
        face.imagePath = columnText(stmt.get(), 5);
        faces.push_back(std::move(face));
    }
    return faces;
}

std::optional<std::string> FaceDatabase::getUnknownFaceImagePath(int64_t id) {
    Statement stmt(db_, "SELECT image_path FROM unknown_faces WHERE id = ?;");
    sqlite3_bind_int64(stmt.get(), 1, id);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    const auto path = columnText(stmt.get(), 0);
    if (path.empty()) return std::nullopt;
    return path;
}

bool FaceDatabase::deleteUnknownFace(int64_t id) {
    Statement stmt(db_, "DELETE FROM unknown_faces WHERE id = ?;");
    sqlite3_bind_int64(stmt.get(), 1, id);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

bool FaceDatabase::deleteUnknownFaceByTrackId(const std::string& trackId, const std::string& cameraUid) {
    Statement stmt(db_, "DELETE FROM unknown_faces WHERE track_id = ? AND camera_uid = ?;");
    sqlite3_bind_text(stmt.get(), 1, trackId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

bool FaceDatabase::updateUnknownFaceEmbedding(const std::string& trackId,
                                               const std::string& cameraUid,
                                               const std::vector<float>& newEmbedding) {
    validateEmbeddingSize(newEmbedding);
    Statement stmt(db_,
        "UPDATE unknown_faces SET embedding = ?, timestamp = ? "
        "WHERE track_id = ? AND camera_uid = ?;");
    sqlite3_bind_blob(stmt.get(), 1, newEmbedding.data(),
                      static_cast<int>(newEmbedding.size() * sizeof(float)), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 2, static_cast<int64_t>(std::time(nullptr)));
    sqlite3_bind_text(stmt.get(), 3, trackId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

int64_t FaceDatabase::enrollUnknownAsNewPerson(int64_t unknownFaceId, const std::string& name, const std::string& code, int accessLevel) {
    // Read the unknown face record
    Statement readStmt(db_, "SELECT embedding, image_path FROM unknown_faces WHERE id = ?;");
    sqlite3_bind_int64(readStmt.get(), 1, unknownFaceId);
    if (sqlite3_step(readStmt.get()) != SQLITE_ROW) {
        return -1;
    }

    std::vector<float> embedding;
    const auto* blob = sqlite3_column_blob(readStmt.get(), 0);
    const auto blobBytes = sqlite3_column_bytes(readStmt.get(), 0);
    if (blob == nullptr || blobBytes != static_cast<int>(kEmbeddingDimension * sizeof(float))) {
        return -1;
    }
    const auto* data = static_cast<const float*>(blob);
    embedding.assign(data, data + kEmbeddingDimension);

    const std::string imagePath = columnText(readStmt.get(), 1);

    // Create person
    const auto personId = addPerson(name, code, true, imagePath);

    // Set access level
    updatePersonAccessLevel(personId, accessLevel);

    // Add reference embedding
    addReferenceEmbedding(personId, embedding);

    // Delete the unknown face record
    deleteUnknownFace(unknownFaceId);

    return personId;
}

bool FaceDatabase::mergeUnknownWithPerson(int64_t unknownFaceId, int64_t personId) {
    // Read the unknown face embedding and crop image
    Statement readStmt(db_, "SELECT embedding, image_path FROM unknown_faces WHERE id = ?;");
    sqlite3_bind_int64(readStmt.get(), 1, unknownFaceId);
    if (sqlite3_step(readStmt.get()) != SQLITE_ROW) {
        return false;
    }

    const auto* blob = sqlite3_column_blob(readStmt.get(), 0);
    const auto blobBytes = sqlite3_column_bytes(readStmt.get(), 0);
    if (blob == nullptr || blobBytes != static_cast<int>(kEmbeddingDimension * sizeof(float))) {
        return false;
    }
    const auto* data = static_cast<const float*>(blob);
    const std::vector<float> embedding(data, data + kEmbeddingDimension);
    const std::string imagePath = columnText(readStmt.get(), 1);

    // Add reference embedding to existing person
    addReferenceEmbedding(personId, embedding);

    // Use the unknown face crop as default profile photo if person has none
    if (!imagePath.empty()) {
        Person person;
        if (getPerson(personId, person) && person.picturePath.empty()) {
            updatePersonPicturePath(personId, imagePath);
        }
    }

    // Delete the unknown face record
    deleteUnknownFace(unknownFaceId);

    return true;
}

int64_t FaceDatabase::upsertCamera(const CameraConfig& camera) {
    Statement stmt(
        db_,
        "INSERT INTO cameras(camera_uid, name, zone_id, type, stream_enabled, ai_enabled, "
        "connection_type, username, password, stream_uri, encoding, onvif_xaddrs, transport_protocol, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(camera_uid) DO UPDATE SET "
        "name = excluded.name, zone_id = excluded.zone_id, type = excluded.type, "
        "stream_enabled = excluded.stream_enabled, ai_enabled = excluded.ai_enabled, "
        "connection_type = excluded.connection_type, username = excluded.username, password = excluded.password, "
        "stream_uri = excluded.stream_uri, encoding = excluded.encoding, onvif_xaddrs = excluded.onvif_xaddrs, "
        "transport_protocol = excluded.transport_protocol;");
    sqlite3_bind_text(stmt.get(), 1, camera.uid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, camera.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 3, camera.zoneId);
    const auto type = cameraTypeToText(camera.type);
    sqlite3_bind_text(stmt.get(), 4, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 5, camera.streamEnabled ? 1 : 0);
    sqlite3_bind_int(stmt.get(), 6, camera.aiEnabled ? 1 : 0);
    sqlite3_bind_text(stmt.get(), 7, camera.connectionType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 8, camera.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 9, camera.password.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 10, camera.streamUri.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 11, camera.encoding.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 12, camera.onvifXaddrs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 13, camera.transportProtocol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 14, currentUnixTimestamp());
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }

    const auto stored = getCameraConfig(camera.uid);
    return stored.has_value() ? stored->id : 0;
}

std::optional<CameraConfig> FaceDatabase::getCameraConfig(const std::string& cameraUid) {
    Statement stmt(
        db_,
        "SELECT id, camera_uid, name, zone_id, type, stream_enabled, ai_enabled, "
        "connection_type, username, password, stream_uri, encoding, onvif_xaddrs, transport_protocol "
        "FROM cameras WHERE camera_uid = ?;");
    sqlite3_bind_text(stmt.get(), 1, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    CameraConfig camera;
    camera.id = sqlite3_column_int64(stmt.get(), 0);
    camera.uid = columnText(stmt.get(), 1);
    camera.name = columnText(stmt.get(), 2);
    camera.zoneId = sqlite3_column_int64(stmt.get(), 3);
    camera.type = cameraTypeFromText(columnText(stmt.get(), 4));
    camera.streamEnabled = columnBool(stmt.get(), 5);
    camera.aiEnabled = columnBool(stmt.get(), 6);
    camera.connectionType = columnText(stmt.get(), 7);
    camera.username = columnText(stmt.get(), 8);
    camera.password = columnText(stmt.get(), 9);
    camera.streamUri = columnText(stmt.get(), 10);
    camera.encoding = columnText(stmt.get(), 11);
    camera.onvifXaddrs = columnText(stmt.get(), 12);
    camera.transportProtocol = columnText(stmt.get(), 13);
    return camera;
}

std::vector<CameraConfig> FaceDatabase::getAllCameras() {
    Statement stmt(
        db_,
        "SELECT id, camera_uid, name, zone_id, type, stream_enabled, ai_enabled, "
        "connection_type, username, password, stream_uri, encoding, onvif_xaddrs, transport_protocol "
        "FROM cameras ORDER BY id;");
    std::vector<CameraConfig> cameras;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        CameraConfig camera;
        camera.id = sqlite3_column_int64(stmt.get(), 0);
        camera.uid = columnText(stmt.get(), 1);
        camera.name = columnText(stmt.get(), 2);
        camera.zoneId = sqlite3_column_int64(stmt.get(), 3);
        camera.type = cameraTypeFromText(columnText(stmt.get(), 4));
        camera.streamEnabled = columnBool(stmt.get(), 5);
        camera.aiEnabled = columnBool(stmt.get(), 6);
        camera.connectionType = columnText(stmt.get(), 7);
        camera.username = columnText(stmt.get(), 8);
        camera.password = columnText(stmt.get(), 9);
        camera.streamUri = columnText(stmt.get(), 10);
        camera.encoding = columnText(stmt.get(), 11);
        camera.onvifXaddrs = columnText(stmt.get(), 12);
        camera.transportProtocol = columnText(stmt.get(), 13);
        cameras.push_back(camera);
    }
    return cameras;
}

bool FaceDatabase::deleteCamera(const std::string& cameraUid) {
    try {
        execute("BEGIN TRANSACTION;");

        {
            Statement stmt(db_, "DELETE FROM camera_module_config WHERE camera_uid = ?;");
            sqlite3_bind_text(stmt.get(), 1, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                execute("ROLLBACK;");
                return false;
            }
        }

        {
            Statement stmt(
                db_,
                "DELETE FROM location_log "
                "WHERE camera_id IN (SELECT id FROM cameras WHERE camera_uid = ?);");
            sqlite3_bind_text(stmt.get(), 1, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                execute("ROLLBACK;");
                return false;
            }
        }

        {
            Statement stmt(db_, "DELETE FROM cameras WHERE camera_uid = ?;");
            sqlite3_bind_text(stmt.get(), 1, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                execute("ROLLBACK;");
                return false;
            }
        }

        execute("COMMIT;");
        return sqlite3_changes(db_) > 0;
    } catch (...) {
        try {
            execute("ROLLBACK;");
        } catch (...) {}
        return false;
    }
}

bool FaceDatabase::updateCameraStreamEnabled(const std::string& cameraUid, bool enabled) {
    Statement stmt(db_, "UPDATE cameras SET stream_enabled = ? WHERE camera_uid = ?;");
    sqlite3_bind_int(stmt.get(), 1, enabled ? 1 : 0);
    sqlite3_bind_text(stmt.get(), 2, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

bool FaceDatabase::updateCameraAiEnabled(const std::string& cameraUid, bool enabled) {
    Statement stmt(db_, "UPDATE cameras SET ai_enabled = ? WHERE camera_uid = ?;");
    sqlite3_bind_int(stmt.get(), 1, enabled ? 1 : 0);
    sqlite3_bind_text(stmt.get(), 2, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

std::optional<std::pair<bool, std::string>> FaceDatabase::getCameraModuleConfig(const std::string& cameraUid, const std::string& moduleName) {
    Statement stmt(db_, "SELECT enabled, config_json FROM camera_module_config WHERE camera_uid = ? AND module_name = ?;");
    sqlite3_bind_text(stmt.get(), 1, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, moduleName.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) return std::nullopt;
    bool enabled = columnBool(stmt.get(), 0);
    std::string cfg = columnText(stmt.get(), 1);
    return std::make_pair(enabled, cfg);
}

void FaceDatabase::setCameraModuleConfig(const std::string& cameraUid, const std::string& moduleName, bool enabled, const std::string& configJson) {
    Statement stmt(db_,
        "INSERT INTO camera_module_config(camera_uid, module_name, enabled, config_json) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(camera_uid, module_name) DO UPDATE SET enabled = excluded.enabled, config_json = excluded.config_json;");
    sqlite3_bind_text(stmt.get(), 1, cameraUid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, moduleName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 3, enabled ? 1 : 0);
    sqlite3_bind_text(stmt.get(), 4, configJson.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

int64_t FaceDatabase::addZone(const ZoneInfo& zone) {
    Statement stmt(db_, "INSERT INTO zones(name, description, created_at) VALUES(?, ?, ?);");
    sqlite3_bind_text(stmt.get(), 1, zone.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, zone.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 3, currentUnixTimestamp());
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return sqlite3_last_insert_rowid(db_);
}

std::optional<ZoneInfo> FaceDatabase::getZone(int64_t zoneId) {
    Statement stmt(db_, "SELECT id, name, description, created_at, required_access_level FROM zones WHERE id = ?;");
    sqlite3_bind_int64(stmt.get(), 1, zoneId);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    ZoneInfo zone;
    zone.id = sqlite3_column_int64(stmt.get(), 0);
    zone.name = columnText(stmt.get(), 1);
    zone.description = columnText(stmt.get(), 2);
    zone.createdAt = sqlite3_column_int64(stmt.get(), 3);
    zone.requiredAccessLevel = sqlite3_column_int(stmt.get(), 4);
    return zone;
}

std::vector<ZoneInfo> FaceDatabase::getAllZones() {
    Statement stmt(db_, "SELECT id, name, description, created_at, required_access_level FROM zones ORDER BY id;");
    std::vector<ZoneInfo> zones;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        ZoneInfo zone;
        zone.id = sqlite3_column_int64(stmt.get(), 0);
        zone.name = columnText(stmt.get(), 1);
        zone.description = columnText(stmt.get(), 2);
        zone.createdAt = sqlite3_column_int64(stmt.get(), 3);
        zone.requiredAccessLevel = sqlite3_column_int(stmt.get(), 4);
        zones.push_back(zone);
    }
    return zones;
}

int64_t FaceDatabase::openWorkSession(int64_t personId, const std::string& date, int64_t timestamp) {
    Statement stmt(
        db_,
        "INSERT INTO work_sessions(person_id, date, arrival_time, created_at, is_complete) VALUES(?, ?, ?, ?, 0) "
        "ON CONFLICT(person_id, date) DO UPDATE SET arrival_time = COALESCE(work_sessions.arrival_time, excluded.arrival_time);");
    sqlite3_bind_int64(stmt.get(), 1, personId);
    sqlite3_bind_text(stmt.get(), 2, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 3, timestamp);
    sqlite3_bind_int64(stmt.get(), 4, currentUnixTimestamp());
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }

    const auto session = getOpenSession(personId, date);
    return session.has_value() ? session->id : 0;
}

void FaceDatabase::closeWorkSession(int64_t sessionId, int64_t timestamp, int64_t workSeconds, int64_t breakSeconds) {
    Statement stmt(
        db_,
        "UPDATE work_sessions SET departure_time = ?, total_work_seconds = ?, total_break_seconds = ?, is_complete = 1 "
        "WHERE id = ?;");
    sqlite3_bind_int64(stmt.get(), 1, timestamp);
    sqlite3_bind_int64(stmt.get(), 2, workSeconds);
    sqlite3_bind_int64(stmt.get(), 3, breakSeconds);
    sqlite3_bind_int64(stmt.get(), 4, sessionId);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

std::optional<WorkSession> FaceDatabase::getOpenSession(int64_t personId, const std::string& date) {
    Statement stmt(
        db_,
        "SELECT id, person_id, date, arrival_time, departure_time, total_work_seconds, total_break_seconds, is_complete "
        "FROM work_sessions WHERE person_id = ? AND date = ? AND is_complete = 0 LIMIT 1;");
    sqlite3_bind_int64(stmt.get(), 1, personId);
    sqlite3_bind_text(stmt.get(), 2, date.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    return readWorkSession(stmt.get());
}

int64_t FaceDatabase::addBreak(int64_t sessionId, int64_t personId, int64_t timestamp) {
    Statement stmt(db_, "INSERT INTO breaks(session_id, person_id, break_start) VALUES(?, ?, ?);");
    sqlite3_bind_int64(stmt.get(), 1, sessionId);
    sqlite3_bind_int64(stmt.get(), 2, personId);
    sqlite3_bind_int64(stmt.get(), 3, timestamp);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    return sqlite3_last_insert_rowid(db_);
}

void FaceDatabase::closeBreak(int64_t breakId, int64_t timestamp, int64_t seconds) {
    Statement stmt(db_, "UPDATE breaks SET break_end = ?, break_seconds = ? WHERE id = ?;");
    sqlite3_bind_int64(stmt.get(), 1, timestamp);
    sqlite3_bind_int64(stmt.get(), 2, seconds);
    sqlite3_bind_int64(stmt.get(), 3, breakId);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

std::optional<BreakRecord> FaceDatabase::getOpenBreak(int64_t sessionId) {
    Statement stmt(
        db_,
        "SELECT id, session_id, person_id, break_start, break_end, break_seconds FROM breaks "
        "WHERE session_id = ? AND break_end IS NULL LIMIT 1;");
    sqlite3_bind_int64(stmt.get(), 1, sessionId);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    return readBreakRecord(stmt.get());
}

std::vector<BreakRecord> FaceDatabase::getTimeline(int64_t personId, const std::string& date) {
    Statement sessionStmt(db_, "SELECT id FROM work_sessions WHERE person_id = ? AND date = ? LIMIT 1;");
    sqlite3_bind_int64(sessionStmt.get(), 1, personId);
    sqlite3_bind_text(sessionStmt.get(), 2, date.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(sessionStmt.get()) != SQLITE_ROW) {
        return {};
    }

    const auto sessionId = sqlite3_column_int64(sessionStmt.get(), 0);
    Statement stmt(
        db_,
        "SELECT id, session_id, person_id, break_start, break_end, break_seconds FROM breaks WHERE session_id = ? ORDER BY break_start;");
    sqlite3_bind_int64(stmt.get(), 1, sessionId);

    std::vector<BreakRecord> timeline;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        timeline.push_back(readBreakRecord(stmt.get()));
    }
    return timeline;
}

void FaceDatabase::logLocation(int64_t personId, int64_t zoneId, int64_t cameraId, int64_t timestamp) {
    Statement stmt(db_, "INSERT INTO location_log(person_id, zone_id, camera_id, timestamp) VALUES(?, ?, ?, ?);");
    sqlite3_bind_int64(stmt.get(), 1, personId);
    sqlite3_bind_int64(stmt.get(), 2, zoneId);
    sqlite3_bind_int64(stmt.get(), 3, cameraId);
    sqlite3_bind_int64(stmt.get(), 4, timestamp);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

std::optional<LocationRecord> FaceDatabase::getPersonCurrentLocation(int64_t personId) {
    Statement stmt(
        db_,
        "SELECT l.person_id, l.zone_id, l.camera_id, l.timestamp, COALESCE(z.name, '') "
        "FROM location_log l LEFT JOIN zones z ON z.id = l.zone_id "
        "WHERE l.person_id = ? ORDER BY l.timestamp DESC LIMIT 1;");
    sqlite3_bind_int64(stmt.get(), 1, personId);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    LocationRecord record;
    record.personId = sqlite3_column_int64(stmt.get(), 0);
    record.zoneId = sqlite3_column_int64(stmt.get(), 1);
    record.cameraId = sqlite3_column_int64(stmt.get(), 2);
    record.timestamp = sqlite3_column_int64(stmt.get(), 3);
    record.zoneName = columnText(stmt.get(), 4);
    return record;
}

void FaceDatabase::logTardiness(const TardinessRecord& record) {
    Statement stmt(
        db_,
        "INSERT INTO tardiness_log(person_id, date, expected_arrival, actual_arrival, tardiness_seconds, is_absent) "
        "VALUES(?, ?, ?, ?, ?, ?);");
    sqlite3_bind_int64(stmt.get(), 1, record.personId);
    sqlite3_bind_text(stmt.get(), 2, record.date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 3, record.expectedArrival);
    sqlite3_bind_int64(stmt.get(), 4, record.actualArrival);
    sqlite3_bind_int64(stmt.get(), 5, record.tardinessSeconds);
    sqlite3_bind_int(stmt.get(), 6, record.isAbsent ? 1 : 0);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

std::vector<TardinessRecord> FaceDatabase::getTardinessList(const std::string& date) {
    Statement stmt(
        db_,
        "SELECT person_id, date, expected_arrival, actual_arrival, tardiness_seconds, is_absent "
        "FROM tardiness_log WHERE date = ? ORDER BY person_id;");
    sqlite3_bind_text(stmt.get(), 1, date.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<TardinessRecord> items;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        TardinessRecord record;
        record.personId = sqlite3_column_int64(stmt.get(), 0);
        record.date = columnText(stmt.get(), 1);
        record.expectedArrival = sqlite3_column_int64(stmt.get(), 2);
        record.actualArrival = sqlite3_column_int64(stmt.get(), 3);
        record.tardinessSeconds = sqlite3_column_int64(stmt.get(), 4);
        record.isAbsent = columnBool(stmt.get(), 5);
        items.push_back(record);
    }
    return items;
}

DailySummary FaceDatabase::getDailySummary(int64_t personId, const std::string& date) {
    DailySummary summary;
    summary.personId = personId;
    summary.date = date;

    Statement stmt(
        db_,
        "SELECT id, person_id, date, arrival_time, departure_time, total_work_seconds, total_break_seconds, is_complete "
        "FROM work_sessions WHERE person_id = ? AND date = ? LIMIT 1;");
    sqlite3_bind_int64(stmt.get(), 1, personId);
    sqlite3_bind_text(stmt.get(), 2, date.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const auto session = readWorkSession(stmt.get());
        summary.arrivalTime = session.arrivalTime;
        summary.departureTime = session.departureTime;
        summary.workSeconds = session.totalWorkSeconds;
        summary.breakSeconds = session.totalBreakSeconds;
    }

    for (const auto& breakItem : getTimeline(personId, date)) {
        summary.breakIntervals.emplace_back(breakItem.breakStart, breakItem.breakEnd);
    }

    Statement tardinessStmt(
        db_,
        "SELECT tardiness_seconds, is_absent FROM tardiness_log WHERE person_id = ? AND date = ? "
        "ORDER BY id DESC LIMIT 1;");
    sqlite3_bind_int64(tardinessStmt.get(), 1, personId);
    sqlite3_bind_text(tardinessStmt.get(), 2, date.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(tardinessStmt.get()) == SQLITE_ROW) {
        summary.tardinessSeconds = sqlite3_column_int64(tardinessStmt.get(), 0);
        summary.isAbsent = columnBool(tardinessStmt.get(), 1);
    }

    return summary;
}

void FaceDatabase::upsertPersonSchedule(const PersonSchedule& schedule) {
    Statement stmt(
        db_,
        "INSERT INTO person_schedules(person_id, expected_arrival, expected_departure, grace_minutes, work_days, updated_at) "
        "VALUES(?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(person_id) DO UPDATE SET "
        "expected_arrival = excluded.expected_arrival, "
        "expected_departure = excluded.expected_departure, "
        "grace_minutes = excluded.grace_minutes, "
        "work_days = excluded.work_days, "
        "updated_at = excluded.updated_at;");
    sqlite3_bind_int64(stmt.get(), 1, schedule.personId);
    sqlite3_bind_text(stmt.get(), 2, schedule.expectedArrival.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, schedule.expectedDeparture.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 4, schedule.graceMinutes);
    sqlite3_bind_text(stmt.get(), 5, schedule.workDays.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 6, currentUnixTimestamp());
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

std::optional<PersonSchedule> FaceDatabase::getPersonSchedule(int64_t personId) {
    Statement stmt(
        db_,
        "SELECT person_id, expected_arrival, expected_departure, grace_minutes, work_days "
        "FROM person_schedules WHERE person_id = ? LIMIT 1;");
    sqlite3_bind_int64(stmt.get(), 1, personId);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    PersonSchedule schedule;
    schedule.personId = sqlite3_column_int64(stmt.get(), 0);
    schedule.expectedArrival = columnText(stmt.get(), 1);
    schedule.expectedDeparture = columnText(stmt.get(), 2);
    schedule.graceMinutes = sqlite3_column_int(stmt.get(), 3);
    schedule.workDays = columnText(stmt.get(), 4);
    return schedule;
}

void FaceDatabase::setSetting(const std::string& key, const std::string& value, const std::string& description) {
    Statement stmt(
        db_,
        "INSERT INTO system_settings(key, value, description, updated_at) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value, description = excluded.description, updated_at = excluded.updated_at;");
    sqlite3_bind_text(stmt.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 4, currentUnixTimestamp());
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
}

std::optional<std::string> FaceDatabase::getSetting(const std::string& key) {
    Statement stmt(db_, "SELECT value FROM system_settings WHERE key = ? LIMIT 1;");
    sqlite3_bind_text(stmt.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    return columnText(stmt.get(), 0);
}

std::vector<Person> FaceDatabase::getAllPersons() const {
    Statement stmt(db_, "SELECT id, name, code, created_at, picture_path, access_level FROM persons ORDER BY id;");
    std::vector<Person> persons;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        Person p;
        p.id = sqlite3_column_int64(stmt.get(), 0);
        p.name = columnText(stmt.get(), 1);
        p.code = columnText(stmt.get(), 2);
        p.createdAt = sqlite3_column_int64(stmt.get(), 3);
        p.picturePath = columnText(stmt.get(), 4);
        p.access_level = sqlite3_column_int(stmt.get(), 5);
        persons.push_back(std::move(p));
    }
    return persons;
}

bool FaceDatabase::updatePerson(int64_t personId, const std::string& name, const std::string& code) {
    Statement stmt(db_, "UPDATE persons SET name = ?, code = ? WHERE id = ?;");
    sqlite3_bind_text(stmt.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (code.empty()) {
        sqlite3_bind_null(stmt.get(), 2);
    } else {
        sqlite3_bind_text(stmt.get(), 2, code.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int64(stmt.get(), 3, personId);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        return false;
    }
    return true;
}

bool FaceDatabase::deletePerson(int64_t personId) {
    // First delete dependent records (foreign keys cascading may be set, but we explicitly delete)
    Statement delRefs(db_, "DELETE FROM reference_faces WHERE person_id = ?;");
    sqlite3_bind_int64(delRefs.get(), 1, personId);
    sqlite3_step(delRefs.get());
    
    Statement delRaw(db_, "DELETE FROM raw_detections WHERE person_id = ?;");
    sqlite3_bind_int64(delRaw.get(), 1, personId);
    sqlite3_step(delRaw.get());
    
    Statement delAttendance(db_, "DELETE FROM attendance_logs WHERE person_id = ?;");
    sqlite3_bind_int64(delAttendance.get(), 1, personId);
    sqlite3_step(delAttendance.get());
    
    Statement delSchedule(db_, "DELETE FROM person_schedules WHERE person_id = ?;");
    sqlite3_bind_int64(delSchedule.get(), 1, personId);
    sqlite3_step(delSchedule.get());
    
    Statement delPerson(db_, "DELETE FROM persons WHERE id = ?;");
    sqlite3_bind_int64(delPerson.get(), 1, personId);
    if (sqlite3_step(delPerson.get()) != SQLITE_DONE) {
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

bool FaceDatabase::updatePersonPicturePath(int64_t personId, const std::string& picturePath) {
    Statement stmt(db_, "UPDATE persons SET picture_path = ? WHERE id = ?;");
    sqlite3_bind_text(stmt.get(), 1, picturePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.get(), 2, personId);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        return false;
    }
    return true;
}

bool FaceDatabase::updatePersonAccessLevel(int64_t personId, int accessLevel) {
    Statement stmt(db_, "UPDATE persons SET access_level = ? WHERE id = ?;");
    sqlite3_bind_int(stmt.get(), 1, accessLevel);
    sqlite3_bind_int64(stmt.get(), 2, personId);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) return false;
    return sqlite3_changes(db_) > 0;
}

bool FaceDatabase::updateZoneRequiredAccessLevel(int64_t zoneId, int requiredAccessLevel) {
    Statement stmt(db_, "UPDATE zones SET required_access_level = ? WHERE id = ?;");
    sqlite3_bind_int(stmt.get(), 1, requiredAccessLevel);
    sqlite3_bind_int64(stmt.get(), 2, zoneId);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) return false;
    return sqlite3_changes(db_) > 0;
}

}  // namespace face
