import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../core/app_strings.dart';
import '../core/app_theme.dart';
import '../core/formatters.dart';
import '../data/api_client.dart';
import '../providers.dart';

// ── Page scaffolding ──────────────────────────────────────────────────────

/// Standard page wrapper: consistent padding, centered max-width column.
class PageBody extends StatelessWidget {
  const PageBody({super.key, required this.child, this.maxWidth = 1280});

  final Widget child;
  final double maxWidth;

  @override
  Widget build(BuildContext context) {
    return Align(
      alignment: Alignment.topCenter,
      child: ConstrainedBox(
        constraints: BoxConstraints(maxWidth: maxWidth),
        child: Scrollbar(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(24, 20, 24, 40),
            child: child,
          ),
        ),
      ),
    );
  }
}

/// Page header: hamburger (narrow) / title / subtitle / actions.
class PageHeader extends StatelessWidget {
  const PageHeader({
    super.key,
    required this.title,
    this.subtitle,
    this.actions = const [],
  });

  final String title;
  final String? subtitle;
  final List<Widget> actions;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 20),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          if (MediaQuery.of(context).size.width < 760) ...[
            IconButton(
              tooltip: MaterialLocalizations.of(context).openAppDrawerTooltip,
              icon: const Icon(Icons.menu_rounded),
              onPressed: () => Scaffold.of(context).openDrawer(),
            ),
            const SizedBox(width: 4),
          ],
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title, style: Theme.of(context).textTheme.headlineSmall),
                if (subtitle != null) ...[
                  const SizedBox(height: 4),
                  Text(
                    subtitle!,
                    style: Theme.of(context)
                        .textTheme
                        .bodySmall
                        ?.copyWith(color: Theme.of(context).colorScheme.onSurfaceVariant),
                  ),
                ],
              ],
            ),
          ),
          ...actions,
        ],
      ),
    );
  }
}

// ── Panel (the card primitive) ────────────────────────────────────────────

class Panel extends StatelessWidget {
  const Panel({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(20),
    this.onTap,
  });

  final Widget child;
  final EdgeInsetsGeometry padding;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final body = Padding(padding: padding, child: child);
    return Material(
      color: scheme.surface,
      borderRadius: BorderRadius.circular(14),
      clipBehavior: Clip.antiAlias,
      child: Ink(
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(14),
          border: Border.all(color: scheme.outlineVariant),
        ),
        child: onTap != null ? InkWell(onTap: onTap, child: body) : body,
      ),
    );
  }
}

/// Panel with a title row.
class TitledPanel extends StatelessWidget {
  const TitledPanel({
    super.key,
    required this.title,
    required this.child,
    this.trailing,
    this.padding = const EdgeInsets.all(20),
  });

  final String title;
  final Widget child;
  final Widget? trailing;
  final EdgeInsetsGeometry padding;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Panel(
      padding: padding,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: Text(
                  title,
                  style: Theme.of(context)
                      .textTheme
                      .titleMedium
                      ?.copyWith(color: scheme.onSurface),
                ),
              ),
              ?trailing,
            ],
          ),
          const SizedBox(height: 14),
          child,
        ],
      ),
    );
  }
}

// ── Stat card ──────────────────────────────────────────────────────────────

enum StatTone { neutral, success, warning, error, primary }

class StatCard extends StatelessWidget {
  const StatCard({
    super.key,
    required this.icon,
    required this.value,
    required this.label,
    this.tone = StatTone.neutral,
    this.onTap,
  });

  final IconData icon;
  final String value;
  final String label;
  final StatTone tone;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final sem = SemanticColors.of(context);

    final (Color fg, Color bg) = switch (tone) {
      StatTone.neutral => (scheme.onSurfaceVariant, scheme.surfaceContainer),
      StatTone.primary => (scheme.primary, scheme.primaryContainer),
      StatTone.success => (sem.success, sem.successContainer),
      StatTone.warning => (sem.warning, sem.warningContainer),
      StatTone.error => (scheme.error, scheme.errorContainer),
    };

