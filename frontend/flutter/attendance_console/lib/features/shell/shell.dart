import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/app_strings.dart';
import '../../core/app_theme.dart';
import '../../data/models.dart';
import '../../providers.dart';

/// App shell: adaptive sidebar (expanded / rail / drawer) + content.
///
/// - ≥ 1080 px: fixed 256 px sidebar (the width used by Linear/Stripe/Vercel)
/// - 720–1080 px: 72 px icon rail
/// - < 720 px: hamburger drawer
class AppShell extends ConsumerStatefulWidget {
  const AppShell({super.key, required this.shell});

  final StatefulNavigationShell shell;

  @override
  ConsumerState<AppShell> createState() => _AppShellState();
}

class _AppShellState extends ConsumerState<AppShell> {
  Timer? _healthTimer;

  @override
  void initState() {
    super.initState();
    // Poll backend liveness for the sidebar status dot (and auto-refresh via
    // Riverpod's dependency graph when it recovers).
    _healthTimer = Timer.periodic(const Duration(seconds: 30), (_) {
      if (mounted) ref.invalidate(healthProvider);
    });
  }

  @override
  void dispose() {
    _healthTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.sizeOf(context).width;

    if (width < 720) {
      return Scaffold(
        drawer: _DrawerSidebar(shell: widget.shell),
        body: widget.shell,
      );
    }

    final rail = width < 1080;
    return Scaffold(
      body: Row(
        children: [
          _Sidebar(shell: widget.shell, rail: rail),
          VerticalDivider(
            width: 1,
            thickness: 1,
            color: Theme.of(context).colorScheme.outlineVariant,
          ),
          Expanded(child: widget.shell),
        ],
      ),
    );
  }
}

abstract class _SidebarItems {
  static IconData icon(int i) => icons[i] ?? Icons.circle_outlined;

  static const icons = <int, IconData>{
    0: Icons.space_dashboard_outlined,
    1: Icons.sensors,
    2: Icons.people_outline_rounded,
    3: Icons.event_available_outlined,
    4: Icons.linked_camera_outlined,
    5: Icons.map_outlined,
    6: Icons.person_search_rounded,
    7: Icons.insert_chart_outlined_rounded,
    8: Icons.settings_outlined,
  };
}

// ── Sidebar ───────────────────────────────────────────────────────────────

class _Sidebar extends ConsumerWidget {
  const _Sidebar({required this.shell, required this.rail});

  final StatefulNavigationShell shell;
  final bool rail;

  static const expandedWidth = 256.0;
  static const railWidth = 72.0;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;

    final items = <_NavItem>[
      _NavItem('/dashboard', 0, s.navDashboard, _SidebarItems.icon(0)),
      _NavItem('/live', 1, s.navLive, _SidebarItems.icon(1)),
      _NavItem('/people', 2, s.navPeople, _SidebarItems.icon(2)),
      _NavItem('/attendance', 3, s.navAttendance, _SidebarItems.icon(3)),
      _NavItem('/cameras', 4, s.navCameras, _SidebarItems.icon(4)),
      _NavItem('/zones', 5, s.navZones, _SidebarItems.icon(5)),
      _NavItem('/unknown', 6, s.navUnknown, _SidebarItems.icon(6)),
      _NavItem('/reports', 7, s.navReports, _SidebarItems.icon(7)),
    ];

    return Material(
      color: scheme.surfaceContainerLowest,
      child: SizedBox(
        width: rail ? railWidth : expandedWidth,
        child: Column(
          children: [
            const SizedBox(height: 18),
            _Logo(rail: rail),
            const SizedBox(height: 18),
            Expanded(
              child: ListView(
                padding: const EdgeInsets.symmetric(horizontal: 12),
                children: [
                  for (final item in items)
                    _SidebarButton(
                      item: item,
                      selected: shell.currentIndex == item.index,
                      rail: rail,
                      onTap: () => shell.goBranch(item.index, initialLocation: shell.currentIndex == item.index),
                    ),
                ],
              ),
            ),
            Divider(color: scheme.outlineVariant, height: 1),
            _SidebarButton(
              item: _NavItem('/settings', 8, s.navSettings, _SidebarItems.icon(8)),
              selected: shell.currentIndex == 8,
              rail: rail,
              onTap: () => shell.goBranch(8),
            ),
            const SizedBox(height: 8),
            _FooterControls(rail: rail),
            const SizedBox(height: 14),
          ],
        ),
      ),
    );
  }
}

class _NavItem {
  const _NavItem(this.path, this.index, this.label, this.icon);

  final String path;
  final int index;
  final String label;
  final IconData icon;
}

