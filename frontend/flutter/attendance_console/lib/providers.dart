import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'data/api_client.dart';
import 'data/models.dart';
import 'data/repositories.dart';

// ── Settings (locale / theme / server URL) ────────────────────────────────

/// Loaded once by `main()` before `runApp`, consumed by [SettingsController].
AppSettings bootSettings = const AppSettings();

class SettingsController extends Notifier<AppSettings> {
  static const _kLocale = 'settings.locale';
  static const _kTheme = 'settings.themeMode';
  static const _kServer = 'settings.serverUrl';

  @override
  AppSettings build() => bootSettings;

  Future<void> _persist() async {
    final p = await SharedPreferences.getInstance();
    await p.setString(_kLocale, state.localeCode);
    await p.setInt(_kTheme, state.themeMode.index);
    await p.setString(_kServer, state.serverUrl);
  }

  Future<void> setLocale(String code) async {
    state = state.copyWith(localeCode: code);
    await _persist();
  }

  Future<void> setThemeMode(ThemeModeOption mode) async {
    state = state.copyWith(themeMode: mode);
    await _persist();
  }

  /// Empty string restores the automatic default.
  Future<void> setServerUrl(String url) async {
    state = state.copyWith(serverUrl: url.trim());
    await _persist();
  }
}

final settingsProvider =
    NotifierProvider<SettingsController, AppSettings>(SettingsController.new);

// ── API client + repositories ────────────────────────────────────────────

final apiClientProvider = Provider<ApiClient>((ref) {
  final url = ref.watch(settingsProvider.select((s) => s.serverUrl));
  final client = ApiClient(baseUrl: url.isEmpty ? ApiClient.defaultBaseUrl : url);
  ref.onDispose(client.close);
  return client;
});

final systemRepoProvider = Provider((ref) => SystemRepository(ref.watch(apiClientProvider)));
final personsRepoProvider = Provider((ref) => PersonsRepository(ref.watch(apiClientProvider)));
final camerasRepoProvider = Provider((ref) => CamerasRepository(ref.watch(apiClientProvider)));
final zonesRepoProvider = Provider((ref) => ZonesRepository(ref.watch(apiClientProvider)));
final attendanceRepoProvider = Provider((ref) => AttendanceRepository(ref.watch(apiClientProvider)));
final unknownRepoProvider = Provider((ref) => UnknownFacesRepository(ref.watch(apiClientProvider)));

// ── Server data ───────────────────────────────────────────────────────────
// Every provider depends on its repository, so changing the server URL
// rebuilds the whole graph automatically.

final healthProvider = FutureProvider<Health>((ref) async {
  // Small grace: health is polled, a quick failure would flash the UI.
  await Future<void>.delayed(const Duration(milliseconds: 150));
  return ref.watch(systemRepoProvider).health();
});

final personsProvider = FutureProvider<List<Person>>(
    (ref) => ref.watch(personsRepoProvider).list());

final camerasProvider = FutureProvider<List<Camera>>(
    (ref) => ref.watch(camerasRepoProvider).list());

final zonesProvider = FutureProvider<List<Zone>>(
    (ref) => ref.watch(zonesRepoProvider).list());

final unknownFacesProvider = FutureProvider<List<UnknownFace>>(
    (ref) => ref.watch(unknownRepoProvider).list());

/// All-person summaries for one date key (YYYY-MM-DD).
final summariesProvider =
    FutureProvider.family<List<DailySummary>, String>(
        (ref, dateKey) => ref.watch(attendanceRepoProvider).summaries(dateKey));

final tardinessProvider =
    FutureProvider.family<List<TardinessRecord>, String>(
        (ref, dateKey) => ref.watch(attendanceRepoProvider).tardiness(dateKey));

/// Per-person day view: summary + break timeline for a date key.
class PersonDay {
  const PersonDay({required this.summary, required this.breaks});

  final DailySummary summary;
  final List<BreakRecord> breaks;
}

final personDayProvider =
    FutureProvider.family<PersonDay, ({int personId, String dateKey})>(
        (ref, arg) async {
  final repo = ref.watch(attendanceRepoProvider);
  final results = await Future.wait<dynamic>([
    repo.summary(arg.personId, arg.dateKey),
    repo.timeline(arg.personId, arg.dateKey),
  ]);
  return PersonDay(summary: results[0] as DailySummary, breaks: results[1] as List<BreakRecord>);
});

/// Null = no schedule saved yet → show defaults (08:00 / 17:00 / 15 / Mon-Fri).
final personScheduleProvider = FutureProvider.family<Schedule?, int>(
    (ref, personId) => ref.watch(personsRepoProvider).schedule(personId));

final personLocationProvider = FutureProvider.family<CurrentLocation?, int>(
    (ref, personId) => ref.watch(personsRepoProvider).currentLocation(personId));

/// Profile photo bytes (null = no picture). Cached until invalidated,
/// e.g. after an upload.
final personPictureProvider = FutureProvider.family<Uint8List?, int>(
    (ref, personId) => ref.watch(personsRepoProvider).picture(personId));

final unknownFaceImageProvider = FutureProvider.family<Uint8List?, int>(
    (ref, faceId) => ref.watch(unknownRepoProvider).image(faceId));

/// Person entity by id (for deep links /people/:id).
final personProvider = FutureProvider.family<Person, int>(
    (ref, id) => ref.watch(personsRepoProvider).get(id));

// ── Helpers ───────────────────────────────────────────────────────────────

/// Last N date keys ending today (oldest first).
List<String> lastDateKeys(int n, [DateTime? today]) {
  final now = today ?? DateTime.now();
  return List.generate(n, (i) {
    final d = DateTime(now.year, now.month, now.day - (n - 1 - i));
    return '${d.year.toString().padLeft(4, '0')}-'
        '${d.month.toString().padLeft(2, '0')}-'
        '${d.day.toString().padLeft(2, '0')}';
  });
}

/// Today's date key.
String todayKey() {
  final n = DateTime.now();
  return '${n.year.toString().padLeft(4, '0')}-'
      '${n.month.toString().padLeft(2, '0')}-'
      '${n.day.toString().padLeft(2, '0')}';
}