    return Panel(
      onTap: onTap,
      padding: const EdgeInsets.all(18),
      child: Row(
        children: [
          Container(
            width: 42,
            height: 42,
            decoration:
                BoxDecoration(color: bg, borderRadius: BorderRadius.circular(12)),
            child: Icon(icon, size: 22, color: fg),
          ),
          const SizedBox(width: 14),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(value,
                    style: const TextStyle(
                        fontSize: 24, fontWeight: FontWeight.w700, height: 1.1)),
                const SizedBox(height: 3),
                Text(
                  label,
                  style: Theme.of(context)
                      .textTheme
                      .bodySmall
                      ?.copyWith(color: scheme.onSurfaceVariant),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

// ── Status chip ───────────────────────────────────────────────────────────

enum ChipTone { neutral, success, warning, error, primary }

class StatusChip extends StatelessWidget {
  const StatusChip({super.key, required this.label, this.tone = ChipTone.neutral, this.icon});

  final String label;
  final ChipTone tone;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final sem = SemanticColors.of(context);

    final (Color fg, Color bg) = switch (tone) {
      ChipTone.neutral => (scheme.onSurfaceVariant, scheme.surfaceContainer),
      ChipTone.primary => (scheme.primary, scheme.primaryContainer),
      ChipTone.success => (sem.success, sem.successContainer),
      ChipTone.warning => (sem.warning, sem.warningContainer),
      ChipTone.error => (scheme.error, scheme.errorContainer),
    };

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
      decoration:
          BoxDecoration(color: bg, borderRadius: BorderRadius.circular(999)),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (icon != null) ...[
            Icon(icon, size: 13, color: fg),
            const SizedBox(width: 4),
          ],
          Text(label,
              style: TextStyle(
                  fontSize: 12, fontWeight: FontWeight.w600, color: fg)),
        ],
      ),
    );
  }
}

// ── Async wrapper ─────────────────────────────────────────────────────────

/// Renders loading / error / data states of an [AsyncValue] consistently.
///
/// State swaps crossfade instead of popping (M3 fade-through feel).
class AsyncGate<T> extends StatelessWidget {
  const AsyncGate({super.key, required this.value, required this.data});

  final AsyncValue<T> value;
  final Widget Function(T data) data;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final (key, child) = switch (value) {
      AsyncData(:final value) => ('data', data(value)),
      AsyncError(:final error) => ('error', _ErrorPane(error: error)),
      _ => (
          'loading',
          Center(
            child: Padding(
              padding: const EdgeInsets.symmetric(vertical: 48),
              child: SizedBox(
                width: 26,
                height: 26,
                child: CircularProgressIndicator(strokeWidth: 2.6, color: scheme.outline),
              ),
            ),
          )
        ),
    };
    return AnimatedSwitcher(
      duration: Motion.medium,
      switchInCurve: Motion.entry,
      switchOutCurve: Curves.easeInCubic,
      // Top-start + passthrough: outgoing content keeps its place (no jump
      // to center) and stretched children keep stretching while fading.
      layoutBuilder: (currentChild, previousChildren) => Stack(
        alignment: AlignmentDirectional.topStart,
        fit: StackFit.passthrough,
        children: [...previousChildren, ?currentChild],
      ),
      child: KeyedSubtree(key: ValueKey(key), child: child),
    );
  }
}

class _ErrorPane extends StatelessWidget {
  const _ErrorPane({required this.error});

  final Object error;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final s = S.of(context);
    final msg = error is ApiException ? error.toString() : null;
    return Center(
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 40, horizontal: 20),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.cloud_off_rounded, size: 36, color: scheme.outline),
            const SizedBox(height: 12),
            Text(
              msg ?? s.serverUnreachable,
              textAlign: TextAlign.center,
              style: Theme.of(context)
                  .textTheme
                  .bodyMedium
                  ?.copyWith(color: scheme.onSurfaceVariant),
            ),
          ],
        ),
      ),
    );
  }
}

// ── Empty state ────────────────────────────────────────────────────────────

class EmptyState extends StatelessWidget {
  const EmptyState({
    super.key,
    required this.icon,
    required this.title,
    required this.message,
    this.action,
  });

