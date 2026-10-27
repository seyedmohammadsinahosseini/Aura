import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/app_strings.dart';
import '../../data/models.dart';
import '../../providers.dart';
import '../../shared/widgets.dart';

/// Camera management: cards with streaming/AI switches, discovery scan,
/// manual add/edit and connection testing.
class CamerasPage extends ConsumerWidget {
  const CamerasPage({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final cameras = ref.watch(camerasProvider);

    return PageBody(
      maxWidth: 1400,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          PageHeader(
            title: s.camerasTitle,
            actions: [
              OutlinedButton.icon(
                onPressed: () => _openDiscovery(context, ref),
                icon: const Icon(Icons.radar_rounded, size: 18),
                label: Text(s.scanNetwork),
              ),
              const SizedBox(width: 8),
              FilledButton.icon(
                onPressed: () => _openForm(context, ref),
                icon: const Icon(Icons.add_rounded, size: 18),
                label: Text(s.addCamera),
              ),
            ],
          ),
          AsyncGate(
            value: cameras,
            data: (List<Camera> list) {
              if (list.isEmpty) {
                return EmptyState(
                  icon: Icons.linked_camera_outlined,
                  title: s.camerasEmpty,
                  message: s.liveNoCamerasHint,
                  action: FilledButton.icon(
                    onPressed: () => _openDiscovery(context, ref),
                    icon: const Icon(Icons.radar_rounded, size: 18),
                    label: Text(s.scanNetwork),
                  ),
                );
              }
              return LayoutBuilder(builder: (context, constraints) {
                final w = constraints.maxWidth;
                final perRow = w > 1100 ? 2 : 1;
                return Wrap(
                  spacing: 14,
                  runSpacing: 14,
                  children: [
                    for (final cam in list)
                      SizedBox(
                        width: (w - 14 * (perRow - 1)) / perRow,
                        child: _CameraCard(camera: cam),
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

  Future<void> _openDiscovery(BuildContext context, WidgetRef ref) async {
    final added = await showDialog<bool>(
      context: context,
      builder: (_) => const DiscoveryDialog(),
    );
    if (added == true) ref.invalidate(camerasProvider);
  }

  Future<void> _openForm(BuildContext context, WidgetRef ref, [Camera? camera]) async {
    final saved = await showDialog<bool>(
      context: context,
      builder: (_) => CameraFormDialog(camera: camera),
    );
    if (saved == true) ref.invalidate(camerasProvider);
  }
}

// ── Camera card ────────────────────────────────────────────────────────────

class _CameraCard extends ConsumerStatefulWidget {
  const _CameraCard({required this.camera});

  final Camera camera;

  @override
  ConsumerState<_CameraCard> createState() => _CameraCardState();
}

class _CameraCardState extends ConsumerState<_CameraCard> {
  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;
    final cam = widget.camera;
    final zones = ref.watch(zonesProvider).value ?? const <Zone>[];
    final zoneName = zones.where((z) => z.id == cam.zoneId).firstOrNull?.name;

    return Panel(
      padding: const EdgeInsets.all(18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 40,
                height: 40,
                decoration: BoxDecoration(
                  color: cam.streamEnabled
                      ? scheme.primaryContainer
                      : scheme.surfaceContainer,
                  borderRadius: BorderRadius.circular(11),
                ),
                child: Icon(
                  Icons.linked_camera_outlined,
                  size: 20,
                  color: cam.streamEnabled
                      ? scheme.primary
                      : scheme.onSurfaceVariant,
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      cam.name.isNotEmpty ? cam.name : cam.uid,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontSize: 14.5, fontWeight: FontWeight.w600),
                    ),
                    const SizedBox(height: 3),
                    Text(
                      '${cam.connectionType} · ${cam.role == CameraRole.entry ? s.ctEntry : cam.role == CameraRole.exit ? s.ctExit : s.ctBoth}',
                      style: TextStyle(fontSize: 12, color: scheme.onSurfaceVariant),
                    ),
                  ],
                ),
              ),
              IconButton(
                tooltip: s.edit,
                icon: Icon(Icons.edit_outlined, size: 20, color: scheme.onSurfaceVariant),
                onPressed: () => _edit(),
              ),
              IconButton(
                tooltip: s.delete,
                icon: Icon(Icons.delete_outline_rounded, size: 20, color: scheme.error),
                onPressed: _delete,
              ),
            ],
          ),
          const SizedBox(height: 12),
          Text(
            zoneName != null && zoneName.isNotEmpty
                ? '${s.zone}: $zoneName'
                : '${s.zone}: ${s.noZone}',
            style: TextStyle(fontSize: 12.5, color: scheme.onSurfaceVariant),
          ),
          const SizedBox(height: 14),
          Row(
            children: [
              Expanded(
                child: _SwitchLine(
                  label: s.streamEnabled,
                  value: cam.streamEnabled,
                  onChanged: (v) => _toggleStream(v),
                ),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: _SwitchLine(
                  label: s.aiEnabled,
                  value: cam.aiEnabled,
                  onChanged: (v) => _toggleAi(v),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Future<void> _edit() async {
    final saved = await showDialog<bool>(
      context: context,
      builder: (_) => CameraFormDialog(camera: widget.camera),
    );
    if (saved == true) ref.invalidate(camerasProvider);
  }

  Future<void> _delete() async {
    final s = S.of(context);
    if (!await confirmDialog(
      context,
      title: s.delete,
      message: s.deleteCameraQ,
      confirmLabel: s.delete,
    )) {
      return;
    }
    try {
      await ref.read(camerasRepoProvider).delete(widget.camera.uid);
      ref.invalidate(camerasProvider);
    } catch (e) {
      if (mounted) context.showError(e);
    }
  }

  Future<void> _toggleStream(bool value) async {
    try {
      await ref.read(camerasRepoProvider).setStreaming(widget.camera.uid, value);
      ref.invalidate(camerasProvider);
    } catch (e) {
      if (mounted) context.showError(e);
    }
  }

  Future<void> _toggleAi(bool value) async {
    try {
      final cam = widget.camera;
      await ref.read(camerasRepoProvider).configure(
            Camera(
              uid: cam.uid,
              name: cam.name,
              zoneId: cam.zoneId,
              role: cam.role,
              streamEnabled: cam.streamEnabled,
              aiEnabled: value,
              connectionType: cam.connectionType,
              username: cam.username,
              password: cam.password,
              streamUri: cam.streamUri,
              encoding: cam.encoding,
              onvifXaddrs: cam.onvifXaddrs,
              transportProtocol: cam.transportProtocol,
            ),
          );
      ref.invalidate(camerasProvider);
    } catch (e) {
      if (mounted) context.showError(e);
    }
  }
}

class _SwitchLine extends StatelessWidget {
  const _SwitchLine({required this.label, required this.value, required this.onChanged});

  final String label;
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
      decoration: BoxDecoration(
        color: scheme.surfaceContainerLow,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        children: [
          Expanded(
            child: Text(label,
                style: TextStyle(fontSize: 12.5, color: scheme.onSurfaceVariant)),
          ),
          Switch(value: value, onChanged: onChanged),
        ],
      ),
    );
  }
}

// ── Add/Edit dialog ────────────────────────────────────────────────────────

class CameraFormDialog extends ConsumerStatefulWidget {
  const CameraFormDialog({super.key, this.camera, this.prefill});

  final Camera? camera;
  final DiscoveredCamera? prefill;

  @override
  ConsumerState<CameraFormDialog> createState() => _CameraFormDialogState();
}

class _CameraFormDialogState extends ConsumerState<CameraFormDialog> {
  late final TextEditingController _name;
  late final TextEditingController _uid;
  late final TextEditingController _uri;
  late final TextEditingController _user;
  late final TextEditingController _pass;
  late CameraRole _role;
  late int _zoneId;
  late String _transport;
  bool _saving = false;
  bool _testing = false;

  @override
  void initState() {
    super.initState();
    final cam = widget.camera;
    final d = widget.prefill;
    _name = TextEditingController(
      text: cam?.name ?? d?.label ?? '',
    );
    _uid = TextEditingController(text: cam?.uid ?? d?.devicePath ?? '');
    _uri = TextEditingController(
      text: cam?.streamUri ?? d?.streamUri ?? d?.mediaProfiles.firstOrNull?.streamUri ?? '',
    );
    _user = TextEditingController(text: cam?.username ?? d?.username ?? '');
    _pass = TextEditingController(text: cam?.password ?? d?.password ?? '');
    _role = cam?.role ?? CameraRole.both;
    _zoneId = cam?.zoneId ?? 0;
    _transport = cam?.transportProtocol ?? 'tcp';
  }

  @override
  void dispose() {
    _name.dispose();
    _uid.dispose();
    _uri.dispose();
    _user.dispose();
    _pass.dispose();
    super.dispose();
  }

  Future<void> _test() async {
    final s = S.of(context);
    setState(() => _testing = true);
    try {
      final r = await ref
          .read(camerasRepoProvider)
          .test(_uri.text.trim(), _user.text, _pass.text);
      if (!mounted) return;
      r.ok
          ? context.showSuccess(s.testOk)
          : context.showError(r.requiresAuth ? '${s.testFailed} — ${s.needsAuth}' : '${s.testFailed}${r.message.isEmpty ? '' : ': ${r.message}'}');
    } catch (e) {
      if (mounted) context.showError(e);
    } finally {
      if (mounted) setState(() => _testing = false);
    }
  }

  Future<void> _save() async {
    final s = S.of(context);
    if (_uid.text.trim().isEmpty || _uri.text.trim().isEmpty) return;
    setState(() => _saving = true);
    final cam = Camera(
      uid: _uid.text.trim(),
      name: _name.text.trim(),
      zoneId: _zoneId,
      role: _role,
      streamEnabled: widget.camera?.streamEnabled ?? true,
      aiEnabled: widget.camera?.aiEnabled ?? true,
      connectionType: widget.camera?.connectionType ??
          (widget.prefill?.type == 'LOCAL' ? 'LOCAL' : 'RTSP'),
      username: _user.text,
      password: _pass.text,
      streamUri: _uri.text.trim(),
      encoding: widget.camera?.encoding ?? widget.prefill?.mediaProfiles.firstOrNull?.encoding ?? '',
      onvifXaddrs: widget.camera?.onvifXaddrs ?? '',
      transportProtocol: _transport,
    );
    try {
      final repo = ref.read(camerasRepoProvider);
      if (widget.camera == null) {
        await repo.create(cam);
        if (mounted) context.showSuccess(s.cameraAdded);
      } else {
        await repo.configure(cam);
        if (mounted) context.showSuccess(s.cameraSaved);
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
    final isEdit = widget.camera != null;
    final zones = ref.watch(zonesProvider).value ?? const <Zone>[];

    return AlertDialog(
      title: Text(isEdit ? s.editCamera : s.addCamera),
      content: SizedBox(
        width: 460,
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              LabeledField(
                label: s.name,
                child: TextField(controller: _name, autofocus: !isEdit),
              ),
              const SizedBox(height: 14),
              LabeledField(
                label: 'ID (uid)',
                child: TextField(
                  controller: _uid,
                  enabled: !isEdit,
                ),
              ),
              const SizedBox(height: 14),
              LabeledField(
                label: s.streamUri,
                child: TextField(
                  controller: _uri,
                  onSubmitted: (_) => _save(),
                ),
              ),
              const SizedBox(height: 14),
              Row(
                children: [
                  Expanded(
                    child: LabeledField(
                      label: s.cameraType,
                      child: DropdownMenu(
                        initialSelection: _role,
                        onSelected: (v) =>
                            setState(() => _role = v ?? CameraRole.both),
                        dropdownMenuEntries: [
                          DropdownMenuEntry(value: CameraRole.entry, label: s.ctEntry),
                          DropdownMenuEntry(value: CameraRole.exit, label: s.ctExit),
                          DropdownMenuEntry(value: CameraRole.both, label: s.ctBoth),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: LabeledField(
                      label: s.zone,
                      child: DropdownMenu(
                        initialSelection: _zoneId,
                        onSelected: (v) => setState(() => _zoneId = v as int),
                        dropdownMenuEntries: [
                          DropdownMenuEntry(value: 0, label: s.noZone),
                          for (final z in zones)
                            DropdownMenuEntry(value: z.id, label: z.name),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 14),
              Row(
                children: [
                  Expanded(
                    child: LabeledField(
                      label: s.username,
                      child: TextField(controller: _user),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: LabeledField(
                      label: s.password,
                      child: TextField(
                        controller: _pass,
                        obscureText: true,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 14),
              LabeledField(
                label: s.transport,
                child: DropdownMenu(
                  initialSelection: _transport,
                  onSelected: (v) => setState(() => _transport = v ?? 'tcp'),
                  dropdownMenuEntries: const [
                    DropdownMenuEntry(value: 'tcp', label: 'TCP'),
                    DropdownMenuEntry(value: 'udp', label: 'UDP'),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          onPressed: _testing ? null : _test,
          child: _testing
              ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
              : Text(s.testConnection),
        ),
        TextButton(
          onPressed: () => Navigator.pop(context, false),
          child: Text(s.cancel),
        ),
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

// ── Discovery dialog ──────────────────────────────────────────────────────

class DiscoveryDialog extends ConsumerStatefulWidget {
  const DiscoveryDialog({super.key});

  @override
  ConsumerState<DiscoveryDialog> createState() => _DiscoveryDialogState();
}

class _DiscoveryDialogState extends ConsumerState<DiscoveryDialog> {
  bool _scanning = false;
  bool _deep = false;
  List<DiscoveredCamera> _results = const [];

  Future<void> _scan() async {
    setState(() => _scanning = true);
    try {
      final results = await ref.read(camerasRepoProvider).discover(deep: _deep);
      if (mounted) setState(() => _results = results);
    } catch (e) {
      if (mounted) context.showError(e);
    } finally {
      if (mounted) setState(() => _scanning = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;

    return AlertDialog(
      title: Text(s.scanNetwork),
      content: SizedBox(
        width: 480,
        height: 420,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Expanded(
                  child: CheckboxListTile(
                    value: _deep,
                    onChanged: (v) => setState(() => _deep = v ?? false),
                    title: Text(s.deepScan, style: const TextStyle(fontSize: 13)),
                    dense: true,
                    controlAffinity: ListTileControlAffinity.leading,
                    contentPadding: EdgeInsets.zero,
                  ),
                ),
                FilledButton.icon(
                  onPressed: _scanning ? null : _scan,
                  icon: _scanning
                      ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                      : const Icon(Icons.radar_rounded, size: 18),
                  label: Text(s.scanNetwork),
                ),
              ],
            ),
            if (_scanning) ...[
              const SizedBox(height: 8),
              Text(s.scanHint,
                  style: TextStyle(fontSize: 12, color: scheme.onSurfaceVariant)),
            ],
            const Divider(height: 24),
            Expanded(
              child: _scanning
                  ? Center(
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          const CircularProgressIndicator(strokeWidth: 2.5),
                          const SizedBox(height: 14),
                          Text(s.scanning,
                              style: TextStyle(
                                  fontSize: 13, color: scheme.onSurfaceVariant)),
                        ],
                      ),
                    )
                  : _results.isEmpty
                      ? Center(
                          child: Text(s.noResults,
                              style: TextStyle(color: scheme.onSurfaceVariant)),
                        )
                      : ListView.builder(
                          itemCount: _results.length,
                          itemBuilder: (context, i) {
                            final d = _results[i];
                            return ListTile(
                              leading: Icon(
                                d.type == 'LOCAL'
                                    ? Icons.videocam_rounded
                                    : d.type == 'ONVIF'
                                        ? Icons.lan_outlined
                                        : Icons.wifi_tethering_rounded,
                                color: scheme.primary,
                              ),
                              title: Text(d.label,
                                  overflow: TextOverflow.ellipsis,
                                  style: const TextStyle(fontSize: 13.5)),
                              subtitle: Text(
                                d.type == 'LOCAL'
                                    ? d.devicePath
                                    : d.streamUri,
                                overflow: TextOverflow.ellipsis,
                                style: TextStyle(
                                    fontSize: 11.5, color: scheme.onSurfaceVariant),
                              ),
                              trailing: d.requiresAuth
                                  ? Tooltip(
                                      message: s.needsAuth,
                                      child: Icon(Icons.key_rounded,
                                          size: 16, color: scheme.onSurfaceVariant),
                                    )
                                  : null,
                              onTap: () async {
                                final saved = await showDialog<bool>(
                                  context: context,
                                  builder: (_) => CameraFormDialog(prefill: d),
                                );
                                if (saved == true && context.mounted) {
                                  Navigator.pop(context, true);
                                }
                              },
                            );
                          },
                        ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context, false),
          child: Text(s.close),
        ),
      ],
    );
  }
}
