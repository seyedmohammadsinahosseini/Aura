import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/app_strings.dart';
import '../../data/models.dart';
import '../../providers.dart';
import '../../shared/widgets.dart';

/// Settings: server connection, appearance (theme), language, about.
class SettingsPage extends ConsumerStatefulWidget {
  const SettingsPage({super.key});

  @override
  ConsumerState<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends ConsumerState<SettingsPage> {
  late final TextEditingController _url;
  bool _testing = false;

  @override
  void initState() {
    super.initState();
    _url = TextEditingController(text: ref.read(settingsProvider).serverUrl);
  }

  @override
  void dispose() {
    _url.dispose();
    super.dispose();
  }

  Future<void> _apply() async {
    final s = S.of(context);
    await ref.read(settingsProvider.notifier).setServerUrl(_url.text);
    ref.invalidate(healthProvider);
    ref.invalidate(personsProvider);
    ref.invalidate(camerasProvider);
    ref.invalidate(zonesProvider);
    ref.invalidate(unknownFacesProvider);
    if (mounted) context.showSuccess(s.connected);
  }

  Future<void> _test() async {
    setState(() => _testing = true);
    try {
      await ref.read(settingsProvider.notifier).setServerUrl(_url.text);
      ref.invalidate(healthProvider);
      final h = await ref.read(healthProvider.future);
      if (!mounted) return;
      final s = S.of(context);
      h.ok
          ? context.showSuccess('${s.connected}${h.version.isNotEmpty ? ' · ${s.backendVersion} ${h.version}' : ''}')
          : context.showError(s.notConnected);
    } catch (e) {
      if (mounted) context.showError(e);
    } finally {
      if (mounted) setState(() => _testing = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final settings = ref.watch(settingsProvider);
    final health = ref.watch(healthProvider);

    return PageBody(
      maxWidth: 760,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          PageHeader(title: s.navSettings),

          // ── Connection ───────────────────────────────────────────
          TitledPanel(
            title: s.connection,
            trailing: health.maybeWhen(
              data: (h) => StatusChip(
                label: h.ok ? s.connected : s.notConnected,
                tone: h.ok ? ChipTone.success : ChipTone.error,
              ),
              orElse: () => const SizedBox.shrink(),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                LabeledField(
                  label: s.serverUrl,
                  helper: s.serverUrlHint,
                  child: TextField(
                    controller: _url,
                    textDirection: TextDirection.ltr,
                    decoration: InputDecoration(
                      hintText: 'http://localhost:8080',
                    ),
                    onSubmitted: (_) => _apply(),
                  ),
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    FilledButton.icon(
                      onPressed: _testing ? null : _test,
                      icon: _testing
                          ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                          : const Icon(Icons.wifi_tethering_rounded, size: 18),
                      label: Text(s.testConnection),
                    ),
                    const SizedBox(width: 8),
                    TextButton(onPressed: _apply, child: Text(s.save)),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 14),

          // ── Appearance ───────────────────────────────────────────
          TitledPanel(
            title: s.appearance,
            child: Column(
              children: [
                _SettingRow(
                  label: s.themeMode,
                  child: SegmentedButton<ThemeModeOption>(
                    segments: [
                      ButtonSegment(
                        value: ThemeModeOption.system,
                        label: Text(s.tmSystem),
                        icon: const Icon(Icons.brightness_auto_outlined, size: 18),
                      ),
                      ButtonSegment(
                        value: ThemeModeOption.light,
                        label: Text(s.tmLight),
                        icon: const Icon(Icons.light_mode_outlined, size: 18),
                      ),
                      ButtonSegment(
                        value: ThemeModeOption.dark,
                        label: Text(s.tmDark),
                        icon: const Icon(Icons.dark_mode_outlined, size: 18),
                      ),
                    ],
                    selected: {settings.themeMode},
                    onSelectionChanged: (set) =>
                        ref.read(settingsProvider.notifier).setThemeMode(set.first),
                  ),
                ),
                const Divider(height: 1),
                _SettingRow(
                  label: s.language,
                  child: SegmentedButton<String>(
                    segments: [
                      ButtonSegment(value: 'fa', label: const Text('فارسی')),
                      ButtonSegment(value: 'en', label: const Text('English')),
                    ],
                    selected: {settings.localeCode},
                    onSelectionChanged: (set) =>
                        ref.read(settingsProvider.notifier).setLocale(set.first),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 14),

          // ── About ────────────────────────────────────────────────
          TitledPanel(
            title: s.about,
            child: Column(
              children: [
                _SettingRow(
                  label: s.appName,
                  child: Text(
                    health.value?.version.isNotEmpty == true
                        ? '${s.backendVersion}: ${health.value!.version}'
                        : 'FaceAuthentication Console · v1.0.0',
                    style: const TextStyle(fontSize: 13),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _SettingRow extends StatelessWidget {
  const _SettingRow({required this.label, required this.child});

  final String label;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 12),
      child: Row(
        children: [
          Expanded(child: Text(label, style: const TextStyle(fontSize: 13.5))),
          child,
        ],
      ),
    );
  }
}
