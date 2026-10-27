import 'dart:typed_data';

import '../data/api_client.dart';
import '../data/models.dart';

export '../data/api_client.dart' show ApiException;

int _idOf(Map<String, Object?> j) => (j['id'] as num?)?.toInt() ?? 0;
int _personIdOf(Map<String, Object?> j) => (j['personId'] as num?)?.toInt() ?? 0;

/// Server health.
class SystemRepository {
  SystemRepository(this._api);

  final ApiClient _api;

  Future<Health> health() async => Health.fromJson(await _api.getObj('/api/health'));
}

/// Persons + their profile photos, schedules and last-seen location.
class PersonsRepository {
  PersonsRepository(this._api);

  final ApiClient _api;

  Future<List<Person>> list() async =>
      (await _api.getList('/api/persons')).map(Person.fromJson).toList();

  Future<Person> get(int id) async =>
      Person.fromJson(await _api.getObj('/api/persons/$id'));

  Future<int> create({required String name, String code = ''}) async =>
      _idOf(await _api.post('/api/persons', {'name': name, 'code': code}));

  Future<void> update(int id, {required String name, String code = ''}) =>
      _api.put('/api/persons/$id', {'name': name, 'code': code});

  Future<void> setAccessLevel(int id, int level) =>
      _api.put('/api/persons/$id/access-level', {'accessLevel': level});

  Future<void> delete(int id) => _api.delete('/api/persons/$id');

  /// Profile photo bytes, or null when the person has none.
  Future<Uint8List?> picture(int id) => _api.getImage('/api/persons/$id/picture');

  Future<void> uploadPicture(int id, String base64, {String extension = 'jpg'}) =>
      _api.put('/api/persons/$id/picture', {
        'image': base64,
        'extension': extension,
      });

  /// Null when the person has no saved schedule (backend 404s).
  Future<Schedule?> schedule(int id) async {
    try {
      return Schedule.fromJson(await _api.getObj('/api/schedules/$id'));
    } on ApiException catch (e) {
      if (e.statusCode == 404) return null;
      rethrow;
    }
  }

  Future<void> saveSchedule(Schedule s) =>
      _api.put('/api/schedules', s.toJson());

  /// Null when the person has never been seen.
  Future<CurrentLocation?> currentLocation(int id) async {
    try {
      return CurrentLocation.fromJson(
          await _api.getObj('/api/attendance/current-location/$id'));
    } on ApiException catch (e) {
      if (e.statusCode == 404) return null;
      rethrow;
    }
  }
}

class CamerasRepository {
  CamerasRepository(this._api);

  final ApiClient _api;

  Future<List<Camera>> list() async =>
      (await _api.getList('/api/cameras')).map(Camera.fromJson).toList();

  Future<void> create(Camera c) => _api.post('/api/cameras', c.toCreateJson());

  Future<void> configure(Camera c) =>
      _api.put('/api/cameras/${c.uid}/configure', c.toConfigureJson());

  Future<void> setStreaming(String uid, bool active) =>
      _api.put('/api/cameras/$uid/active', {'active': active});

  Future<void> delete(String uid) => _api.delete('/api/cameras/$uid');

  /// Device discovery (deep scan can take minutes on big networks).
  Future<List<DiscoveredCamera>> discover({bool deep = false}) async =>
      (await _api.postList('/api/cameras/discover', {'deep': deep}, true))
          .map(DiscoveredCamera.fromJson)
          .toList();

  Future<({bool ok, bool requiresAuth, String message})> test(
      String uri, String user, String pass) async {
    final r = await _api.post('/api/cameras/test', {
      'streamUri': uri,
      'username': user,
      'password': pass,
    });
    return (
      ok: r['ok'] == true,
      requiresAuth: r['requiresAuth'] == true,
      message: r['message'] is String ? r['message'] as String : '',
    );
  }
}

class ZonesRepository {
  ZonesRepository(this._api);

  final ApiClient _api;

  Future<List<Zone>> list() async =>
      (await _api.getList('/api/zones')).map(Zone.fromJson).toList();

  Future<Zone> create(String name, String description) async =>
      Zone.fromJson(await _api.post('/api/zones', {
        'name': name,
        'description': description,
      }));

  Future<void> setRequiredLevel(int id, int level) =>
      _api.put('/api/zones/$id/access-level', {'requiredAccessLevel': level});
}

class AttendanceRepository {
  AttendanceRepository(this._api);

  final ApiClient _api;

  /// One summary per registered person for [dateKey] (YYYY-MM-DD).
  Future<List<DailySummary>> summaries(String dateKey) async =>
      (await _api.getList('/api/attendance/summary', query: {'date': dateKey}))
          .map(DailySummary.fromJson)
          .toList();

  Future<DailySummary> summary(int personId, String dateKey) async =>
      DailySummary.fromJson(await _api.getObj(
          '/api/attendance/summary',
          query: {'personId': '$personId', 'date': dateKey}));

  Future<List<BreakRecord>> timeline(int personId, String dateKey) async =>
      (await _api.getList('/api/attendance/timeline', query: {
        'personId': '$personId',
        'date': dateKey,
      }))
          .map(BreakRecord.fromJson)
          .toList();

  Future<List<TardinessRecord>> tardiness(String dateKey) async =>
      (await _api.getList('/api/attendance/tardiness', query: {'date': dateKey}))
          .map(TardinessRecord.fromJson)
          .toList();
}

class UnknownFacesRepository {
  UnknownFacesRepository(this._api);

  final ApiClient _api;

  Future<List<UnknownFace>> list() async =>
      (await _api.getList('/api/unknown-faces')).map(UnknownFace.fromJson).toList();

  Future<Uint8List?> image(int id) => _api.getImage('/api/unknown-faces/$id/image');

  /// Returns the new person's id.
  Future<int> enrollAsNew(int faceId, {required String name, String code = '', int accessLevel = 2}) async =>
      _personIdOf(await _api.post('/api/unknown-faces/$faceId/enroll', {
        'name': name,
        'code': code,
        'accessLevel': accessLevel,
      }));

  Future<void> mergeInto(int faceId, int personId) =>
      _api.post('/api/unknown-faces/$faceId/merge', {'personId': personId});

  Future<void> discard(int id) => _api.delete('/api/unknown-faces/$id');
}
