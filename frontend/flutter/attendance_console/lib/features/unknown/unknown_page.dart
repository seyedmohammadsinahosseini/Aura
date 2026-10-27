import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/app_strings.dart';
import '../../core/formatters.dart';
import '../../data/models.dart';
import '../../providers.dart';
import '../../shared/widgets.dart';
import '../people/people_page.dart' show accessLevelsUpTo;

/// Review queue for unrecognized faces: enroll as new person, merge into
/// an existing one, or discard.
class UnknownFacesPage extends ConsumerWidget {
  const UnknownFacesPage({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final faces = ref.watch(unknownFacesProvider);

    return PageBody(
      maxWidth: 1400,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          PageHeader(
            title: s.unknownTitle,
            subtitle: s.unknownEmptyHint,
          ),
          AsyncGate(
            value: faces,
            data: (List<UnknownFace> list) {
              if (list.isEmpty) {
                return EmptyState(
                  icon: Icons.person_search_rounded,
                  title: s.unknownEmpty,
                  message: s.unknownEmptyHint,
                );
              }
              return LayoutBuilder(builder: (context, constraints) {
                final w = constraints.maxWidth;
                final perRow = w > 1100 ? 4 : (w > 720 ? 3 : 2);
                return Wrap(
                  spacing: 14,
                  runSpacing: 14,
                  children: [
                    for (final face in list)
                      SizedBox(
                        width: (w - 14 * (perRow - 1)) / perRow,
                        child: _FaceCard(face: face),
                      ),
                  ],
                );
              });
            },
          ),
        ],
      ),
    );
  }
}

class _FaceCard extends ConsumerWidget {
  const _FaceCard({required this.face});

  final UnknownFace face;

  Future<void> _enroll(BuildContext context, WidgetRef ref) async {
    final s = S.of(context);
    final id = await showDialog<int>(
      context: context,
      builder: (_) => _EnrollDialog(faceId: face.id),
    );
    if (id != null && context.mounted) {
      context.showSuccess(s.enrolledOk);
      context.go('/people/$id');
      ref.invalidate(unknownFacesProvider);
      ref.invalidate(personsProvider);
      ref.invalidate(personPictureProvider(id));
    }
  }

  Future<void> _merge(BuildContext context, WidgetRef ref) async {
    final s = S.of(context);
    final ok = await showDialog<bool>(
      context: context,
      builder: (_) => _MergeDialog(faceId: face.id),
    );
    if (ok == true) {
      ref.invalidate(unknownFacesProvider);
      ref.invalidate(personsProvider);
      if (context.mounted) context.showSuccess(s.mergedOk);
    }
  }