  final IconData icon;
  final String title;
  final String message;
  final Widget? action;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Center(
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 48, horizontal: 24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 56,
              height: 56,
              decoration: BoxDecoration(
                color: scheme.surfaceContainer,
                borderRadius: BorderRadius.circular(16),
              ),
              child: Icon(icon, size: 28, color: scheme.onSurfaceVariant),
            ),
            const SizedBox(height: 16),
            Text(title, style: Theme.of(context).textTheme.titleLarge),
            const SizedBox(height: 6),
            ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 360),
              child: Text(
                message,
                textAlign: TextAlign.center,
                style: Theme.of(context)
                    .textTheme
                    .bodySmall
                    ?.copyWith(color: scheme.onSurfaceVariant),
              ),
            ),
            if (action != null) ...[const SizedBox(height: 18), action!],
          ],
        ),
      ),
    );
  }
}

// ── Person avatar ─────────────────────────────────────────────────────────

class PersonAvatar extends ConsumerWidget {
  const PersonAvatar({
    super.key,
    required this.personId,
    required this.name,
    this.size = 40,
  });

  final int personId;
  final String name;
  final double size;

  String get _initials {
    final parts =
        name.trim().split(RegExp(r'\s+')).where((p) => p.isNotEmpty).toList();
    if (parts.isEmpty) return '?';
    if (parts.length == 1) return parts.first.substring(0, 1);
    return '${parts.first.substring(0, 1)}${parts.last.substring(0, 1)}';
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final scheme = Theme.of(context).colorScheme;
    final pic = ref.watch(personPictureProvider(personId));

    final fallback = Container(
      width: size,
      height: size,
      decoration: BoxDecoration(
        color: scheme.primaryContainer,
        borderRadius: BorderRadius.circular(size / 2.6),
      ),
      alignment: Alignment.center,
      child: Text(
        _initials,
        style: TextStyle(
          fontSize: size * .34,
          fontWeight: FontWeight.w600,
          color: scheme.onPrimaryContainer,
        ),
      ),
    );

    return pic.when(
      data: (bytes) {
        final Widget? photo = bytes == null || bytes.isEmpty
            ? null
            : ClipRRect(
                borderRadius: BorderRadius.circular(size / 2.6),
                child: TweenAnimationBuilder<double>(
                  // One-shot soft fade-in when the picture arrives.
                  tween: Tween(begin: 0, end: 1),
                  duration: Motion.medium,
                  curve: Motion.entry,
                  builder: (context, opacity, child) => Opacity(
                    opacity: opacity,
                    child: child,
                  ),
                  child: Image.memory(
                    bytes,
                    width: size,
                    height: size,
                    fit: BoxFit.cover,
                    gaplessPlayback: true,
                    errorBuilder: (_, _, _) => const SizedBox.shrink(),
                  ),
                ),
              );
        return AnimatedSwitcher(
          duration: Motion.fast,
          switchInCurve: Motion.entry,
          child: photo ?? KeyedSubtree(key: const ValueKey('fallback'), child: fallback),
        );
      },
      loading: () => fallback,
      error: (_, _) => fallback,
    );
  }
}

// ── Date navigator ────────────────────────────────────────────────────────

/// Chevrons + long (Jalali/Gregorian) date label + "today" reset.
///
/// The label slides in the travel direction (M3 shared-axis feel) instead
/// of snapping to the new date.
class DateNavigator extends ConsumerStatefulWidget {
  const DateNavigator({
    super.key,
    required this.date,
    required this.onChanged,
    this.showToday = true,
  });

  final DateTime date;
  final ValueChanged<DateTime> onChanged;
  final bool showToday;

  @override
  ConsumerState<DateNavigator> createState() => _DateNavigatorState();
}

class _DateNavigatorState extends ConsumerState<DateNavigator> {
  int _direction = 1;

  void _go(int days) {
    setState(() => _direction = days);
    widget.onChanged(
      DateTime(widget.date.year, widget.date.month, widget.date.day + days),
    );
  }

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final fa = s.isFa;
    final isToday = dateKey(widget.date) == dateKey(DateTime.now());
    // In RTL the visual direction flips: "next" (future) is to the left.
    final dx = (fa ? -_direction : _direction) * 0.35;

