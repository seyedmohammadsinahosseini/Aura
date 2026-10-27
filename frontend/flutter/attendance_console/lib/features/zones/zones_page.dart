import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/app_strings.dart';
import '../../data/models.dart';
import '../../providers.dart';
import '../../shared/widgets.dart';
import '../people/people_page.dart' show accessLevelsUpTo;

/// Zones with their required access level.
class ZonesPage extends ConsumerWidget {
  const ZonesPage({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final zones = ref.watch(zonesProvider);

    return PageBody(
      maxWidth: 900,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          PageHeader(
            title: s.zonesTitle,
            actions: [
              FilledButton.icon(
                onPressed: () => _add(context, ref),
                icon: const Icon(Icons.add_rounded, size: 18),
                label: Text(s.addZone),
              ),
            ],
          ),
          AsyncGate(
            value: zones,
            data: (List<Zone> list) {
              if (list.isEmpty) {
                return EmptyState(
                  icon: Icons.map_outlined,
                  title: s.zonesEmpty,
                  message: s.currentLocationHint,
                  action: FilledButton.icon(
                    onPressed: () => _add(context, ref),
                    icon: const Icon(Icons.add_rounded, size: 18),
                    label: Text(s.addZone),
                  ),
                );
              }
              return Panel(
                padding: EdgeInsets.zero,
                child: Column(
                  children: [
                    for (final zone in list)
                      _ZoneRow(zone: zone),
                  ],
                ),
              );
            },
          ),
        ],
      ),
    );
  }

  Future<void> _add(BuildContext context, WidgetRef ref) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (_) => const _AddZoneDialog(),
    );
    if (ok == true) ref.invalidate(zonesProvider);
  }
}

class _ZoneRow extends ConsumerWidget {
  const _ZoneRow({required this.zone});

  final Zone zone;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
      child: Row(
        children: [
          Icon(Icons.place_outlined, size: 20, color: scheme.primary),
          const SizedBox(width: 14),
          Expanded(
            flex: 3,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(zone.name,
                    style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
                if (zone.description.isNotEmpty)
                  Text(zone.description,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(fontSize: 12, color: scheme.onSurfaceVariant)),
              ],
            ),
          ),
          Expanded(
            flex: 2,
            child: DropdownMenu<int>(
              initialSelection: zone.requiredAccessLevel,
              inputDecorationTheme: InputDecorationTheme(
                filled: true,
                fillColor: scheme.surfaceContainerLow,
                contentPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 13),
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(10),
                  borderSide: BorderSide(color: scheme.outlineVariant),
                ),
              ),
              onSelected: (v) async {
                if (v == null || v == zone.requiredAccessLevel) return;
                try {
                  await ref.read(zonesRepoProvider).setRequiredLevel(zone.id, v);
                  ref.invalidate(zonesProvider);
                } catch (e) {
                  if (context.mounted) context.showError(e);
                }
              },
              dropdownMenuEntries: [
                for (final level in accessLevelsUpTo(zone.requiredAccessLevel))
                  DropdownMenuEntry(value: level, label: s.accessLevelName(level)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _AddZoneDialog extends ConsumerStatefulWidget {
  const _AddZoneDialog();

  @override
  ConsumerState<_AddZoneDialog> createState() => _AddZoneDialogState();
}

class _AddZoneDialogState extends ConsumerState<_AddZoneDialog> {
  final _name = TextEditingController();
  final _desc = TextEditingController();
  bool _saving = false;

  @override
  void dispose() {
    _name.dispose();
    _desc.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    if (_name.text.trim().isEmpty) return;
    setState(() => _saving = true);
    try {
      await ref
          .read(zonesRepoProvider)
          .create(_name.text.trim(), _desc.text.trim());
      if (mounted) Navigator.pop(context, true);
    } catch (e) {
      if (mounted) {
        setState(() => _saving = false);
        context.showError(e);
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    return AlertDialog(
      title: Text(s.addZone),
      content: SizedBox(
        width: 400,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            LabeledField(
              label: s.zoneName,
              child: TextField(
                controller: _name,
                autofocus: true,
                onSubmitted: (_) => _save(),
              ),
            ),
            const SizedBox(height: 14),
            LabeledField(
              label: s.description,
              child: TextField(controller: _desc, onSubmitted: (_) => _save()),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(context, false), child: Text(s.cancel)),
        FilledButton(
          onPressed: _saving ? null : _save,
          child: _saving
              ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
              : Text(s.save),
        ),
      ],
    );
  }
}
