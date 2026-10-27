import 'dart:convert';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/app_strings.dart';
import '../../core/app_theme.dart';
import '../../core/formatters.dart';
import '../../data/models.dart';
import '../../providers.dart';
import '../../shared/widgets.dart';
import 'people_page.dart';

/// Person profile: identity header, daily attendance with a visual 24h
/// timeline, breaks, work schedule editor and last-seen location.
class PersonDetailPage extends ConsumerStatefulWidget {
  const PersonDetailPage({super.key, required this.personId});

  final int personId;

  @override
  ConsumerState<PersonDetailPage> createState() => _PersonDetailPageState();
}

class _PersonDetailPageState extends ConsumerState<PersonDetailPage> {
  DateTime _date = DateTime.now();
  bool _uploading = false;

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final fa = s.isFa;
    final person = ref.watch(personProvider(widget.personId));
    final dateK = dateKey(_date);
    final isToday = dateK == todayKey();
    final day = ref.watch(personDayProvider(
      (personId: widget.personId, dateKey: dateK),
    ));

    return PageBody(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          PageHeader(
            title: s.profile,
            actions: [
              IconButton(
                tooltip: s.close,
                icon: const Icon(Icons.arrow_forward_rounded),
                onPressed: () => context.go('/people'),
              ),
            ],
          ),
          AsyncGate(
            value: person,
            data: (Person p) => Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                _ProfileHeader(
                  person: p,
                  s: s,
                  onUpload: () => _uploadPhoto(p),
                  uploading: _uploading,
                  onEdit: () => _edit(p),
                  onDelete: () => _delete(p),
                ),
                const SizedBox(height: 16),
                _DayCard(
                  date: _date,
                  isToday: isToday,
                  day: day,
                  fa: fa,
                  s: s,
                  onDateChanged: (d) => setState(() => _date = d),
                ),
                const SizedBox(height: 16),
                LayoutBuilder(builder: (context, constraints) {
                  final wide = constraints.maxWidth > 1000;
                  final schedule = _ScheduleCard(personId: p.id, s: s);
                  final location = _LocationCard(personId: p.id, s: s);
                  return wide
                      ? Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Expanded(child: schedule),
                            const SizedBox(width: 14),
                            Expanded(child: location),
                          ],
                        )
                      : Column(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                            schedule,
                            const SizedBox(height: 14),
                            location,
                          ],
                        );
                }),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Future<void> _uploadPhoto(Person p) async {
    final s = S.of(context);
    final result = await FilePicker.pickFiles(
      type: FileType.image,
      withData: true,
    );
    final file = result?.files.single;
    final bytes = file?.bytes;
    if (bytes == null) return;
    setState(() => _uploading = true);
    try {
      final ext = (file?.extension ?? 'jpg').toLowerCase() == 'png' ? 'png' : 'jpg';
      await ref
          .read(personsRepoProvider)
          .uploadPicture(p.id, base64Encode(bytes), extension: ext);
      ref.invalidate(personPictureProvider(p.id));
      if (mounted) context.showSuccess(s.photoUpdated);
    } catch (e) {
      if (mounted) context.showError(e);
    } finally {
      if (mounted) setState(() => _uploading = false);
    }
  }

  Future<void> _edit(Person p) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (_) => PersonFormDialog(person: p),
    );
    if (ok == true) {
      ref.invalidate(personProvider(p.id));
      ref.invalidate(personsProvider);
    }
  }

  Future<void> _delete(Person p) async {
    final s = S.of(context);
    if (!await confirmDialog(
      context,
      title: s.delete,
      message: s.deletePersonQ,
      confirmLabel: s.delete,
    )) {
      return;
    }
    try {
      await ref.read(personsRepoProvider).delete(p.id);
      ref.invalidate(personsProvider);
      ref.invalidate(personPictureProvider(p.id));
      if (mounted) context.go('/people');
    } catch (e) {
      if (mounted) context.showError(e);
    }
  }
}

// ── Profile header ─────────────────────────────────────────────────────────

class _ProfileHeader extends ConsumerWidget {
  const _ProfileHeader({
    required this.person,
    required this.s,
    required this.onUpload,
    required this.uploading,
    required this.onEdit,
    required this.onDelete,
  });