class _SidebarButton extends ConsumerWidget {
  const _SidebarButton({
    required this.item,
    required this.selected,
    required this.rail,
    required this.onTap,
  });

  final _NavItem item;
  final bool selected;
  final bool rail;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final scheme = Theme.of(context).colorScheme;

    Widget? badge;
    if (item.path == '/unknown') {
      final faces = ref.watch(unknownFacesProvider).value ?? const <dynamic>[];
      if (faces.isNotEmpty) {
        badge = Container(
          padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 2),
          decoration: BoxDecoration(
            color: scheme.primary,
            borderRadius: BorderRadius.circular(999),
          ),
          child: Text(
            S.of(context).isFa
                ? faDigitsStatic(faces.length)
                : '${faces.length}',
            style: TextStyle(
              fontSize: 11,
              fontWeight: FontWeight.w700,
              color: scheme.onPrimary,
            ),
          ),
        );
      }
    }

    final button = Padding(
      padding: const EdgeInsets.only(bottom: 2),
      child: Material(
        key: ValueKey('nav-${item.path}'),
        borderRadius: BorderRadius.circular(10),
        color: Colors.transparent,
        child: AnimatedContainer(
          // Selection pill melts in/out instead of snapping.
          duration: Motion.fast,
          curve: Motion.entry,
          decoration: BoxDecoration(
            color: selected ? scheme.primaryContainer : Colors.transparent,
            borderRadius: BorderRadius.circular(10),
          ),
          child: InkWell(
            borderRadius: BorderRadius.circular(10),
            onTap: onTap,
            child: Tooltip(
              message: rail ? item.label : '',
              child: Padding(
                padding: EdgeInsets.symmetric(
                  horizontal: rail ? 0 : 12,
                  vertical: 10,
                ),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    TweenAnimationBuilder<Color?>(
                      // Icon color eases between states.
                      tween: ColorTween(
                        begin: selected ? scheme.primary : scheme.onSurfaceVariant,
                        end: selected ? scheme.primary : scheme.onSurfaceVariant,
                      ),
                      duration: Motion.fast,
                      curve: Motion.entry,
                      builder: (context, color, child) =>
                          Icon(item.icon, size: 21, color: color),
                    ),
                    if (!rail) ...[
                      const SizedBox(width: 12),
                      Expanded(
                        child: AnimatedDefaultTextStyle(
                          duration: Motion.fast,
                          curve: Motion.entry,
                          style: TextStyle(
                            fontSize: 13.5,
                            fontWeight: selected ? FontWeight.w600 : FontWeight.w500,
                            color: selected ? scheme.onPrimaryContainer : scheme.onSurfaceVariant,
                          ),
                          child: Text(item.label, overflow: TextOverflow.ellipsis),
                        ),
                      ),
                      AnimatedSwitcher(
                        // Badge pops softly (and unpops) instead of appearing.
                        duration: Motion.fast,
                        switchInCurve: Curves.easeOutBack,
                        switchOutCurve: Curves.easeInCubic,
                        transitionBuilder: (child, animation) =>
                            ScaleTransition(scale: animation, child: child),
                        child: badge != null
                            ? Padding(
                                key: const ValueKey('badge'),
                                padding: const EdgeInsetsDirectional.only(start: 8),
                                child: badge,
                              )
                            : const SizedBox(width: 8, key: ValueKey('no-badge')),
                      ),
                    ],
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );

    if (rail) {
      return Center(child: SizedBox(width: 44, height: 44, child: button));
    }
    return button;
  }
}

String faDigitsStatic(int n) {
  const d = ['۰', '۱', '۲', '۳', '۴', '۵', '۶', '۷', '۸', '۹'];
  return n.toString().split('').map((c) => d[int.tryParse(c) ?? 0]).join();
}

class _Logo extends StatelessWidget {
  const _Logo({required this.rail});

  final bool rail;

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;
    return Row(
      mainAxisAlignment: rail ? MainAxisAlignment.center : MainAxisAlignment.start,
      children: [
        Container(
          width: 34,
          height: 34,
          margin: rail ? EdgeInsets.zero : const EdgeInsetsDirectional.only(start: 12),
          decoration: BoxDecoration(
            color: scheme.primary,
            borderRadius: BorderRadius.circular(10),
          ),
          child: const Icon(Icons.center_focus_strong_rounded, size: 20, color: Colors.white),
        ),
        if (!rail) ...[
          const SizedBox(width: 10),
          Flexible(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  s.appName,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w700),
                ),
                Text(
                  s.console,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(fontSize: 11, color: scheme.onSurfaceVariant),
                ),
              ],
            ),
          ),
        ],
      ],
    );
  }
}