    return Row(
      children: [
        IconButton(
          tooltip: MaterialLocalizations.of(context).previousPageTooltip,
          icon: Icon(fa ? Icons.chevron_right_rounded : Icons.chevron_left_rounded),
          onPressed: () => _go(-1),
        ),
        Expanded(
          child: Center(
            child: AnimatedSwitcher(
              duration: Motion.fast,
              switchInCurve: Motion.entry,
              switchOutCurve: Curves.easeInCubic,
              transitionBuilder: (child, animation) => FadeTransition(
                opacity: animation,
                child: SlideTransition(
                  position: animation.drive(
                    Tween<Offset>(begin: Offset(dx, 0), end: Offset.zero),
                  ),
                  child: child,
                ),
              ),
              layoutBuilder: (currentChild, previousChildren) => Stack(
                alignment: AlignmentDirectional.center,
                children: [...previousChildren, ?currentChild],
              ),
              child: Text(
                longDate(widget.date, fa: fa),
                key: ValueKey(dateKey(widget.date)),
                style: Theme.of(context).textTheme.titleMedium,
              ),
            ),
          ),
        ),
        IconButton(
          tooltip: MaterialLocalizations.of(context).nextPageTooltip,
          icon: Icon(fa ? Icons.chevron_left_rounded : Icons.chevron_right_rounded),
          onPressed: () => _go(1),
        ),
        if (widget.showToday && !isToday) ...[
          const SizedBox(width: 6),
          OutlinedButton(
            onPressed: () {
              setState(() => _direction = 1);
              widget.onChanged(DateTime.now());
            },
            child: Text(s.today),
          ),
        ],
      ],
    );
  }
}

// ── Dialogs & toasts ───────────────────────────────────────────────────────

Future<bool> confirmDialog(
  BuildContext context, {
  required String title,
  required String message,
  String? confirmLabel,
  bool danger = true,
}) async {
  final s = S.of(context);
  final result = await showDialog<bool>(
    context: context,
    builder: (context) => AlertDialog(
      title: Text(title),
      content: Text(message),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context, false),
          child: Text(s.cancel),
        ),
        FilledButton(
          style: danger
              ? FilledButton.styleFrom(
                  backgroundColor: Theme.of(context).colorScheme.error)
              : null,
          onPressed: () => Navigator.pop(context, true),
          child: Text(confirmLabel ?? s.confirm),
        ),
      ],
    ),
  );
  return result ?? false;
}

extension AppContextX on BuildContext {
  void showError(Object error) {
    final messenger = ScaffoldMessenger.maybeOf(this);
    if (messenger == null) return;
    messenger.showSnackBar(SnackBar(
      content: Text(error.toString()),
      behavior: SnackBarBehavior.floating,
      width: 420,
    ));
  }

  void showSuccess(String message) {
    final messenger = ScaffoldMessenger.maybeOf(this);
    if (messenger == null) return;
    messenger.showSnackBar(SnackBar(
      content: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Icon(Icons.check_circle_rounded, size: 18, color: Color(0xFF4ADE80)),
          const SizedBox(width: 10),
          Flexible(child: Text(message)),
        ],
      ),
      behavior: SnackBarBehavior.floating,
      width: 420,
    ));
  }
}

// ── Labeled field for forms ───────────────────────────────────────────────

class LabeledField extends StatelessWidget {
  const LabeledField({
    super.key,
    required this.label,
    required this.child,
    this.helper,
  });

  final String label;
  final Widget child;
  final String? helper;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: Theme.of(context)
              .textTheme
              .labelMedium
              ?.copyWith(color: scheme.onSurfaceVariant),
        ),
        const SizedBox(height: 7),
        child,
        if (helper != null) ...[
          const SizedBox(height: 6),
          Text(
            helper!,
            style: Theme.of(context)
                .textTheme
                .labelSmall
                ?.copyWith(color: scheme.onSurfaceVariant.withValues(alpha: .8)),
          ),
        ],
      ],
    );
  }
}
