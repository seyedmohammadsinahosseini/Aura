import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:attendance_console/app.dart';
import 'package:attendance_console/data/api_client.dart';
import 'package:attendance_console/providers.dart';

/// Smoke tests: pump the real app (all pages through the router shell).
/// The API client is pointed at a dead port so data providers fail
/// gracefully instead of hitting a real server.
Future<void> _pumpApp(WidgetTester tester) async {
  await tester.pumpWidget(
    ProviderScope(
      // No automatic retry inside tests.
      retry: (_, _) => null,
      overrides: [
        apiClientProvider.overrideWith(
          (ref) => ApiClient(baseUrl: 'http://127.0.0.1:9'),
        ),
      ],
      child: const FaceConsoleApp(),
    ),
  );
}

void main() {
  testWidgets('app boots and renders the dashboard shell', (tester) async {
    await _pumpApp(tester);

    // Let failed providers settle into error states without retry loops.
    await tester.pump(const Duration(seconds: 1));

    expect(find.byType(MaterialApp), findsOneWidget);
    // Sidebar rendered with its navigation buttons.
    expect(find.byIcon(Icons.space_dashboard_outlined), findsOneWidget);
    expect(find.byIcon(Icons.settings_outlined), findsOneWidget);
  }, timeout: const Timeout(Duration(minutes: 2)));

  testWidgets('rail mode renders (narrow window)', (tester) async {
    tester.view.physicalSize = const Size(800, 900);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    await _pumpApp(tester);
    await tester.pump(const Duration(seconds: 1));

    expect(find.byIcon(Icons.space_dashboard_outlined), findsOneWidget);
  }, timeout: const Timeout(Duration(minutes: 2)));

  testWidgets('drawer mode renders (mobile window)', (tester) async {
    tester.view.physicalSize = const Size(500, 900);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    await _pumpApp(tester);
    await tester.pump(const Duration(seconds: 1));

    // Body still renders; drawer is available but closed.
    expect(find.byType(MaterialApp), findsOneWidget);
  }, timeout: const Timeout(Duration(minutes: 2)));
}
