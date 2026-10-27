/// Typed models matching the backend REST shapes exactly
/// (verified against `backend/http/http_server.cpp` + `RecognitionTypes.h`).
///
/// Parsing is tolerant: missing/null fields fall back to defaults instead of
/// throwing, so a slightly older backend never breaks the console.
library;

import 'package:flutter/foundation.dart';

int _i(Object? v, [int d = 0]) => v is num ? v.toInt() : d;
bool _b(Object? v, [bool d = false]) => v is bool ? v : d;
String _s(Object? v, [String d = '']) => v is String ? v : d;

List<Map<String, Object?>> _objList(Object? v) => v is List
    ? v.whereType<Map>().map((e) => Map<String, Object?>.from(e)).toList()
    : const [];

class Person {
  const Person({
    required this.id,
    required this.name,
    required this.code,
    this.picturePath = '',
    this.createdAt = 0,
    this.accessLevel = 2,
  });

  factory Person.fromJson(Map<String, Object?> j) => Person(
        id: _i(j['id']),
        name: _s(j['name']),
        code: _s(j['code']),
        picturePath: _s(j['picturePath']),
        createdAt: _i(j['createdAt']),
        accessLevel: _i(j['accessLevel'], 2),
      );

  final int id;
  final String name;
  final String code;
  final String picturePath;
  final int createdAt; // unix seconds (0 = unknown)
  final int accessLevel;

  bool get hasPicture => picturePath.isNotEmpty;

  Person copyWith({
    String? name,
    String? code,
    int? accessLevel,
    String? picturePath,
  }) =>
      Person(
        id: id,
        name: name ?? this.name,
        code: code ?? this.code,
        picturePath: picturePath ?? this.picturePath,
        createdAt: createdAt,
        accessLevel: accessLevel ?? this.accessLevel,
      );
}

enum CameraRole { entry, exit, both }

CameraRole _role(String v) => switch (v) {
      'ENTRY' => CameraRole.entry,
      'EXIT' => CameraRole.exit,
      _ => CameraRole.both,
    };

extension CameraRoleX on CameraRole {
  String get api => switch (this) {
        CameraRole.entry => 'ENTRY',
        CameraRole.exit => 'EXIT',
        CameraRole.both => 'BOTH',
      };
}

class Camera {
  const Camera({
    required this.uid,
    this.id = 0,
    required this.name,
    this.zoneId = 0,
    this.role = CameraRole.both,
    this.streamEnabled = true,
    this.aiEnabled = true,
    this.connectionType = 'RTSP',
    this.username = '',
    this.password = '',
    this.streamUri = '',
    this.encoding = '',
    this.onvifXaddrs = '',
    this.transportProtocol = 'tcp',
  });

  factory Camera.fromJson(Map<String, Object?> j) => Camera(
        id: _i(j['id']),
        uid: _s(j['uid']),
        name: _s(j['name']),
        zoneId: _i(j['zoneId']),
        role: _role(_s(j['type'], 'BOTH')),
        streamEnabled: _b(j['streamEnabled'], true),
        aiEnabled: _b(j['aiEnabled'], true),
        connectionType: _s(j['connectionType'], 'RTSP'),
        username: _s(j['username']),
        password: _s(j['password']),
        streamUri: _s(j['streamUri']),
        encoding: _s(j['encoding']),
        onvifXaddrs: _s(j['onvifXaddrs']),
        transportProtocol: _s(j['transportProtocol'], 'tcp'),
      );

  final int id;
  final String uid;
  final String name;
  final int zoneId;
  final CameraRole role;
  final bool streamEnabled;
  final bool aiEnabled;
  final String connectionType; // LOCAL | RTSP | ONVIF
  final String username;
  final String password;
  final String streamUri;
  final String encoding;
  final String onvifXaddrs;
  final String transportProtocol;

  Map<String, Object?> toCreateJson() => {
        'uid': uid,
        'name': name,
        'zoneId': zoneId,
        'type': role.api,
        'streamEnabled': streamEnabled,
        'aiEnabled': aiEnabled,
        'connectionType': connectionType,
        'username': username,
        'password': password,
        'streamUri': streamUri,
        'encoding': encoding,
        'onvifXaddrs': onvifXaddrs,
        'transportProtocol': transportProtocol,
      };

  Map<String, Object?> toConfigureJson() => {
        'name': name,
        'zoneId': zoneId,
        'type': role.api,
        'streamEnabled': streamEnabled,
        'aiEnabled': aiEnabled,
        'connectionType': connectionType,
        'username': username,
        'password': password,
        'streamUri': streamUri,
        'encoding': encoding,
        'onvifXaddrs': onvifXaddrs,
        'transportProtocol': transportProtocol,
      };
}

class Zone {
  const Zone({
    required this.id,
    required this.name,
    this.description = '',
    this.requiredAccessLevel = 2,
    this.createdAt = 0,
  });

