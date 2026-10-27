import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:attendance_console/app.dart';
import 'package:attendance_console/core/app_strings.dart';
import 'package:attendance_console/core/formatters.dart';
import 'package:attendance_console/data/api_client.dart';
import 'package:attendance_console/data/models.dart';
import 'package:attendance_console/data/repositories.dart';
import 'package:attendance_console/features/cameras/cameras_page.dart';
import 'package:attendance_console/features/people/people_page.dart';
import 'package:attendance_console/features/unknown/unknown_page.dart';
import 'package:attendance_console/features/zones/zones_page.dart';
import 'package:attendance_console/providers.dart';

// ── Fake repositories with realistic data ──────────────────────────────────

class _FakePersonsRepo extends PersonsRepository {
  _FakePersonsRepo() : super(ApiClient(baseUrl: ''));

  @override
  Future<List<Person>> list() async => [
        Person(id: 1, name: 'mohamad', code: '104', accessLevel: 4, createdAt: 1791858953),
        Person(id: 2, name: 'Matin', code: '', accessLevel: 2, createdAt: 1792086272),
      ];

  @override
  Future<Person> get(int id) async =>
      (await list()).firstWhere((p) => p.id == id);

  @override
  Future<Schedule?> schedule(int id) async => null;
}

class _FakeCamerasRepo extends CamerasRepository {
  _FakeCamerasRepo() : super(ApiClient(baseUrl: ''));

  @override
  Future<List<Camera>> list() async => [
        Camera(uid: '/dev/video0', name: 'Integrated Camera', connectionType: 'LOCAL', role: CameraRole.both),
        Camera(
          uid: 'rtsp___cam2', name: 'Door IPCAM', connectionType: 'RTSP',
          streamUri: 'rtsp://10.0.0.5/live', role: CameraRole.entry,
        ),
      ];
}

class _FakeZonesRepo extends ZonesRepository {
  _FakeZonesRepo() : super(ApiClient(baseUrl: ''));

  @override
  Future<List<Zone>> list() async => [
        Zone(id: 1, name: 'server', requiredAccessLevel: 5),
        Zone(id: 2, name: 'class 101', requiredAccessLevel: 1),
      ];
}

class _FakeAttendanceRepo extends AttendanceRepository {
  _FakeAttendanceRepo() : super(ApiClient(baseUrl: ''));

  @override
  Future<List<DailySummary>> summaries(String dateKey) async => [
        DailySummary(
          personId: 1, date: dateKey,
          arrival: _at(dateKey, 8, 31), departure: _at(dateKey, 17, 2),
          workSeconds: 8 * 3600 + 12 * 60, tardinessSeconds: 31 * 60,
          breaks: [BreakInterval(_at(dateKey, 12, 0), _at(dateKey, 12, 45))],
        ),
        DailySummary(personId: 2, date: dateKey, arrival: _at(dateKey, 7, 58)),
        DailySummary(personId: 3, date: dateKey, isAbsent: true),
      ];

  @override
  Future<DailySummary> summary(int personId, String dateKey) async =>
      DailySummary(
        personId: personId, date: dateKey,
        arrival: _at(dateKey, 8, 31), departure: _at(dateKey, 17, 2),
        workSeconds: 8 * 3600, tardinessSeconds: 31 * 60,
        breaks: [BreakInterval(_at(dateKey, 12, 0), _at(dateKey, 12, 45))],
      );

  @override
  Future<List<BreakRecord>> timeline(int personId, String dateKey) async => [
        BreakRecord(
          id: 1, personId: personId,
          breakStart: _at(dateKey, 12, 0), breakEnd: _at(dateKey, 12, 45),
          breakSeconds: 45 * 60,
        ),
      ];

  @override
  Future<List<TardinessRecord>> tardiness(String dateKey) async => [
        TardinessRecord(
          personId: 1, date: dateKey,
          expectedArrival: _at(dateKey, 8, 0), actualArrival: _at(dateKey, 8, 31),
          tardinessSeconds: 31 * 60,
        ),
      ];

  static int _at(String dateKey, int h, int m) {
    final d = dateFromKey(dateKey);
    return DateTime(d.year, d.month, d.day, h, m).millisecondsSinceEpoch ~/ 1000;
  }
}

class _FakeUnknownRepo extends UnknownFacesRepository {
  _FakeUnknownRepo() : super(ApiClient(baseUrl: ''));

  @override
  Future<List<UnknownFace>> list() async => [
        UnknownFace(id: 10, cameraUid: 'rtsp___cam2', timestamp: 1792800000),
      ];
}

class _FakeSystemRepo extends SystemRepository {
  _FakeSystemRepo() : super(ApiClient(baseUrl: ''));

  @override
  Future<Health> health() async => const Health(ok: true, version: '1.0.0');
}