  final Person person;
  final S s;
  final VoidCallback onUpload;
  final bool uploading;
  final VoidCallback onEdit;
  final VoidCallback onDelete;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final scheme = Theme.of(context).colorScheme;
    return Panel(
      padding: const EdgeInsets.all(20),
      child: Column(
        children: [
          Row(
            children: [
              PersonAvatar(personId: person.id, name: person.name, size: 64),
              const SizedBox(width: 18),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(person.name,
                        style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w700)),
                    const SizedBox(height: 6),
                    Row(
                      children: [
                        if (person.code.isNotEmpty) ...[
                          Text(person.code,
                              style: TextStyle(fontSize: 13, color: scheme.onSurfaceVariant)),
                          const SizedBox(width: 10),
                        ],
                        StatusChip(
                          label: s.accessLevelName(person.accessLevel),
                          tone: person.accessLevel >= 3 ? ChipTone.primary : ChipTone.neutral,
                        ),
                      ],
                    ),
                  ],
                ),
              ),
              OutlinedButton.icon(
                onPressed: uploading ? null : onUpload,
                icon: uploading
                    ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                    : const Icon(Icons.photo_camera_front_outlined, size: 18),
                label: Text(s.uploadPhoto),
              ),
              const SizedBox(width: 8),
              IconButton(
                tooltip: s.edit,
                onPressed: onEdit,
                icon: Icon(Icons.edit_outlined, color: scheme.onSurfaceVariant),
              ),
              IconButton(
                tooltip: s.delete,
                onPressed: onDelete,
                icon: Icon(Icons.delete_outline_rounded, color: scheme.error),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

// ── Day card (summary + timeline + breaks) ────────────────────────────────

class _DayCard extends ConsumerWidget {
  const _DayCard({
    required this.date,
    required this.isToday,
    required this.day,
    required this.fa,
    required this.s,
    required this.onDateChanged,
  });

  final DateTime date;
  final bool isToday;
  final AsyncValue<PersonDay> day;
  final bool fa;
  final S s;
  final ValueChanged<DateTime> onDateChanged;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return TitledPanel(
      title: s.dayTimeline,
      trailing: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 380),
        child: DateNavigator(date: date, onChanged: onDateChanged),
      ),
      child: AsyncGate(
        value: day,
        data: (PersonDay d) {
          final summary = d.summary;
          final status = summaryStatus(summary, isToday: isToday);

          String orDash(String v) => v.isEmpty ? '—' : v;
          final arrival = orDash(unixClock(summary.arrival, fa: fa));
          final departure = orDash(unixClock(summary.departure, fa: fa));
          final worked = orDash(compactDuration(summary.workSeconds, fa: fa));
          final breaks = orDash(compactDuration(summary.breakSeconds, fa: fa));
          final lateBy = summary.tardinessSeconds > 0
              ? compactDuration(summary.tardinessSeconds, fa: fa)
              : '';

          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Wrap(
                spacing: 18,
                runSpacing: 12,
                crossAxisAlignment: WrapCrossAlignment.center,
                children: [
                  _Metric(label: s.arrival, value: arrival),
                  _Metric(label: s.departure, value: departure),
                  _Metric(label: s.worked, value: worked),
                  _Metric(label: s.onBreak, value: breaks),
                  if (lateBy.isNotEmpty) _Metric(label: s.lateBy, value: lateBy),
                  switch (status) {
                    AttStatus.present =>
                      StatusChip(label: s.stOnTime, tone: ChipTone.success),
                    AttStatus.late => StatusChip(label: s.stLate, tone: ChipTone.warning),
                    AttStatus.absent => StatusChip(label: s.stAbsent, tone: ChipTone.error),
                    AttStatus.notArrived =>
                      StatusChip(label: s.stNotArrived, tone: ChipTone.neutral),
                    AttStatus.notRecorded =>
                      StatusChip(label: s.notSet, tone: ChipTone.neutral),
                  },
                ],
              ),
              const SizedBox(height: 18),
              if (summary.arrival > 0) ...[
                DayBar(summary: summary, fa: fa),
                const SizedBox(height: 18),
              ],
              _BreaksList(breaks: d.breaks, fa: fa, s: s),
            ],
          );
        },
      ),
    );
  }
}

class _Metric extends StatelessWidget {
  const _Metric({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label,
            style: TextStyle(fontSize: 11.5, color: scheme.onSurfaceVariant)),
        const SizedBox(height: 3),
        Text(value, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600)),
      ],
    );
  }
}

class _BreaksList extends StatelessWidget {
  const _BreaksList({required this.breaks, required this.fa, required this.s});