class _FooterControls extends ConsumerWidget {
  const _FooterControls({required this.rail});

  final bool rail;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;
    final health = ref.watch(healthProvider);

    final online = health.value?.ok ?? false;

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      child: rail
          ? Column(
              children: [
                _StatusDot(online: online, label: online ? s.connected : s.notConnected),
                const SizedBox(height: 4),
                _ThemeButton(rail: true),
                _LangButton(rail: true),
              ],
            )
          : Row(
              children: [
                Expanded(
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      _StatusDot(online: online, label: online ? s.connected : s.notConnected),
                      const SizedBox(width: 8),
                      Flexible(
                        child: Text(
                          online
                              ? (health.value?.version.isNotEmpty == true
                                  ? '${s.backendVersion} ${health.value!.version}'
                                  : s.connected)
                              : s.notConnected,
                          overflow: TextOverflow.ellipsis,
                          style: TextStyle(fontSize: 11.5, color: scheme.onSurfaceVariant),
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: 8),
                _ThemeButton(rail: false),
                _LangButton(rail: false),
              ],
            ),
    );
  }
}

class _StatusDot extends StatelessWidget {
  const _StatusDot({required this.online, required this.label});

  final bool online;
  final String label;

  @override
  Widget build(BuildContext context) {
    final sem = SemanticColors.of(context);
    return Tooltip(
      message: label,
      child: Container(
        width: 9,
        height: 9,
        decoration: BoxDecoration(
          shape: BoxShape.circle,
          color: online ? sem.success : Theme.of(context).colorScheme.error,
        ),
      ),
    );
  }
}

class _ThemeButton extends ConsumerWidget {
  const _ThemeButton({required this.rail});

  final bool rail;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final settings = ref.watch(settingsProvider);
    final controller = ref.read(settingsProvider.notifier);

    final icon = switch (settings.themeMode) {
      ThemeModeOption.system => Icons.brightness_auto_outlined,
      ThemeModeOption.light => Icons.light_mode_outlined,
      ThemeModeOption.dark => Icons.dark_mode_outlined,
    };

    return PopupMenuButton<ThemeModeOption>(
      tooltip: s.themeMode,
      icon: Icon(icon, size: 19, color: Theme.of(context).colorScheme.onSurfaceVariant),
      padding: EdgeInsets.zero,
      onSelected: controller.setThemeMode,
      itemBuilder: (context) => [
        PopupMenuItem(
          value: ThemeModeOption.system,
          child: _menuRow(context, Icons.brightness_auto_outlined, s.tmSystem, settings.themeMode == ThemeModeOption.system),
        ),
        PopupMenuItem(
          value: ThemeModeOption.light,
          child: _menuRow(context, Icons.light_mode_outlined, s.tmLight, settings.themeMode == ThemeModeOption.light),
        ),
        PopupMenuItem(
          value: ThemeModeOption.dark,
          child: _menuRow(context, Icons.dark_mode_outlined, s.tmDark, settings.themeMode == ThemeModeOption.dark),
        ),
      ],
    );
  }

  Widget _menuRow(BuildContext context, IconData icon, String label, bool checked) {
    final scheme = Theme.of(context).colorScheme;
    return Row(
      children: [
        Icon(icon, size: 17, color: checked ? scheme.primary : scheme.onSurfaceVariant),
        const SizedBox(width: 10),
        Text(label, style: TextStyle(fontSize: 13)),
      ],
    );
  }
}

class _LangButton extends ConsumerWidget {
  const _LangButton({required this.rail});

  final bool rail;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final controller = ref.read(settingsProvider.notifier);
    final next = s.isFa ? 'EN' : 'فا';
    return Tooltip(
      message: s.language,
      child: IconButton(
        visualDensity: VisualDensity.compact,
        onPressed: () => controller.setLocale(s.isFa ? 'en' : 'fa'),
        icon: Text(
          next,
          style: TextStyle(
            fontSize: 12.5,
            fontWeight: FontWeight.w700,
            color: Theme.of(context).colorScheme.onSurfaceVariant,
          ),
        ),
      ),
    );
  }
}

// ── Drawer (narrow screens) ───────────────────────────────────────────────

class _DrawerSidebar extends ConsumerWidget {
  const _DrawerSidebar({required this.shell});

  final StatefulNavigationShell shell;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // Reuse the expanded sidebar inside the drawer.
    return Drawer(
      backgroundColor: Theme.of(context).colorScheme.surfaceContainerLowest,
      width: _Sidebar.expandedWidth,
      child: _Sidebar(shell: shell, rail: false),
    );
  }
}