Future<void> _pumpApp(WidgetTester tester) async {
  await tester.pumpWidget(
    ProviderScope(
      retry: (_, _) => null, // no auto-retry inside tests
      overrides: [
        personsRepoProvider.overrideWithValue(_FakePersonsRepo()),
        camerasRepoProvider.overrideWithValue(_FakeCamerasRepo()),
        zonesRepoProvider.overrideWithValue(_FakeZonesRepo()),
        attendanceRepoProvider.overrideWithValue(_FakeAttendanceRepo()),
        unknownRepoProvider.overrideWithValue(_FakeUnknownRepo()),
        systemRepoProvider.overrideWithValue(_FakeSystemRepo()),
      ],
      child: const FaceConsoleApp(),
    ),
  );
  await tester.pump(const Duration(milliseconds: 600));
}

void main() {
  testWidgets('dashboard renders KPIs, chart, board with data', (tester) async {
    tester.view.physicalSize = const Size(1500, 1000);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    await _pumpApp(tester);

    expect(find.text('mohamad'), findsWidgets);
    expect(find.text('Matin'), findsWidgets);
  }, timeout: const Timeout(Duration(minutes: 2)));

  testWidgets('people page renders list; person detail renders day card',
      (tester) async {
    tester.view.physicalSize = const Size(1500, 1000);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    await _pumpApp(tester);

    // Navigate via sidebar: People
    await tester.tap(find.byKey(const ValueKey('nav-/people')));
    await tester.pump(const Duration(milliseconds: 600));
    expect(find.descendant(
      of: find.byType(PeoplePage),
      matching: find.text('Matin'),
    ), findsOneWidget);

    // Open person detail (tap the first row inside the people page).
    await tester.tap(find.descendant(
      of: find.byType(PeoplePage),
      matching: find.text('mohamad'),
    ));
    // Multiple pumps: provider resolution needs a couple of frames.
    await tester.pump(const Duration(milliseconds: 300));
    await tester.pump(const Duration(milliseconds: 600));
    await tester.pump(const Duration(seconds: 2));

    // Detail page shows the day card title and schedule card title.
    expect(
      find.text(const S('fa').dayTimeline),
      findsOneWidget,
    );
    expect(
      find.text(const S('fa').schedule),
      findsOneWidget,
    );
  }, timeout: const Timeout(Duration(minutes: 2)));

  testWidgets('attendance page renders the daily board', (tester) async {
    tester.view.physicalSize = const Size(1500, 1000);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    await _pumpApp(tester);
    await tester.tap(find.byKey(const ValueKey('nav-/attendance')));
    await tester.pump(const Duration(milliseconds: 900));
    expect(find.text('mohamad'), findsWidgets);
  }, timeout: const Timeout(Duration(minutes: 2)));

  testWidgets('cameras, zones and unknown pages render with data',
      (tester) async {
    tester.view.physicalSize = const Size(1500, 1000);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    await _pumpApp(tester);

    await tester.tap(find.byKey(const ValueKey('nav-/cameras')));
    await tester.pump(const Duration(milliseconds: 600));
    expect(find.descendant(
      of: find.byType(CamerasPage),
      matching: find.text('Integrated Camera'),
    ), findsOneWidget);
    expect(find.descendant(
      of: find.byType(CamerasPage),
      matching: find.text('Door IPCAM'),
    ), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('nav-/zones')));
    await tester.pump(const Duration(milliseconds: 600));
    expect(find.descendant(
      of: find.byType(ZonesPage),
      matching: find.text('server'),
    ), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('nav-/unknown')));
    await tester.pump(const Duration(milliseconds: 600));
    // The unknown faces page is now the active branch.
    expect(find.byType(UnknownFacesPage), findsOneWidget);
  }, timeout: const Timeout(Duration(minutes: 2)));

  testWidgets('live page renders tiles for cameras', (tester) async {
    tester.view.physicalSize = const Size(1500, 1000);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    await _pumpApp(tester);
    await tester.tap(find.byKey(const ValueKey('nav-/live')));
    await tester.pump(const Duration(milliseconds: 600));
    expect(find.text('Integrated Camera'), findsOneWidget);

    // Dispose the tree cleanly so poller timers flush before test end.
    await tester.pumpWidget(const SizedBox.shrink());
    await tester.pump(const Duration(seconds: 5));
  }, timeout: const Timeout(Duration(minutes: 2)));

  testWidgets('reports and settings render', (tester) async {
    tester.view.physicalSize = const Size(1500, 1000);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    await _pumpApp(tester);

    await tester.tap(find.byKey(const ValueKey('nav-/reports')));
    await tester.pump(const Duration(milliseconds: 600));
    expect(find.byIcon(Icons.insert_chart_outlined_rounded), findsWidgets);

    await tester.tap(find.byKey(const ValueKey('nav-/settings')));
    await tester.pump(const Duration(milliseconds: 600));
    expect(find.byType(SegmentedButton<String>), findsOneWidget);
  }, timeout: const Timeout(Duration(minutes: 2)));
}