  final List<BreakRecord> breaks;
  final bool fa;
  final S s;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    if (breaks.isEmpty) {
      return Padding(
        padding: const EdgeInsets.symmetric(vertical: 8),
        child: Text(s.noBreaks,
            style: TextStyle(fontSize: 12.5, color: scheme.onSurfaceVariant)),
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(s.breaksTitle,
            style: Theme.of(context).textTheme.titleSmall),
        const SizedBox(height: 10),
        for (final b in breaks)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Row(
              children: [
                Icon(Icons.coffee_rounded, size: 16, color: scheme.onSurfaceVariant),
                const SizedBox(width: 8),
                Text(
                  '${unixClock(b.breakStart, fa: fa)} ← ${unixClock(b.breakEnd, fa: fa)}',
                  style: const TextStyle(fontSize: 13),
                ),
                const SizedBox(width: 10),
                Text(
                  b.breakEnd > 0
                      ? compactDuration(b.breakSeconds, fa: fa)
                      : s.onBreak,
                  style: TextStyle(fontSize: 12.5, color: scheme.onSurfaceVariant),
                ),
              ],
            ),
          ),
      ],
    );
  }
}

// ── 24h visual timeline ───────────────────────────────────────────────────

class DayBar extends StatelessWidget {
  const DayBar({super.key, required this.summary, required this.fa});

  final DailySummary summary;
  final bool fa;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final sem = SemanticColors.of(context);

    final arrival = summary.arrival;
    final end = summary.departure > 0
        ? summary.departure
        : (dateKey(DateTime.now()) == summary.date ? DateTime.now().millisecondsSinceEpoch ~/ 1000 : summary.departure);

    return Directionality(
      textDirection: TextDirection.ltr, // time always flows left→right
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            height: 36,
            child: CustomPaint(
              size: const Size(double.infinity, 36),
              painter: _DayBarPainter(
                track: scheme.surfaceContainerHigh,
                work: scheme.primary,
                breakColor: sem.warning,
                arrivalSec: arrival > 0 ? arrival : 0,
                endSec: end,
                breaks: [
                  for (final b in summary.breaks) (b.start, b.end > 0 ? b.end : end),
                ],
              ),
            ),
          ),
          const SizedBox(height: 4),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              for (final h in const [0, 6, 12, 18, 24])
                Text(
                  fa ? faDigits('$h') : '$h',
                  style: TextStyle(fontSize: 10.5, color: scheme.onSurfaceVariant),
                ),
            ],
          ),
        ],
      ),
    );
  }
}

class _DayBarPainter extends CustomPainter {
  _DayBarPainter({
    required this.track,
    required this.work,
    required this.breakColor,
    required this.arrivalSec,
    required this.endSec,
    required this.breaks,
  });

  final Color track;
  final Color work;
  final Color breakColor;
  final int arrivalSec;
  final int endSec;
  final List<(int, int)> breaks;

  static double _fracOf(int sec) {
    final d = DateTime.fromMillisecondsSinceEpoch(sec * 1000);
    return (d.hour * 60 + d.minute) / (24 * 60);
  }

  @override
  void paint(Canvas canvas, Size size) {
    final r = Radius.circular(size.height / 2);
    final trackPaint = Paint()..color = track;
    final trackRect = RRect.fromRectAndCorners(
      Offset.zero & size,
      topLeft: r, topRight: r, bottomLeft: r, bottomRight: r,
    );
    canvas.drawRRect(trackRect, trackPaint);

    if (arrivalSec > 0 && endSec > arrivalSec) {
      final startF = _fracOf(arrivalSec).clamp(0.0, 1.0);
      final endF = _fracOf(endSec).clamp(startF, 1.0);
      if (endF > startF) {
        final workRect = RRect.fromRectAndCorners(
          Rect.fromLTWH(startF * size.width, 0, (endF - startF) * size.width, size.height),
          topLeft: r, topRight: r, bottomLeft: r, bottomRight: r,
        );
        canvas.drawRRect(workRect, Paint()..color = work);

        for (final (bs, be) in breaks) {
          if (be <= bs) continue;
          final bStart = _fracOf(bs).clamp(startF, endF);
          final bEnd = _fracOf(be).clamp(bStart, endF);
          if (bEnd <= bStart) continue;
          canvas.drawRRect(
            RRect.fromRectAndCorners(
              Rect.fromLTWH(
                bStart * size.width,
                size.height * .22,
                (bEnd - bStart) * size.width,
                size.height * .56,
              ),
              topLeft: r, topRight: r, bottomLeft: r, bottomRight: r,
            ),
            Paint()..color = breakColor,
          );
        }
      }
    }
  }

