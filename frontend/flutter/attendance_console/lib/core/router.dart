import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import '../core/app_theme.dart';
import '../features/attendance/attendance_page.dart';
import '../features/cameras/cameras_page.dart';
import '../features/dashboard/dashboard_page.dart';
import '../features/live/live_page.dart';
import '../features/people/people_page.dart';
import '../features/people/person_detail_page.dart';
import '../features/reports/reports_page.dart';
import '../features/settings/settings_page.dart';
import '../features/shell/shell.dart';
import '../features/unknown/unknown_page.dart';
import '../features/zones/zones_page.dart';
import '../providers.dart';

/// App-wide router: URL-driven navigation with a persistent shell
/// (sidebar) around every page. Each branch keeps its own state.
final routerProvider = Provider<GoRouter>((ref) {
  // Watch locale so the not-found page relocalizes.
  ref.watch(settingsProvider.select((s) => s.localeCode));

  return GoRouter(
    initialLocation: '/',
    routes: [
      StatefulShellRoute.indexedStack(
        builder: (context, state, shell) => AppShell(shell: shell),
        branches: [
          StatefulShellBranch(routes: [
            GoRoute(
              path: '/',
              name: 'dashboard',
              builder: (_, _) => const DashboardPage(),
            ),
          ]),
          StatefulShellBranch(routes: [
            GoRoute(
              path: '/live',
              name: 'live',
              builder: (_, _) => const LivePage(),
            ),
          ]),
          StatefulShellBranch(routes: [
            GoRoute(
              path: '/people',
              name: 'people',
              builder: (_, _) => const PeoplePage(),
              routes: [
                GoRoute(
                  path: ':id',
                  name: 'person',
                  // Soft fade-through between the list and the profile.
                  pageBuilder: (_, state) => CustomTransitionPage(
                    key: state.pageKey,
                    transitionDuration: Motion.slow,
                    reverseTransitionDuration: Motion.fast,
                    transitionsBuilder: (_, animation, _, child) => FadeTransition(
                      opacity: animation.drive(CurveTween(curve: Motion.entry)),
                      child: child,
                    ),
                    child: PersonDetailPage(
                      personId: int.tryParse(state.pathParameters['id'] ?? '') ?? 0,
                    ),
                  ),
                ),
              ],
            ),
          ]),
          StatefulShellBranch(routes: [
            GoRoute(
              path: '/attendance',
              name: 'attendance',
              builder: (_, _) => const AttendancePage(),
            ),
          ]),
          StatefulShellBranch(routes: [
            GoRoute(
              path: '/cameras',
              name: 'cameras',
              builder: (_, _) => const CamerasPage(),
            ),
          ]),
          StatefulShellBranch(routes: [
            GoRoute(
              path: '/zones',
              name: 'zones',
              builder: (_, _) => const ZonesPage(),
            ),
          ]),
          StatefulShellBranch(routes: [
            GoRoute(
              path: '/unknown',
              name: 'unknown',
              builder: (_, _) => const UnknownFacesPage(),
            ),
          ]),
          StatefulShellBranch(routes: [
            GoRoute(
              path: '/reports',
              name: 'reports',
              builder: (_, _) => const ReportsPage(),
            ),
          ]),
          StatefulShellBranch(routes: [
            GoRoute(
              path: '/settings',
              name: 'settings',
              builder: (_, _) => const SettingsPage(),
            ),
          ]),
        ],
      ),
    ],
  );
});
