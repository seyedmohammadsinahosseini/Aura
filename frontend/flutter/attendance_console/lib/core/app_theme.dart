/// Design tokens for the console.
///
/// The palette follows the neutral "one accent" pattern used by modern SaaS
/// dashboards (Vercel/Linear/Stripe): neutral zinc surfaces in both modes,
/// a single indigo accent, and color reserved for *state* only
/// (green=ok, amber=warning, red=error, indigo=interactive/brand).
library;

import 'package:flutter/material.dart';
/// Semantic colors that don't exist in [ColorScheme].
class SemanticColors {
  const SemanticColors({
    required this.success,
    required this.onSuccess,
    required this.successContainer,
    required this.onSuccessContainer,
    required this.warning,
    required this.onWarning,
    required this.warningContainer,
    required this.onWarningContainer,
  });

  final Color success;
  final Color onSuccess;
  final Color successContainer;
  final Color onSuccessContainer;
  final Color warning;
  final Color onWarning;
  final Color warningContainer;
  final Color onWarningContainer;

  static const light = SemanticColors(
    success: Color(0xFF16A34A),
    onSuccess: Colors.white,
    successContainer: Color(0xFFDCFCE7),
    onSuccessContainer: Color(0xFF14532D),
    warning: Color(0xFFD97706),
    onWarning: Colors.white,
    warningContainer: Color(0xFFFEF3C7),
    onWarningContainer: Color(0xFF78350F),
  );

  static const dark = SemanticColors(
    success: Color(0xFF4ADE80),
    onSuccess: Color(0xFF052E16),
    successContainer: Color(0xFF14532D),
    onSuccessContainer: Color(0xFFDCFCE7),
    warning: Color(0xFFFBBF24),
    onWarning: Color(0xFF451A03),
    warningContainer: Color(0xFF78350F),
    onWarningContainer: Color(0xFFFEF3C7),
  );

  static SemanticColors of(BuildContext context) {
    return Theme.of(context).brightness == Brightness.dark
        ? SemanticColors.dark
        : SemanticColors.light;
  }
}

const _fontFamily = 'Vazirmatn';

/// Motion tokens (Material 3 "easing and duration" spec):
/// small UI changes ≈100–200 ms, container/content transitions ≈250–400 ms,
/// entering elements use a *decelerate* curve so they land softly.
/// All animations here are implicit — they retarget mid-flight instead of
/// being cut off when state changes quickly.
abstract final class Motion {
  /// Micro: color/selection changes.
  static const Duration fast = Duration(milliseconds: 180);

  /// Content crossfades, overlays.
  static const Duration medium = Duration(milliseconds: 250);

  /// Route transitions.
  static const Duration slow = Duration(milliseconds: 300);

  /// Chart data morphs.
  static const Duration chart = Duration(milliseconds: 400);

  /// Entrances decelerate to a soft stop.
  static const Curve entry = Curves.easeOutCubic;

  /// Roomy, emphasized movement (M3 emphasized feel).
  static const Curve emphasized = Curves.easeInOutCubicEmphasized;
}

ColorScheme _lightScheme() {
  const base = ColorScheme.light(
    primary: Color(0xFF4F46E5), // indigo-600
    onPrimary: Colors.white,
    primaryContainer: Color(0xFFEEF2FF), // indigo-50
    onPrimaryContainer: Color(0xFF3730A3), // indigo-800
    secondary: Color(0xFF52525B),
    onSecondary: Colors.white,
    secondaryContainer: Color(0xFFF4F4F5),
    onSecondaryContainer: Color(0xFF27272A),
    surface: Colors.white,
    onSurface: Color(0xFF18181B), // zinc-900
    surfaceContainerLowest: Colors.white,
    surfaceContainerLow: Color(0xFFFAFAFA),
    surfaceContainer: Color(0xFFF4F4F5), // zinc-100
    surfaceContainerHigh: Color(0xFFE4E4E7), // zinc-200
    onSurfaceVariant: Color(0xFF52525B), // zinc-600
    outline: Color(0xFFA1A1AA), // zinc-400
    outlineVariant: Color(0xFFE4E4E7), // zinc-200
    error: Color(0xFFDC2626),
    onError: Colors.white,
  );
  return base;
}