  @override
  bool shouldRepaint(covariant _DayBarPainter old) =>
      old.arrivalSec != arrivalSec ||
      old.endSec != endSec ||
      old.breaks.length != breaks.length;
}

// ── Schedule editor ────────────────────────────────────────────────────────

class _ScheduleCard extends ConsumerStatefulWidget {
  const _ScheduleCard({required this.personId, required this.s});

  final int personId;
  final S s;

  @override
  ConsumerState<_ScheduleCard> createState() => _ScheduleCardState();
}

class _ScheduleCardState extends ConsumerState<_ScheduleCard> {
  bool _dirty = false;
  bool _saving = false;
  Schedule? _draft;

  @override
  Widget build(BuildContext context) {
    final s = widget.s;
    final schedule = ref.watch(personScheduleProvider(widget.personId));

    return TitledPanel(
      title: s.schedule,
      child: AsyncGate(
        value: schedule,
        data: (Schedule? saved) {
          final current = _draft ?? saved ?? Schedule(personId: widget.personId);
          return _editor(context, current, saved == null);
        },
      ),
    );
  }

  Widget _editor(BuildContext context, Schedule current, bool isDefault) {
    final s = widget.s;
    final scheme = Theme.of(context).colorScheme;

    void update(Schedule next) {
      setState(() {
        _draft = next;
        _dirty = true;
      });
    }

    // ISO weekday: 1=Mon…7=Sun; presented in the locale's week order.
    final order = s.isFa ? const [6, 7, 1, 2, 3, 4, 5] : const [1, 2, 3, 4, 5, 6, 7];
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text(
          s.scheduleHint,
          style: TextStyle(fontSize: 12.5, color: scheme.onSurfaceVariant),
        ),
        const SizedBox(height: 14),
        Row(
          children: [
            Expanded(
              child: LabeledField(
                label: s.expectedArrival,
                child: _TimeField(
                  value: current.arrival,
                  onChanged: (v) => update(current.copyWith(arrival: v)),
                ),
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: LabeledField(
                label: s.expectedDeparture,
                child: _TimeField(
                  value: current.departure,
                  onChanged: (v) => update(current.copyWith(departure: v)),
                ),
              ),
            ),
          ],
        ),
        const SizedBox(height: 14),
        LabeledField(
          label: s.graceMinutes,
          child: DropdownMenu<int>(
            initialSelection: current.graceMinutes,
            onSelected: (v) => update(current.copyWith(graceMinutes: v ?? 15)),
            dropdownMenuEntries: [
              for (final m in const [0, 5, 10, 15, 20, 30, 45, 60])
                DropdownMenuEntry(
                  value: m,
                  label: s.isFa ? faDigits(m) : '$m',
                ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        LabeledField(
          label: s.workDays,
          child: Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              for (final iso in order)
                _DayChip(
                  label: _isoDayLabel(s, iso),
                  selected: current.daySet.contains(iso),
                  onTap: () {
                    final set = {...current.daySet};
                    set.contains(iso) ? set.remove(iso) : set.add(iso);
                    update(current.copyWith(days: set));
                  },
                ),
            ],
          ),
        ),
        const SizedBox(height: 16),
        Row(
          children: [
            FilledButton(
              onPressed: (_dirty && !_saving)
                  ? () async {
                      setState(() => _saving = true);
                      try {
                        await ref.read(personsRepoProvider).saveSchedule(_draft!);
                        ref.invalidate(personScheduleProvider(widget.personId));
                        if (mounted) {
                          setState(() { _dirty = false; _draft = null; });
                          this.context.showSuccess(s.scheduleSaved);
                        }
                      } catch (e) {
                        if (mounted) this.context.showError(e);
                      } finally {
                        if (mounted) setState(() => _saving = false);
                      }
                    }
                  : null,
              child: _saving
                  ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                  : Text(s.save),
            ),
            if (isDefault && !_dirty) ...[
              const SizedBox(width: 10),
              Text(
                s.notSet,
                style: TextStyle(fontSize: 12, color: scheme.onSurfaceVariant),
              ),
            ],
          ],
        ),
      ],
    );
  }
}

extension on Schedule {
  Schedule copyWith({
    String? arrival,
    String? departure,
    int? graceMinutes,
    Set<int>? days,
  }) {
    return Schedule(
      personId: personId,
      arrival: arrival ?? this.arrival,
      departure: departure ?? this.departure,
      graceMinutes: graceMinutes ?? this.graceMinutes,
      workDays: days == null ? workDays : (days.toList()..sort()).join(','),
    );
  }
}

/// ISO weekday (1=Mon…7=Sun) → localized day name.
String _isoDayLabel(S s, int iso) {
  if (s.isFa) {
    const fa = {
      6: 'شنبه', 7: 'یکشنبه', 1: 'دوشنبه', 2: 'سه‌شنبه',
      3: 'چهارشنبه', 4: 'پنجشنبه', 5: 'جمعه',
    };
    return fa[iso] ?? '';
  }
  const en = {
    1: 'Mon', 2: 'Tue', 3: 'Wed', 4: 'Thu', 5: 'Fri', 6: 'Sat', 7: 'Sun',
  };
  return en[iso] ?? '';
}

class _TimeField extends StatelessWidget {
  const _TimeField({required this.value, required this.onChanged});

