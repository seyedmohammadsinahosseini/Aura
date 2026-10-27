import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/app_strings.dart';
import '../../core/app_theme.dart';
import '../../core/formatters.dart';
import '../../data/models.dart';
import '../../providers.dart';
import '../../shared/widgets.dart';

/// Reports: per-person weekly hours + team tardiness for the same week.
class ReportsPage extends ConsumerStatefulWidget {
  const ReportsPage({super.key});

  @override
  ConsumerState<ReportsPage> createState() => _ReportsPageState();
}

class _ReportsPageState extends ConsumerState<ReportsPage> {
  int? _personId;

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final fa = s.isFa;
    final persons = ref.watch(personsProvider);

    return PageBody(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          PageHeader(title: s.reportsTitle),
          LayoutBuilder(builder: (context, constraints) {
            final wide = constraints.maxWidth > 1000;
            final personReport = _PersonWeekReport(
              persons: persons.value ?? const <Person>[],
              personId: _personId,
              fa: fa,
              s: s,
              onPick: (id) => setState(() => _personId = id),
            );
            final teamReport = _TardinessReport(fa: fa, s: s);
            return wide
                ? Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(flex: 3, child: personReport),
                      const SizedBox(width: 14),
                      Expanded(flex: 2, child: teamReport),
                    ],
                  )
                : Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      personReport,
                      const SizedBox(height: 14),
                      teamReport,
                    ],
                  );
          }),
        ],
      ),
    );
  }
}

// ── Person weekly hours ────────────────────────────────────────────────────

class _PersonWeekReport extends ConsumerWidget {
  const _PersonWeekReport({
    required this.persons,
    required this.personId,
    required this.fa,
    required this.s,
    required this.onPick,
  });

  final List<Person> persons;
  final int? personId;
  final bool fa;
  final S s;
  final ValueChanged<int> onPick;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final scheme = Theme.of(context).colorScheme;
    final person = persons.where((p) => p.id == personId).firstOrNull;

    return TitledPanel(
      title: s.weeklyHours,
      child: persons.isEmpty
          ? Padding(
              padding: const EdgeInsets.symmetric(vertical: 24),
              child: Center(
                child: Text(s.peopleEmpty,
                    style: TextStyle(color: scheme.onSurfaceVariant)),
              ),
            )
          : Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                DropdownMenu<int>(
                  width: double.infinity,
                  initialSelection: personId,
                  hintText: s.selectPerson,
                  onSelected: (v) => onPick(v ?? 0),
                  dropdownMenuEntries: [
                    for (final p in persons)
                      DropdownMenuEntry(
                        value: p.id,
                        label: p.name,
                      ),
                  ],
                ),
                if (person != null) ...[
                  const SizedBox(height: 18),
                  _WeekChart(personId: person.id, fa: fa),
                ],
              ],
            ),
    );
  }
}

class _WeekChart extends ConsumerWidget {
  const _WeekChart({required this.personId, required this.fa});

  final int personId;
  final bool fa;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;

    final days = lastDateKeys(7);
    final daysData = [
      for (final k in days)
        ref.watch(personDayProvider((personId: personId, dateKey: k))),
    ];

