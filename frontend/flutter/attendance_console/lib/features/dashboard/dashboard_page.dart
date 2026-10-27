import 'dart:async';

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/app_strings.dart';
import '../../core/app_theme.dart';
import '../../core/formatters.dart';
import '../../data/models.dart';
import '../../providers.dart';
import '../../shared/widgets.dart';

/// Main dashboard: answers "are we on track today?" in one glance.
///
/// - 4 KPI tiles (present / late / absent / unknown pending)
/// - 7-day attendance chart
/// - camera status list
/// - today's attendance table (top rows)
class DashboardPage extends ConsumerStatefulWidget {
  const DashboardPage({super.key});

  @override
  ConsumerState<DashboardPage> createState() => _DashboardPageState();
}

class _DashboardPageState extends ConsumerState<DashboardPage> {
  DateTime _lastRefresh = DateTime.now();
  Timer? _timer;

  @override
  void initState() {
    super.initState();
    _timer = Timer.periodic(const Duration(seconds: 30), (_) => _refresh());
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  void _refresh() {
    if (!mounted) return;
    final key = todayKey();
    ref.invalidate(summariesProvider(key));
    ref.invalidate(unknownFacesProvider);
    ref.invalidate(personsProvider);
    ref.invalidate(camerasProvider);
    setState(() => _lastRefresh = DateTime.now());
  }

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final fa = s.isFa;

    final persons = ref.watch(personsProvider);
    final summaries = ref.watch(summariesProvider(todayKey()));
    final unknowns = ref.watch(unknownFacesProvider).value ?? [];
    final cameras = ref.watch(camerasProvider).value ?? [];

    final pList = persons.value ?? const <Person>[];
    final sList = summaries.value ?? const <DailySummary>[];
    final isToday = true;

    var present = 0, late = 0, absent = 0;
    for (final sum in sList) {
      switch (summaryStatus(sum, isToday: isToday)) {
        case AttStatus.present:
          present++;
        case AttStatus.late:
          late++;
        case AttStatus.absent:
          absent++;
        case AttStatus.notArrived:
        case AttStatus.notRecorded:
          break;
      }
    }

    final todayBoard = _TodayBoard(
      persons: pList,
      summaries: sList,
      fa: fa,
      s: s,
    );

    return PageBody(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          PageHeader(
            title: s.navDashboard,
            subtitle: '${longDate(DateTime.now(), fa: fa)} · ${s.lastUpdate} ${clock(_lastRefresh, fa: fa)}',
            actions: [
              IconButton(
                tooltip: s.refresh,
                icon: const Icon(Icons.refresh_rounded),
                onPressed: _refresh,
              ),
            ],
          ),

          // ── KPI row ──────────────────────────────────────────────
          LayoutBuilder(builder: (context, constraints) {
            final wide = constraints.maxWidth > 1000;
            final cards = [
              (Icons.check_circle_outline_rounded, present, s.dashPresentToday,
                  StatTone.success, '/attendance'),
              (Icons.schedule_rounded, late, s.dashLateToday,
                  StatTone.warning, '/attendance'),
              (Icons.person_off_outlined, absent, s.dashAbsentToday,
                  StatTone.error, '/attendance'),
              (Icons.person_search_rounded, unknowns.length, s.dashPendingUnknown,
                  StatTone.primary, '/unknown'),
            ];
            final tiles = cards
                .map((c) => StatCard(
                      icon: c.$1,
                      value: fa ? faDigits(c.$2) : '${c.$2}',
                      label: c.$3,
                      tone: c.$4,
                      onTap: () => context.go(c.$5),
                    ))
                .toList();
            return wide
                ? Row(
                    children: [
                      for (var i = 0; i < tiles.length; i++) ...[
                        Expanded(child: tiles[i]),
                        if (i < tiles.length - 1) const SizedBox(width: 14),
                      ],
                    ],
                  )
                : Wrap(spacing: 14, runSpacing: 14, children: [
                    for (final t in tiles)
                      SizedBox(width: (constraints.maxWidth - 14) / 2, child: t),
                  ]);
          }),
          const SizedBox(height: 16),

          // ── Weekly chart + cameras ───────────────────────────────
          LayoutBuilder(builder: (context, constraints) {
            final wide = constraints.maxWidth > 1000;
            final chart = _WeeklyChart(fa: fa);
            final camCard = _CamerasCard(cameras: cameras);
            if (wide) {
              return Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(flex: 3, child: chart),
                      const SizedBox(width: 14),
                      Expanded(flex: 2, child: camCard),
                    ],
                  ),
                  const SizedBox(height: 16),
                  todayBoard,
                ],
              );
            }
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                chart,
                const SizedBox(height: 14),
                camCard,
                const SizedBox(height: 16),
                todayBoard,
              ],
            );
          }),
        ],
      ),
    );
  }
}