  final String value;
  final ValueChanged<String> onChanged;

  @override
  Widget build(BuildContext context) {
    return OutlinedButton(
      onPressed: () async {
        final initial = _parse(value) ?? const TimeOfDay(hour: 8, minute: 0);
        final picked = await showTimePicker(context: context, initialTime: initial);
        if (picked != null) {
          onChanged('${picked.hour.toString().padLeft(2, '0')}:${picked.minute.toString().padLeft(2, '0')}');
        }
      },
      child: Align(
        alignment: AlignmentDirectional.centerStart,
        child: Text(value, style: const TextStyle(fontSize: 13.5)),
      ),
    );
  }

  TimeOfDay? _parse(String v) {
    final parts = v.split(':');
    if (parts.length != 2) return null;
    final h = int.tryParse(parts[0]);
    final m = int.tryParse(parts[1]);
    if (h == null || m == null) return null;
    return TimeOfDay(hour: h, minute: m);
  }
}

class _DayChip extends StatelessWidget {
  const _DayChip({required this.label, required this.selected, required this.onTap});

  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Material(
      color: selected ? scheme.primaryContainer : scheme.surfaceContainer,
      borderRadius: BorderRadius.circular(999),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(999),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
          child: Text(
            label,
            style: TextStyle(
              fontSize: 12.5,
              fontWeight: FontWeight.w600,
              color: selected ? scheme.onPrimaryContainer : scheme.onSurfaceVariant,
            ),
          ),
        ),
      ),
    );
  }
}

// ── Last-seen location ────────────────────────────────────────────────────

class _LocationCard extends ConsumerWidget {
  const _LocationCard({required this.personId, required this.s});

  final int personId;
  final S s;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final scheme = Theme.of(context).colorScheme;
    final loc = ref.watch(personLocationProvider(personId));
    return TitledPanel(
      title: s.currentLocation,
      child: AsyncGate(
        value: loc,
        data: (CurrentLocation? l) {
          if (l == null) {
            return Padding(
              padding: const EdgeInsets.symmetric(vertical: 12),
              child: Text(
                s.notSet,
                style: TextStyle(fontSize: 13, color: scheme.onSurfaceVariant),
              ),
            );
          }
          final time = l.timestamp > 0
              ? DateTime.fromMillisecondsSinceEpoch(l.timestamp * 1000)
              : null;
          return Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(Icons.place_outlined, size: 18, color: scheme.primary),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      l.zoneName.isNotEmpty ? l.zoneName : '—',
                      style: const TextStyle(fontSize: 14.5, fontWeight: FontWeight.w600),
                    ),
                  ),
                ],
              ),
              if (time != null) ...[
                const SizedBox(height: 8),
                Text(
                  '${s.seenOn} ${longDate(time, fa: s.isFa)} · ${clock(time, fa: s.isFa)}',
                  style: TextStyle(fontSize: 12.5, color: scheme.onSurfaceVariant),
                ),
              ],
              const SizedBox(height: 8),
              Text(
                s.currentLocationHint,
                style: TextStyle(fontSize: 11.5, color: scheme.onSurfaceVariant.withValues(alpha: .8)),
              ),
            ],
          );
        },
      ),
    );
  }
}