ColorScheme _darkScheme() {
  return const ColorScheme.dark(
    primary: Color(0xFF818CF8), // indigo-400
    onPrimary: Color(0xFF1E1B4B),
    primaryContainer: Color(0xFF3730A3), // indigo-800
    onPrimaryContainer: Color(0xFFE0E7FF),
    secondary: Color(0xFFA1A1AA),
    onSecondary: Color(0xFF18181B),
    secondaryContainer: Color(0xFF27272A),
    onSecondaryContainer: Color(0xFFE4E4E7),
    surface: Color(0xFF101013),
    onSurface: Color(0xFFFAFAFA), // zinc-50
    surfaceContainerLowest: Color(0xFF09090B), // zinc-950 (scaffold)
    surfaceContainerLow: Color(0xFF101013),
    surfaceContainer: Color(0xFF18181B), // zinc-900 (cards)
    surfaceContainerHigh: Color(0xFF27272A), // zinc-800
    onSurfaceVariant: Color(0xFFA1A1AA), // zinc-400
    outline: Color(0xFF52525B), // zinc-600
    outlineVariant: Color(0xFF27272A), // zinc-800 (borders)
    error: Color(0xFFF87171),
    onError: Color(0xFF450A0A),
  );
}

ThemeData buildTheme(Brightness brightness) {
  final scheme =
      brightness == Brightness.dark ? _darkScheme() : _lightScheme();
  final isDark = brightness == Brightness.dark;

  // Scaffold uses the darkest neutral; cards sit one step higher.
  final scaffold = isDark
      ? scheme.surfaceContainerLowest
      : const Color(0xFFF7F7F8);

  OutlineInputBorder border(Color c, [double w = 1]) =>
      OutlineInputBorder(borderRadius: BorderRadius.circular(10), borderSide: BorderSide(color: c, width: w));

  return ThemeData(
    useMaterial3: true,
    colorScheme: scheme,
    fontFamily: _fontFamily,
    scaffoldBackgroundColor: scaffold,
    splashFactory: InkSparkle.splashFactory,

    // Text: three sizes only (label 12.5 / body 14 / headline 22) — visual
    // hierarchy from weight & color, not from a dozen sizes.
    textTheme: const TextTheme(
      headlineSmall: TextStyle(fontSize: 22, fontWeight: FontWeight.w700, height: 1.3),
      titleLarge: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, height: 1.4),
      titleMedium: TextStyle(fontSize: 13.5, fontWeight: FontWeight.w600, height: 1.4),
      titleSmall: TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600, height: 1.4),
      bodyLarge: TextStyle(fontSize: 14.5, height: 1.6),
      bodyMedium: TextStyle(fontSize: 13.5, height: 1.55),
      bodySmall: TextStyle(fontSize: 12, height: 1.5),
      labelLarge: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, height: 1.2),
      labelMedium: TextStyle(fontSize: 12, fontWeight: FontWeight.w500, height: 1.2),
      labelSmall: TextStyle(fontSize: 11, fontWeight: FontWeight.w500, height: 1.2),
    ),

    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        minimumSize: const Size(64, 42),
        padding: const EdgeInsets.symmetric(horizontal: 16),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        textStyle: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        minimumSize: const Size(64, 42),
        padding: const EdgeInsets.symmetric(horizontal: 16),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        side: BorderSide(color: scheme.outlineVariant, width: 1),
        textStyle: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600),
      ),
    ),
    textButtonTheme: TextButtonThemeData(
      style: TextButton.styleFrom(
        minimumSize: const Size(48, 40),
        padding: const EdgeInsets.symmetric(horizontal: 12),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        textStyle: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600),
      ),
    ),
    iconButtonTheme: IconButtonThemeData(
      style: IconButton.styleFrom(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
      ),
    ),

    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: isDark ? scheme.surfaceContainerLow : Colors.white,
      contentPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
      border: border(scheme.outlineVariant),
      enabledBorder: border(scheme.outlineVariant),
      focusedBorder: border(scheme.primary, 1.6),
      errorBorder: border(scheme.error),
      focusedErrorBorder: border(scheme.error, 1.6),
      hintStyle: TextStyle(color: scheme.onSurfaceVariant.withValues(alpha: .7), fontSize: 13.5),
      labelStyle: TextStyle(color: scheme.onSurfaceVariant, fontSize: 13.5),
      floatingLabelStyle: TextStyle(color: scheme.primary, fontSize: 13, fontWeight: FontWeight.w600),
    ),

    dropdownMenuTheme: DropdownMenuThemeData(
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: isDark ? scheme.surfaceContainerLow : Colors.white,
        contentPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 13),
        border: border(scheme.outlineVariant),
        enabledBorder: border(scheme.outlineVariant),
      ),
      menuStyle: MenuStyle(
        backgroundColor: WidgetStatePropertyAll(
            isDark ? scheme.surfaceContainerHigh : Colors.white),
        shape: const WidgetStatePropertyAll(
            RoundedRectangleBorder(borderRadius: BorderRadius.all(Radius.circular(12)))),
        side: WidgetStatePropertyAll(BorderSide(color: scheme.outlineVariant)),
        elevation: const WidgetStatePropertyAll(8),
      ),
    ),

    switchTheme: SwitchThemeData(
      thumbColor: WidgetStateProperty.resolveWith((s) =>
          s.contains(WidgetState.selected) ? scheme.onPrimary : scheme.surfaceContainerHigh),
      trackColor: WidgetStateProperty.resolveWith((s) =>
          s.contains(WidgetState.selected) ? scheme.primary : scheme.outlineVariant),
      trackOutlineColor: const WidgetStatePropertyAll(Colors.transparent),
    ),

    checkboxTheme: CheckboxThemeData(
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(5)),
      side: BorderSide(color: scheme.outline, width: 1.4),
    ),

    dialogTheme: DialogThemeData(
      backgroundColor: isDark ? scheme.surfaceContainerLow : Colors.white,
      surfaceTintColor: Colors.transparent,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
      titleTextStyle: TextStyle(
        fontSize: 16,
        fontWeight: FontWeight.w700,
        color: scheme.onSurface,
      ),
      contentTextStyle: TextStyle(
        fontSize: 13.5,
        height: 1.6,
        color: scheme.onSurfaceVariant,
      ),
    ),

    snackBarTheme: SnackBarThemeData(
      behavior: SnackBarBehavior.floating,
      backgroundColor: isDark ? scheme.surfaceContainerHigh : const Color(0xFF18181B),
      contentTextStyle: TextStyle(
        color: isDark ? scheme.onSurface : Colors.white,
        fontSize: 13,
        fontWeight: FontWeight.w500,
      ),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
    ),

    dividerTheme: DividerThemeData(
      color: scheme.outlineVariant,
      thickness: 1,
      space: 1,
    ),

    tooltipTheme: TooltipThemeData(
      decoration: BoxDecoration(
        color: isDark ? scheme.surfaceContainerHigh : const Color(0xFF18181B),
        borderRadius: BorderRadius.circular(8),
      ),
      textStyle: TextStyle(
        color: isDark ? scheme.onSurface : Colors.white,
        fontSize: 12,
      ),
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      waitDuration: const Duration(milliseconds: 500),
    ),

    progressIndicatorTheme: ProgressIndicatorThemeData(
      color: scheme.primary,
      linearTrackColor: scheme.surfaceContainerHigh,
      circularTrackColor: scheme.surfaceContainerHigh,
    ),

    dataTableTheme: DataTableThemeData(
      headingTextStyle: TextStyle(
        fontSize: 12,
        fontWeight: FontWeight.w600,
        color: scheme.onSurfaceVariant,
      ),
      dataTextStyle: TextStyle(fontSize: 13, color: scheme.onSurface),
      headingRowHeight: 44,
      dataRowMinHeight: 52,
      dataRowMaxHeight: 60,
      headingRowColor: WidgetStatePropertyAll(
          isDark ? scheme.surfaceContainerLowest : scheme.surfaceContainerLow),
      horizontalMargin: 16,
    ),

    menuTheme: MenuThemeData(
      style: MenuStyle(
        backgroundColor: WidgetStatePropertyAll(
            isDark ? scheme.surfaceContainerHigh : Colors.white),
        shape: const WidgetStatePropertyAll(
            RoundedRectangleBorder(borderRadius: BorderRadius.all(Radius.circular(12)))),
        side: WidgetStatePropertyAll(BorderSide(color: scheme.outlineVariant)),
        elevation: const WidgetStatePropertyAll(8),
      ),
    ),

    popupMenuTheme: PopupMenuThemeData(
      color: isDark ? scheme.surfaceContainerHigh : Colors.white,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      textStyle: TextStyle(fontSize: 13, color: scheme.onSurface),
      position: PopupMenuPosition.under,
    ),
  );
}