// ── Weekly chart ───────────────────────────────────────────────────────────

class _WeeklyChart extends ConsumerWidget {
  const _WeeklyChart({required this.fa});

  final bool fa;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;
    final sem = SemanticColors.of(context);

    final days = lastDateKeys(7);
    final asyncs = [for (final k in days) ref.watch(summariesProvider(k))];

    if (asyncs.any((a) => a is! AsyncData)) {
      return TitledPanel(
        title: s.dashWeekly,
        child: const SizedBox(height: 220, child: Center(child: SizedBox(width: 24, height: 24, child: CircularProgressIndicator(strokeWidth: 2.4)))),
      );
    }

    var maxPresent = 1;
    final groups = <BarChartGroupData>[];
    for (var i = 0; i < days.length; i++) {
      final list = (asyncs[i].value as List<DailySummary>);
      var present = 0, late = 0;
      for (final sum in list) {
        switch (summaryStatus(sum, isToday: dateKey(DateTime.now()) == days[i])) {
          case AttStatus.present:
            present++;
          case AttStatus.late:
            late++;
          case AttStatus.absent:
          case AttStatus.notArrived:
          case AttStatus.notRecorded:
            break;
        }
      }
      if (present > maxPresent) maxPresent = present;
      groups.add(BarChartGroupData(
        x: i,
        barsSpace: 4,
        barRods: [
          BarChartRodData(
            toY: present.toDouble(),
            width: 10,
            borderRadius: const BorderRadius.vertical(top: Radius.circular(4)),
            color: scheme.primary,
          ),
          BarChartRodData(
            toY: late.toDouble(),
            width: 10,
            borderRadius: const BorderRadius.vertical(top: Radius.circular(4)),
            color: sem.warning,
          ),
        ],
      ));
    }

    return TitledPanel(
      title: s.dashWeekly,
      trailing: Row(
        children: [
          _legend(scheme.primary, s.stPresent),
          const SizedBox(width: 10),
          _legend(sem.warning, s.stLate),
        ],
      ),
      child: SizedBox(
        height: 220,
        child: BarChart(
          duration: Motion.chart,
          curve: Motion.entry,
          BarChartData(
            alignment: BarChartAlignment.spaceBetween,
            gridData: FlGridData(
              show: true,
              drawVerticalLine: false,
              horizontalInterval: (maxPresent / 4).ceilToDouble().clamp(1, 999),
              getDrawingHorizontalLine: (v) =>
                  FlLine(color: scheme.outlineVariant, strokeWidth: 1),
            ),
            borderData: FlBorderData(show: false),
            titlesData: FlTitlesData(
              topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
              rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
              leftTitles: AxisTitles(
                sideTitles: SideTitles(
                  showTitles: true,
                  reservedSize: 30,
                  getTitlesWidget: (v, meta) => SideTitleWidget(
                    meta: meta,
                    child: Text(
                      fa ? faDigits(v.toInt()) : '${v.toInt()}',
                      style: TextStyle(fontSize: 11, color: scheme.onSurfaceVariant),
                    ),
                  ),
                ),
              ),
              bottomTitles: AxisTitles(
                sideTitles: SideTitles(
                  showTitles: true,
                  reservedSize: 26,
                  getTitlesWidget: (v, meta) {
                    final i = v.toInt();
                    if (i < 0 || i >= days.length) return const SizedBox.shrink();
                    final d = dateFromKey(days[i]);
                    return SideTitleWidget(
                      meta: meta,
                      child: Text(
                        weekdayShort(d, fa: fa),
                        style: TextStyle(fontSize: 11, color: scheme.onSurfaceVariant),
                      ),
                    );
                  },
                ),
              ),
            ),
            barGroups: groups,
            groupsSpace: 12,
          ),
        ),
      ),
    );
  }

  Widget _legend(Color c, String label) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(width: 9, height: 9, decoration: BoxDecoration(color: c, borderRadius: BorderRadius.circular(3))),
        const SizedBox(width: 6),
        Text(label, style: const TextStyle(fontSize: 12)),
      ],
    );
  }
}

