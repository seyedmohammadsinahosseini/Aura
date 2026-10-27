import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/app_strings.dart';
import '../../core/formatters.dart';
import '../../data/models.dart';
import '../../providers.dart';
import '../../shared/widgets.dart';

/// People directory: searchable table, add/edit, access level, delete.
class PeoplePage extends ConsumerStatefulWidget {
  const PeoplePage({super.key});

  @override
  ConsumerState<PeoplePage> createState() => _PeoplePageState();
}

class _PeoplePageState extends ConsumerState<PeoplePage> {
  final _search = TextEditingController();

  @override
  void dispose() {
    _search.dispose();
    super.dispose();
  }

  Future<void> _openForm([Person? person]) async {
    final created = await showDialog<bool>(
      context: context,
      builder: (_) => PersonFormDialog(person: person),
    );
    if (created == true) {
      ref.invalidate(personsProvider);
    }
  }

  Future<void> _delete(Person person) async {
    final s = S.of(context);
    final ok = await confirmDialog(
      context,
      title: s.delete,
      message: s.deletePersonQ,
      confirmLabel: s.delete,
    );
    if (!ok) return;
    try {
      await ref.read(personsRepoProvider).delete(person.id);
      ref.invalidate(personsProvider);
      if (mounted) context.showSuccess('${person.name} — ${s.delete}');
    } catch (e) {
      if (mounted) context.showError(e);
    }
  }

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final fa = s.isFa;
    final scheme = Theme.of(context).colorScheme;
    final persons = ref.watch(personsProvider);
    final query = _search.text.trim().toLowerCase();

    return PageBody(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          PageHeader(
            title: s.peopleTitle,
            actions: [
              FilledButton.icon(
                onPressed: () => _openForm(),
                icon: const Icon(Icons.person_add_alt_rounded, size: 18),
                label: Text(s.addPerson),
              ),
            ],
          ),
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
              const SizedBox(width: 12),
              Text(
                '${fa ? faDigits((persons.value ?? const []).length) : (persons.value ?? const []).length} ${s.dashPeople}',
                style: TextStyle(fontSize: 12.5, color: scheme.onSurfaceVariant),
              ),
            ],
          ),
          const SizedBox(height: 14),
          AsyncGate(
            value: persons,
            data: (List<Person> list) {
              if (list.isEmpty) {
                return EmptyState(
                  icon: Icons.people_outline_rounded,
                  title: s.peopleEmpty,
                  message: s.peopleEmptyHint,
                  action: FilledButton.icon(
                    onPressed: () => _openForm(),
                    icon: const Icon(Icons.person_add_alt_rounded, size: 18),
                    label: Text(s.addPerson),
                  ),
                );
              }
              final filtered = query.isEmpty
                  ? list
                  : list
                      .where((p) =>
                          p.name.toLowerCase().contains(query) ||
                          p.code.toLowerCase().contains(query))
                      .toList();
              filtered.sort((a, b) => a.name.compareTo(b.name));

              if (filtered.isEmpty) {
                return Padding(
                  padding: const EdgeInsets.symmetric(vertical: 40),
                  child: Center(
                    child: Text(s.notSet,
                        style: TextStyle(color: scheme.onSurfaceVariant)),
                  ),
                );
              }

              return Panel(
                padding: EdgeInsets.zero,
                child: Column(
                  children: [
                    for (var i = 0; i < filtered.length; i++)
                      _PersonRow(
                        person: filtered[i],
                        s: s,
                        onTap: () => context.go('/people/${filtered[i].id}'),
                        onEdit: () => _openForm(filtered[i]),
                        onDelete: () => _delete(filtered[i]),
                      ),
                  ],
                ),
              );
            },
          ),
        ],
      ),
    );
  }
}

class _PersonRow extends ConsumerWidget {
  const _PersonRow({
    required this.person,
    required this.s,
    required this.onTap,
    required this.onEdit,
    required this.onDelete,
  });

