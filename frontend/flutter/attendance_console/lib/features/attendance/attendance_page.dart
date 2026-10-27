import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/app_strings.dart';
import '../../core/formatters.dart';
import '../../data/models.dart';
import '../../providers.dart';
import '../../shared/widgets.dart';

/// Full daily attendance board for a chosen date.
class AttendancePage extends ConsumerStatefulWidget {
  const AttendancePage({super.key});

  @override
  ConsumerState<AttendancePage> createState() => _AttendancePageState();
}

class _AttendancePageState extends ConsumerState<AttendancePage> {
  DateTime _date = DateTime.now();
  final _search = TextEditingController();

  @override
  void dispose() {
    _search.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final fa = s.isFa;
    final scheme = Theme.of(context).colorScheme;
    final dateK = dateKey(_date);
    final isToday = dateK == todayKey();

    final persons = ref.watch(personsProvider);
    final summaries = ref.watch(summariesProvider(dateK));

    return PageBody(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          PageHeader(title: s.attendanceTitle),
          Panel(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
            child: DateNavigator(
              date: _date,
              onChanged: (d) => setState(() => _date = d),
            ),
          ),
          const SizedBox(height: 14),
          AsyncGate(
            value: summaries,
            data: (List<DailySummary> sums) {
              final pList = persons.value ?? const <Person>[];

              var present = 0, late = 0, absent = 0;
              for (final sum in sums) {
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

              return Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Wrap(
                    spacing: 12,
                    runSpacing: 12,
                    children: [
                      StatCard(
                        icon: Icons.check_circle_outline_rounded,
                        value: fa ? faDigits(present) : '$present',
                        label: s.stPresent,
                        tone: StatTone.success,
                      ),
                      StatCard(
                        icon: Icons.schedule_rounded,
                        value: fa ? faDigits(late) : '$late',
                        label: s.stLate,
                        tone: StatTone.warning,
                      ),
                      StatCard(
                        icon: Icons.person_off_outlined,
                        value: fa ? faDigits(absent) : '$absent',
                        label: s.stAbsent,
                        tone: StatTone.error,
                      ),
                    ],
                  ),
                  const SizedBox(height: 14),
                  Row(
                    children: [
                      Expanded(
                        child: SizedBox(
                          height: 42,
                          child: TextField(
                            controller: _search,
                            onChanged: (_) => setState(() {}),
                            decoration: InputDecoration(
                              hintText: s.search,
                              prefixIcon: const Icon(Icons.search_rounded, size: 20),
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 14),
                  if (pList.isEmpty)
                    Padding(
                      padding: const EdgeInsets.symmetric(vertical: 40),
                      child: Center(
                        child: Text(s.peopleEmpty,
                            style: TextStyle(color: scheme.onSurfaceVariant)),
                      ),
                    )
                  else
                    _Board(
                      persons: pList,
                      summaries: sums,
                      isToday: isToday,
                      fa: fa,
                      s: s,
                      query: _search.text.trim().toLowerCase(),
                    ),
                ],
              );
            },
          ),
        ],
      ),
    );
  }
}

class _Board extends StatelessWidget {
  const _Board({
    required this.persons,
    required this.summaries,
    required this.isToday,
    required this.fa,
    required this.s,
    required this.query,
  });

  final List<Person> persons;
  final List<DailySummary> summaries;
  final bool isToday;
  final bool fa;
  final S s;
  final String query;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;

    final byPerson = {
      for (final sum in summaries) sum.personId: sum,
    };

    var rows = persons.map((p) {
      final sum = byPerson[p.id] ??
          DailySummary(personId: p.id, date: dateKey(DateTime.now()));
      return (p, sum, summaryStatus(sum, isToday: isToday));
    }).toList();

    if (query.isNotEmpty) {
      rows = rows
          .where((r) =>
              r.$1.name.toLowerCase().contains(query) ||
              r.$1.code.toLowerCase().contains(query))
          .toList();
    }

    rows.sort((a, b) {
      int rank((Person, DailySummary, AttStatus) r) => switch (r.$3) {
            AttStatus.late => 0,
            AttStatus.absent => 1,
            AttStatus.present => 2,
            AttStatus.notArrived => 3,
            AttStatus.notRecorded => 4,
          };
      final byStatus = rank(a).compareTo(rank(b));
      if (byStatus != 0) return byStatus;
      return a.$1.name.compareTo(b.$1.name);
    });

    if (rows.isEmpty) {
      return Padding(
        padding: const EdgeInsets.symmetric(vertical: 40),
        child: Center(
          child: Text(s.attendanceEmpty,
              style: TextStyle(color: scheme.onSurfaceVariant)),
        ),
      );
    }

    return Panel(
      padding: EdgeInsets.zero,
      child: Column(
        children: [
          // header row
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
            child: Row(
              children: [
                const SizedBox(width: 52),
                Expanded(
                    flex: 3,
                    child: Text(s.personName,
                        style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: scheme.onSurfaceVariant))),
                Expanded(
                    flex: 2,
                    child: Text(s.arrival,
                        style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: scheme.onSurfaceVariant))),
                Expanded(
                    flex: 2,
                    child: Text(s.departure,
                        style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: scheme.onSurfaceVariant))),
                Expanded(
                    flex: 2,
                    child: Text(s.worked,
                        style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: scheme.onSurfaceVariant))),
                SizedBox(
                  width: 120,
                  child: Align(
                    alignment: AlignmentDirectional.centerEnd,
                    child: Text(s.status,
                        style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: scheme.onSurfaceVariant)),
                  ),
                ),
              ],
            ),
          ),
          for (final (person, sum, status) in rows)
              InkWell(
                onTap: () => context.go('/people/${person.id}'),
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
                  child: Row(
                    children: [
                      PersonAvatar(personId: person.id, name: person.name, size: 36),
                      const SizedBox(width: 16),
                      Expanded(
                        flex: 3,
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(person.name,
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(
                                    fontSize: 13.5, fontWeight: FontWeight.w600)),
                            if (person.code.isNotEmpty)
                              Text(person.code,
                                  style: TextStyle(
                                      fontSize: 11.5,
                                      color: scheme.onSurfaceVariant)),
                          ],
                        ),
                      ),
                      Expanded(
                        flex: 2,
                        child: Text(
                          sum.arrival > 0 ? unixClock(sum.arrival, fa: fa) : '—',
                          style: const TextStyle(fontSize: 13),
                        ),
                      ),
                      Expanded(
                        flex: 2,
                        child: Text(
                          sum.departure > 0 ? unixClock(sum.departure, fa: fa) : '—',
                          style: const TextStyle(fontSize: 13),
                        ),
                      ),
                      Expanded(
                        flex: 2,
                        child: Text(
                          sum.workSeconds > 0
                              ? compactDuration(sum.workSeconds, fa: fa)
                              : '—',
                          style: const TextStyle(fontSize: 13),
                        ),
                      ),
                      SizedBox(
                        width: 120,
                        child: Align(
                          alignment: AlignmentDirectional.centerEnd,
                          child: switch (status) {
                            AttStatus.present =>
                              StatusChip(label: s.stOnTime, tone: ChipTone.success),
                            AttStatus.late =>
                              StatusChip(label: s.stLate, tone: ChipTone.warning),
                            AttStatus.absent =>
                              StatusChip(label: s.stAbsent, tone: ChipTone.error),
                            AttStatus.notArrived =>
                              StatusChip(label: s.stNotArrived, tone: ChipTone.neutral),
                            AttStatus.notRecorded =>
                              const SizedBox.shrink(),
                          },
                        ),
                      ),
                    ],
                  ),
                ),
              ),
        ],
      ),
    );
  }
}