  factory Zone.fromJson(Map<String, Object?> j) => Zone(
        id: _i(j['id']),
        name: _s(j['name']),
        description: _s(j['description']),
        requiredAccessLevel: _i(j['requiredAccessLevel'], 2),
        createdAt: _i(j['createdAt']),
      );

  final int id;
  final String name;
  final String description;
  final int requiredAccessLevel;
  final int createdAt;
}

class BreakInterval {
  const BreakInterval(this.start, this.end);

  factory BreakInterval.fromJson(List<Object?> j) =>
      BreakInterval(_i(j.isNotEmpty ? j[0] : 0), _i(j.length > 1 ? j[1] : 0));

  final int start; // unix seconds
  final int end; // unix seconds
}

class DailySummary {
  const DailySummary({
    required this.personId,
    required this.date,
    this.arrival = 0,
    this.departure = 0,
    this.workSeconds = 0,
    this.breakSeconds = 0,
    this.tardinessSeconds = 0,
    this.isAbsent = false,
    this.breaks = const [],
  });

  factory DailySummary.fromJson(Map<String, Object?> j) => DailySummary(
        personId: _i(j['personId']),
        date: _s(j['date']),
        arrival: _i(j['arrivalTime']),
        departure: _i(j['departureTime']),
        workSeconds: _i(j['workSeconds']),
        breakSeconds: _i(j['breakSeconds']),
        tardinessSeconds: _i(j['tardinessSeconds']),
        isAbsent: _b(j['isAbsent']),
        breaks: (j['breakIntervals'] as List? ?? const [])
            .whereType<List>()
            .map(BreakInterval.fromJson)
            .toList(),
      );

  final int personId;
  final String date; // YYYY-MM-DD
  final int arrival; // unix seconds, 0 = none
  final int departure;
  final int workSeconds;
  final int breakSeconds;
  final int tardinessSeconds;
  final bool isAbsent;
  final List<BreakInterval> breaks;
}

/// UI-level attendance status derived from a [DailySummary].
enum AttStatus { present, late, absent, notArrived, notRecorded }

AttStatus summaryStatus(DailySummary s, {required bool isToday}) {
  if (s.isAbsent) return AttStatus.absent;
  if (s.arrival > 0) {
    return s.tardinessSeconds > 0 ? AttStatus.late : AttStatus.present;
  }
  return isToday ? AttStatus.notArrived : AttStatus.notRecorded;
}

class TardinessRecord {
  const TardinessRecord({
    required this.personId,
    required this.date,
    this.expectedArrival = 0,
    this.actualArrival = 0,
    this.tardinessSeconds = 0,
    this.isAbsent = false,
  });

  factory TardinessRecord.fromJson(Map<String, Object?> j) => TardinessRecord(
        personId: _i(j['personId']),
        date: _s(j['date']),
        expectedArrival: _i(j['expectedArrival']),
        actualArrival: _i(j['actualArrival']),
        tardinessSeconds: _i(j['tardinessSeconds']),
        isAbsent: _b(j['isAbsent']),
      );

  final int personId;
  final String date;
  final int expectedArrival; // unix
  final int actualArrival; // unix
  final int tardinessSeconds;
  final bool isAbsent;
}

class BreakRecord {
  const BreakRecord({
    required this.id,
    required this.personId,
    this.breakStart = 0,
    this.breakEnd = 0,
    this.breakSeconds = 0,
  });

  factory BreakRecord.fromJson(Map<String, Object?> j) => BreakRecord(
        id: _i(j['id']),
        personId: _i(j['personId']),
        breakStart: _i(j['breakStart']),
        breakEnd: _i(j['breakEnd']),
        breakSeconds: _i(j['breakSeconds']),
      );

  final int id;
  final int personId;
  final int breakStart; // unix
  final int breakEnd; // unix, 0 = still on break
  final int breakSeconds;
}

class UnknownFace {
  const UnknownFace({
    required this.id,
    this.trackId = '',
    this.cameraUid = '',
    this.timestamp = 0,
  });

  factory UnknownFace.fromJson(Map<String, Object?> j) => UnknownFace(
        id: _i(j['id']),
        trackId: _s(j['trackId']),
        cameraUid: _s(j['cameraUid']),
        timestamp: _i(j['timestamp']),
      );

  final int id;
  final String trackId;
  final String cameraUid;
  final int timestamp; // unix seconds
}

class Schedule {
  const Schedule({
    required this.personId,
    this.arrival = '08:00',
    this.departure = '17:00',
    this.graceMinutes = 15,
    this.workDays = '1,2,3,4,5',
  });