// ── Cameras card ───────────────────────────────────────────────────────────

class _CamerasCard extends ConsumerWidget {
  const _CamerasCard({required this.cameras});

  final List<Camera> cameras;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;

    return TitledPanel(
      title: s.dashCameras,
      trailing: TextButton.icon(
        onPressed: () => context.go('/live'),
        icon: const Icon(Icons.chevron_right_rounded, size: 18),
        label: Text(s.navLive, style: const TextStyle(fontSize: 12.5)),
      ),
      child: cameras.isEmpty
          ? Padding(
              padding: const EdgeInsets.symmetric(vertical: 24),
              child: Column(
                children: [
                  Icon(Icons.videocam_off_outlined, size: 30, color: scheme.outline),
                  const SizedBox(height: 10),
                  TextButton(onPressed: () => context.go('/cameras'), child: Text(s.addCamera)),
                ],
              ),
            )
          : Column(
              children: [
                for (final c in cameras.take(6))
                  Padding(
                    padding: const EdgeInsets.only(bottom: 10),
                    child: Row(
                      children: [
                        Icon(Icons.linked_camera_outlined,
                            size: 18, color: scheme.onSurfaceVariant),
                        const SizedBox(width: 10),
                        Expanded(child: Text(c.name, overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 13.5))),
                        if (!c.streamEnabled)
                          StatusChip(label: s.liveDisabled, tone: ChipTone.neutral)
                        else if (!c.aiEnabled)
                          StatusChip(label: s.camPreviewOnly, tone: ChipTone.neutral, icon: Icons.videocam)
                        else
                          StatusChip(label: s.active, tone: ChipTone.success, icon: Icons.bolt_rounded),
                      ],
                    ),
                  ),
              ],
            ),
    );
  }
}

// ── Today's board (compact) ───────────────────────────────────────────────

class _TodayBoard extends ConsumerWidget {
  const _TodayBoard({
    required this.persons,
    required this.summaries,
    required this.fa,
    required this.s,
  });

  final List<Person> persons;
  final List<DailySummary> summaries;
  final bool fa;
  final S s;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final scheme = Theme.of(context).colorScheme;

    final rows = <(Person, DailySummary, AttStatus)>[];
    for (final sum in summaries) {
      final person = persons.where((p) => p.id == sum.personId).firstOrNull;
      if (person == null) continue;
      final st = summaryStatus(sum, isToday: true);
      if (st == AttStatus.notArrived || st == AttStatus.notRecorded) continue;
      rows.add((person, sum, st));
    }
    rows.sort((a, b) => b.$2.arrival.compareTo(a.$2.arrival));
    final shown = rows.take(8).toList();

    return TitledPanel(
      title: s.dashTodayBoard,
      trailing: TextButton(
        onPressed: () => context.go('/attendance'),
        child: Text(s.viewAll, style: const TextStyle(fontSize: 12.5)),
      ),
      child: shown.isEmpty
          ? Padding(
              padding: const EdgeInsets.symmetric(vertical: 28),
              child: Text(
                s.attendanceEmpty,
                textAlign: TextAlign.center,
                style: TextStyle(fontSize: 13, color: scheme.onSurfaceVariant),
              ),
            )
          : Column(
              children: [
                for (final (person, sum, st) in shown)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 10),
                    child: Row(
                      children: [
                        PersonAvatar(personId: person.id, name: person.name, size: 34),
                        const SizedBox(width: 12),
                        Expanded(
                          child: Text(person.name,
                              overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 13.5)),
                        ),
                        Text(unixClock(sum.arrival, fa: fa),
                            style: TextStyle(fontSize: 13, color: scheme.onSurfaceVariant)),
                        const SizedBox(width: 12),
                        switch (st) {
                          AttStatus.present => StatusChip(label: s.stOnTime, tone: ChipTone.success),
                          AttStatus.late => StatusChip(label: s.stLate, tone: ChipTone.warning),
                          AttStatus.absent => StatusChip(label: s.stAbsent, tone: ChipTone.error),
                          _ => const SizedBox.shrink(),
                        },
                      ],
                    ),
                  ),
              ],
            ),
    );
  }
}