  Future<void> _discard(BuildContext context, WidgetRef ref) async {
    final s = S.of(context);
    if (!await confirmDialog(
      context,
      title: s.discard,
      message: s.discardQ,
      confirmLabel: s.discard,
    )) {
      return;
    }
    try {
      await ref.read(unknownRepoProvider).discard(face.id);
      ref.invalidate(unknownFacesProvider);
      if (context.mounted) context.showSuccess(s.discardedOk);
    } catch (e) {
      if (context.mounted) context.showError(e);
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;
    final image = ref.watch(unknownFaceImageProvider(face.id));
    final time = face.timestamp > 0
        ? timeAgo(DateTime.fromMillisecondsSinceEpoch(face.timestamp * 1000),
            fa: s.isFa)
        : '';

    return Panel(
      padding: EdgeInsets.zero,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          AspectRatio(
            aspectRatio: 1.15,
            child: image.when(
              data: (bytes) => bytes == null
                  ? ColoredBox(
                      color: scheme.surfaceContainerLow,
                      child: const Center(child: Icon(Icons.image_not_supported_outlined, size: 28)),
                    )
                  : Image.memory(
                      bytes,
                      fit: BoxFit.cover,
                      gaplessPlayback: true,
                      errorBuilder: (_, _, _) => ColoredBox(
                        color: scheme.surfaceContainerLow,
                        child: const Center(child: Icon(Icons.broken_image_outlined, size: 28)),
                      ),
                    ),
              loading: () => ColoredBox(
                color: scheme.surfaceContainerLow,
                child: const Center(child: CircularProgressIndicator(strokeWidth: 2)),
              ),
              error: (_, _) => ColoredBox(
                color: scheme.surfaceContainerLow,
                child: const Center(child: Icon(Icons.image_not_supported_outlined, size: 28)),
              ),
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(12),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                if (face.cameraUid.isNotEmpty)
                  Text(
                    '${s.camerasTitle}: ${face.cameraUid}',
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(fontSize: 11.5, color: scheme.onSurfaceVariant),
                  ),
                if (time.isNotEmpty)
                  Text(
                    '${s.seenOn} $time',
                    style: TextStyle(fontSize: 11.5, color: scheme.onSurfaceVariant),
                  ),
                const SizedBox(height: 10),
                Row(
                  children: [
                    Expanded(
                      child: _ActionChip(
                        icon: Icons.person_add_alt_rounded,
                        label: s.enrollAsNew,
                        primary: true,
                        onTap: () => _enroll(context, ref),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 6),
                Row(
                  children: [
                    Expanded(
                      child: _ActionChip(
                        icon: Icons.merge_rounded,
                        label: s.mergeExisting,
                        onTap: () => _merge(context, ref),
                      ),
                    ),
                    const SizedBox(width: 6),
                    _ActionChip(
                      icon: Icons.delete_outline_rounded,
                      label: s.discard,
                      onTap: () => _discard(context, ref),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _ActionChip extends StatelessWidget {
  const _ActionChip({
    required this.icon,
    required this.label,
    required this.onTap,
    this.primary = false,
  });

  final IconData icon;
  final String label;
  final VoidCallback onTap;
  final bool primary;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Material(
      color: primary ? scheme.primaryContainer : scheme.surfaceContainer,
      borderRadius: BorderRadius.circular(9),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(9),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon,
                  size: 15, color: primary ? scheme.primary : scheme.onSurfaceVariant),
              const SizedBox(width: 6),
              Flexible(
                child: Text(
                  label,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: 11.5,
                    fontWeight: FontWeight.w600,
                    color: primary ? scheme.onPrimaryContainer : scheme.onSurfaceVariant,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// ── Enroll dialog ──────────────────────────────────────────────────────────

class _EnrollDialog extends ConsumerStatefulWidget {
  const _EnrollDialog({required this.faceId});

  final int faceId;

  @override
  ConsumerState<_EnrollDialog> createState() => _EnrollDialogState();
}

class _EnrollDialogState extends ConsumerState<_EnrollDialog> {
  final _name = TextEditingController();
  final _code = TextEditingController();
  int _access = 2;
  bool _saving = false;

  @override
  void dispose() {
    _name.dispose();
    _code.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    if (_name.text.trim().isEmpty) return;
    setState(() => _saving = true);
    try {
      final id = await ref.read(unknownRepoProvider).enrollAsNew(
            widget.faceId,
            name: _name.text.trim(),
            code: _code.text.trim(),
            accessLevel: _access,
          );
      if (mounted) Navigator.pop(context, id);
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
      title: Text(s.enrollAsNew),
      content: SizedBox(
        width: 400,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            LabeledField(
              label: s.personName,
              child: TextField(
                controller: _name,
                autofocus: true,
                textDirection: TextDirection.rtl,
                onSubmitted: (_) => _save(),
              ),
            ),
            const SizedBox(height: 14),
            LabeledField(
              label: s.personCode,
              child: TextField(controller: _code, onSubmitted: (_) => _save()),
            ),
            const SizedBox(height: 14),
            LabeledField(
              label: s.accessLevel,
              child: DropdownMenu<int>(
                initialSelection: _access,
                onSelected: (v) => setState(() => _access = v ?? 2),
                dropdownMenuEntries: [
                  for (final level in accessLevelsUpTo(_access))
                    DropdownMenuEntry(value: level, label: s.accessLevelName(level)),
                ],
              ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(context), child: Text(s.cancel)),
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

// ── Merge dialog (person picker) ───────────────────────────────────────────

class _MergeDialog extends ConsumerStatefulWidget {
  const _MergeDialog({required this.faceId});

  final int faceId;

  @override
  ConsumerState<_MergeDialog> createState() => _MergeDialogState();
}

class _MergeDialogState extends ConsumerState<_MergeDialog> {
  final _search = TextEditingController();
  bool _saving = false;

  @override
  void dispose() {
    _search.dispose();
    super.dispose();
  }

  Future<void> _merge(Person person) async {
    setState(() => _saving = true);
    try {
      await ref
          .read(unknownRepoProvider)
          .mergeInto(widget.faceId, person.id);
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
    final scheme = Theme.of(context).colorScheme;
    final persons = ref.watch(personsProvider);
    final query = _search.text.trim().toLowerCase();

    return AlertDialog(
      title: Text(s.mergeExisting),
      content: SizedBox(
        width: 440,
        height: 420,
        child: Column(
          children: [
            TextField(
              controller: _search,
              onChanged: (_) => setState(() {}),
              decoration: InputDecoration(
                hintText: s.search,
                prefixIcon: const Icon(Icons.search_rounded, size: 20),
              ),
            ),
            const SizedBox(height: 12),
            Expanded(
              child: persons.when(
                data: (list) {
                  final filtered = query.isEmpty
                      ? list
                      : list
                          .where((p) =>
                              p.name.toLowerCase().contains(query) ||
                              p.code.toLowerCase().contains(query))
                          .toList();
                  filtered.sort((a, b) => a.name.compareTo(b.name));
                  if (filtered.isEmpty) {
                    return Center(child: Text(s.peopleEmpty));
                  }
                  return ListView.builder(
                    itemCount: filtered.length,
                    itemBuilder: (context, i) {
                      final p = filtered[i];
                      return ListTile(
                        leading: PersonAvatar(personId: p.id, name: p.name, size: 34),
                        title: Text(p.name, style: const TextStyle(fontSize: 13.5)),
                        subtitle: p.code.isNotEmpty
                            ? Text(p.code,
                                style: TextStyle(
                                    fontSize: 11.5, color: scheme.onSurfaceVariant))
                            : null,
                        onTap: _saving ? null : () => _merge(p),
                      );
                    },
                  );
                },
                loading: () => const Center(child: CircularProgressIndicator(strokeWidth: 2)),
                error: (e, _) => Center(child: Text('$e')),
              ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(context, false), child: Text(s.cancel)),
      ],
    );
  }
}