  factory Schedule.fromJson(Map<String, Object?> j) => Schedule(
        personId: _i(j['personId']),
        arrival: _s(j['expectedArrival'], '08:00'),
        departure: _s(j['expectedDeparture'], '17:00'),
        graceMinutes: _i(j['graceMinutes'], 15),
        workDays: _s(j['workDays'], '1,2,3,4,5'),
      );

  /// ISO weekday numbers 1(Mon)..7(Sun), as stored by the backend.
  Set<int> get daySet =>
      workDays.split(',').map((e) => int.tryParse(e.trim())).nonNulls.toSet();

  final int personId;
  final String arrival; // "HH:MM"
  final String departure; // "HH:MM"
  final int graceMinutes;
  final String workDays; // "1,2,3,4,5"

  Map<String, Object?> toJson() => {
        'personId': personId,
        'expectedArrival': arrival,
        'expectedDeparture': departure,
        'graceMinutes': graceMinutes,
        'workDays': (daySet.toList()..sort()).join(','),
      };
}

class CurrentLocation {
  const CurrentLocation({
    required this.personId,
    this.zoneId = 0,
    this.cameraId = 0,
    this.timestamp = 0,
    this.zoneName = '',
  });

  factory CurrentLocation.fromJson(Map<String, Object?> j) => CurrentLocation(
        personId: _i(j['personId']),
        zoneId: _i(j['zoneId']),
        cameraId: _i(j['cameraId']),
        timestamp: _i(j['timestamp']),
        zoneName: _s(j['zoneName']),
      );

  final int personId;
  final int zoneId;
  final int cameraId;
  final int timestamp; // unix
  final String zoneName;
}

class Health {
  const Health({required this.ok, this.version = ''});

  factory Health.fromJson(Map<String, Object?> j) =>
      Health(ok: _s(j['status']) == 'ok', version: _s(j['version']));

  final bool ok;
  final String version;
}

class MediaProfile {
  const MediaProfile({
    this.token = '',
    this.name = '',
    this.width = 0,
    this.height = 0,
    this.fps = 0,
    this.encoding = '',
    this.streamUri = '',
  });

  factory MediaProfile.fromJson(Map<String, Object?> j) => MediaProfile(
        token: _s(j['token']),
        name: _s(j['name']),
        width: _i(j['width']),
        height: _i(j['height']),
        fps: _i(j['fps']),
        encoding: _s(j['encoding']),
        streamUri: _s(j['streamUri']),
      );

  final String token;
  final String name;
  final int width;
  final int height;
  final int fps;
  final String encoding;
  final String streamUri;
}

/// A device found by `POST /api/cameras/discover`.
class DiscoveredCamera {
  const DiscoveredCamera({
    this.devicePath = '',
    this.streamUri = '',
    this.cardName = '',
    this.driver = '',
    this.type = 'UNKNOWN', // LOCAL | RTSP | ONVIF | UNKNOWN
    this.manufacturer = '',
    this.model = '',
    this.requiresAuth = false,
    this.username = '',
    this.password = '',
    this.mediaProfiles = const [],
  });

  factory DiscoveredCamera.fromJson(Map<String, Object?> j) => DiscoveredCamera(
        devicePath: _s(j['devicePath']),
        streamUri: _s(j['streamUri']),
        cardName: _s(j['cardName']),
        driver: _s(j['driver']),
        type: _s(j['type'], 'UNKNOWN'),
        manufacturer: _s(j['manufacturer']),
        model: _s(j['model']),
        requiresAuth: _b(j['requiresAuth']),
        username: _s(j['username']),
        password: _s(j['password']),
        mediaProfiles:
            _objList(j['mediaProfiles']).map(MediaProfile.fromJson).toList(),
      );

  final String devicePath;
  final String streamUri;
  final String cardName;
  final String driver;
  final String type;
  final String manufacturer;
  final String model;
  final bool requiresAuth;
  final String username;
  final String password;
  final List<MediaProfile> mediaProfiles;

  /// A short, human label for the scan result list.
  String get label {
    if (type == 'LOCAL') return cardName.isNotEmpty ? cardName : devicePath;
    if (model.isNotEmpty) {
      return manufacturer.isNotEmpty ? '$manufacturer $model' : model;
    }
    return streamUri;
  }
}

@immutable
class AppSettings {
  const AppSettings({
    this.localeCode = 'fa',
    this.themeMode = ThemeModeOption.system,
    this.serverUrl = '',
  });

  final String localeCode; // 'fa' | 'en'
  final ThemeModeOption themeMode;
  /// Empty means same-origin (`/api` behind the nginx proxy).
  final String serverUrl;

  AppSettings copyWith({
    String? localeCode,
    ThemeModeOption? themeMode,
    String? serverUrl,
  }) =>
      AppSettings(
        localeCode: localeCode ?? this.localeCode,
        themeMode: themeMode ?? this.themeMode,
        serverUrl: serverUrl ?? this.serverUrl,
      );
}

enum ThemeModeOption { system, light, dark }