    if (daysData.every((d) => d is AsyncData)) {
      final values = daysData
          .map((d) => (d as AsyncData<PersonDay>).value)
          .toList();

      var totalWork = 0, totalLate = 0, present = 0;
      final bars = <BarChartRodData>[];
      for (var i = 0; i < values.length; i++) {
        final v = values[i];
        final hours = (v.summary.workSeconds / 3600);
        if (v.summary.arrival > 0) present++;
        totalWork += v.summary.workSeconds;
        totalLate += v.summary.tardinessSeconds;
        bars.add(BarChartRodData(
          toY: hours,
          width: 16,
          borderRadius: const BorderRadius.vertical(top: Radius.circular(5)),
          color: scheme.primary,
        ));
      }
      final avg = present == 0 ? 0.0 : totalWork / present / 3600;

      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SizedBox(
            height: 200,
            child: BarChart(
              duration: Motion.chart,
              curve: Motion.entry,
              BarChartData(
                alignment: BarChartAlignment.spaceBetween,
                gridData: FlGridData(
                  show: true,
                  drawVerticalLine: false,
                  horizontalInterval: 2,
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
                      reservedSize: 24,
                      getTitlesWidget: (v, meta) {
                        final i = v.toInt();
                        if (i < 0 || i >= days.length) return const SizedBox.shrink();
                        return SideTitleWidget(
                          meta: meta,
                          child: Text(
                            weekdayShort(dateFromKey(days[i]), fa: fa),
                            style: TextStyle(fontSize: 11, color: scheme.onSurfaceVariant),
                          ),
                        );
                      },
                    ),
                  ),
                ),
                barGroups: [
                  for (var i = 0; i < bars.length; i++)
                    BarChartGroupData(x: i, barRods: [bars[i]]),
                ],
                maxY: 12,
              ),
            ),
          ),
          const SizedBox(height: 16),
          Row(
            children: [
              _sum(context, s.totalWorked, longDuration(totalWork, fa: fa)),
              const SizedBox(width: 18),
              _sum(context, s.dailyAvg, longDuration((avg * 3600).round(), fa: fa)),
              const SizedBox(width: 18),
              _sum(context, s.totalLate, longDuration(totalLate, fa: fa)),
            ],
          ),
        ],
      );
    }

    return const SizedBox(
      height: 200,
      child: Center(child: SizedBox(width: 24, height: 24, child: CircularProgressIndicator(strokeWidth: 2.4))),
    );
  }

  Widget _sum(BuildContext context, String label, String value) {
    final scheme = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label, style: TextStyle(fontSize: 11.5, color: scheme.onSurfaceVariant)),
        const SizedBox(height: 3),
        Text(value, style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
      ],
    );
  }
}

// ── Team tardiness this week ───────────────────────────────────────────────

class _TardinessReport extends ConsumerWidget {
  const _TardinessReport({required this.fa, required this.s});

  final bool fa;
  final S s;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final scheme = Theme.of(context).colorScheme;
    final persons = ref.watch(personsProvider).value ?? const <Person>[];
    final days = lastDateKeys(7);
    final daysData = [
      for (final k in days) ref.watch(summariesProvider(k)),
    ];

    if (daysData.any((d) => d is! AsyncData)) {
      return TitledPanel(
        title: s.tardinessByPerson,
        child: const SizedBox(
          height: 160,
          child: Center(child: SizedBox(width: 24, height: 24, child: CircularProgressIndicator(strokeWidth: 2.4))),
        ),
      );
    }

    final lateMinutes = <int, int>{};
    final absents = <int, int>{};
    for (final d in daysData) {
      final sums = (d as AsyncData<List<DailySummary>>).value;
      for (final sum in sums) {
        if (sum.tardinessSeconds > 0) {
          lateMinutes[sum.personId] =
              (lateMinutes[sum.personId] ?? 0) + sum.tardinessSeconds ~/ 60;
        }
        if (sum.isAbsent) {
          absents[sum.personId] = (absents[sum.personId] ?? 0) + 1;
        }
      }
    }

    final rows = lateMinutes.entries.toList()
      ..sort((a, b) => b.value.compareTo(a.value));

    return TitledPanel(
      title: s.tardinessByPerson,
      child: rows.isEmpty && absents.isEmpty
          ? Padding(
              padding: const EdgeInsets.symmetric(vertical: 24),
              child: Center(
                child: Text(s.stOnTime,
                    style: TextStyle(color: scheme.onSurfaceVariant)),
              ),
            )
          : Column(
              children: [
                for (final e in rows.take(10))
                  Padding(
                    padding: const EdgeInsets.only(bottom: 10),
                    child: Row(
                      children: [
                        Expanded(
                          child: Text(
                            persons.where((p) => p.id == e.key).firstOrNull?.name ??
                                '—',
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(fontSize: 13.5),
                          ),
                        ),
                        if (absents[e.key] != null) ...[
                          StatusChip(
                            label: '${fa ? faDigits(absents[e.key]!) : absents[e.key]!} ${s.stAbsent}',
                            tone: ChipTone.error,
                          ),
                          const SizedBox(width: 8),
                        ],
                        StatusChip(
                          label:
                              '${fa ? faDigits(e.value) : e.value} ${s.isFa ? 'دقیقه' : 'min'}',
                          tone: ChipTone.warning,
                        ),
                      ],
                    ),
                  ),
              ],
            ),
    );
  }
}