  final Person person;
  final S s;
  final VoidCallback onTap;
  final VoidCallback onEdit;
  final VoidCallback onDelete;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final scheme = Theme.of(context).colorScheme;
    return InkWell(
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        child: Row(
          children: [
            PersonAvatar(personId: person.id, name: person.name, size: 38),
            const SizedBox(width: 14),
            Expanded(
              flex: 3,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(person.name,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
                  if (person.code.isNotEmpty)
                    Text(person.code,
                        style: TextStyle(fontSize: 12, color: scheme.onSurfaceVariant)),
                ],
              ),
            ),
            Expanded(
              flex: 2,
              child: Align(
                alignment: AlignmentDirectional.centerStart,
                child: StatusChip(
                  label: s.accessLevelName(person.accessLevel),
                  tone: person.accessLevel >= 3
                      ? ChipTone.primary
                      : ChipTone.neutral,
                ),
              ),
            ),
            PopupMenuButton<String>(
              tooltip: s.actions,
              icon: Icon(Icons.more_vert_rounded, color: scheme.onSurfaceVariant),
              onSelected: (v) => v == 'edit' ? onEdit() : onDelete(),
              itemBuilder: (context) => [
                PopupMenuItem(
                  value: 'edit',
                  child: _menuRow(context, Icons.edit_outlined, s.edit),
                ),
                PopupMenuItem(
                  value: 'delete',
                  child: _menuRow(
                    context,
                    Icons.delete_outline_rounded,
                    s.delete,
                    color: scheme.error,
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _menuRow(BuildContext context, IconData icon, String label, {Color? color}) {
    return Row(
      children: [
        Icon(icon, size: 18, color: color ?? Theme.of(context).colorScheme.onSurfaceVariant),
        const SizedBox(width: 10),
        Text(label, style: TextStyle(fontSize: 13, color: color)),
      ],
    );
  }
}

// ── Add/Edit dialog ────────────────────────────────────────────────────────

/// Access levels the UI offers. The backend treats levels as plain integers
/// (real deployments use up to 5), so always offer 0–6 and keep any higher
/// existing value selectable.
List<int> accessLevelsUpTo(int current) {
  final top = current > 6 ? current : 6;
  return List.generate(top + 1, (i) => i);
}

class PersonFormDialog extends ConsumerStatefulWidget {
  const PersonFormDialog({super.key, this.person});

  final Person? person;

  @override
  ConsumerState<PersonFormDialog> createState() => _PersonFormDialogState();
}

class _PersonFormDialogState extends ConsumerState<PersonFormDialog> {
  late final TextEditingController _name;
  late final TextEditingController _code;
  late int _access;
  bool _saving = false;

  @override
  void initState() {
    super.initState();
    _name = TextEditingController(text: widget.person?.name ?? '');
    _code = TextEditingController(text: widget.person?.code ?? '');
    _access = widget.person?.accessLevel ?? 2;
  }

  @override
  void dispose() {
    _name.dispose();
    _code.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    final name = _name.text.trim();
    if (name.isEmpty) return;
    setState(() => _saving = true);
    try {
      final repo = ref.read(personsRepoProvider);
      final person = widget.person;
      if (person == null) {
        await repo.create(name: name, code: _code.text.trim());
      } else {
        await repo.update(person.id, name: name, code: _code.text.trim());
        if (person.accessLevel != _access) {
          await repo.setAccessLevel(person.id, _access);
        }
      }
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
    final isEdit = widget.person != null;
    return AlertDialog(
      title: Text(isEdit ? s.editPerson : s.addPerson),
      content: SizedBox(
        width: 420,
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
              child: TextField(
                controller: _code,
                onSubmitted: (_) => _save(),
              ),
            ),
            const SizedBox(height: 14),
            LabeledField(
              label: s.accessLevel,
              child: DropdownMenu<int>(
                initialSelection: _access,
                inputDecorationTheme: InputDecorationTheme(
                  filled: true,
                  fillColor: Theme.of(context).colorScheme.surfaceContainerLow,
                  contentPadding:
                      const EdgeInsets.symmetric(horizontal: 14, vertical: 13),
                  border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(10),
                    borderSide:
                        BorderSide(color: Theme.of(context).colorScheme.outlineVariant),
                  ),
                ),
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
