import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_web_plugins/url_strategy.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'app.dart';
import 'data/models.dart';
import 'providers.dart';
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Clean URLs (no #hash) — nginx SPA fallback is already configured.
  usePathUrlStrategy();

  // Load persisted settings before the first frame so there is no flash of
  // the wrong locale/theme.
  bootSettings = await _loadSettings();

  runApp(const ProviderScope(child: FaceConsoleApp()));
}

Future<AppSettings> _loadSettings() async {
  try {
    final prefs = await SharedPreferences.getInstance();
    final locale = prefs.getString('settings.locale');
    final themeIdx = prefs.getInt('settings.themeMode');
    final server = prefs.getString('settings.serverUrl');
    final modes = ThemeModeOption.values;
    return AppSettings(
      localeCode: locale == 'en' ? 'en' : 'fa',
      themeMode:
          themeIdx != null && themeIdx >= 0 && themeIdx < modes.length
              ? modes[themeIdx]
              : ThemeModeOption.system,
      serverUrl: server ?? '',
    );
  } catch (_) {
    return const AppSettings();
  }
}
