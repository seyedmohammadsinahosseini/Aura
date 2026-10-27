import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'core/app_theme.dart';
import 'core/router.dart';
import 'data/models.dart';
import 'providers.dart';

/// Root widget: theme + locale wiring and the router.
class FaceConsoleApp extends ConsumerWidget {
  const FaceConsoleApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final settings = ref.watch(settingsProvider);
    final isFa = settings.localeCode == 'fa';
    final locale = isFa ? const Locale('fa') : const Locale('en');

    final themeMode = switch (settings.themeMode) {
      ThemeModeOption.system => ThemeMode.system,
      ThemeModeOption.light => ThemeMode.light,
      ThemeModeOption.dark => ThemeMode.dark,
    };

    return MaterialApp.router(
      title: 'FaceAuthentication',
      debugShowCheckedModeBanner: false,
      theme: buildTheme(Brightness.light),
      darkTheme: buildTheme(Brightness.dark),
      themeMode: themeMode,
      locale: locale,
      supportedLocales: const [Locale('fa'), Locale('en')],
      localizationsDelegates: const [
        GlobalMaterialLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
      ],
      routerConfig: ref.watch(routerProvider),
    );
  }
}
