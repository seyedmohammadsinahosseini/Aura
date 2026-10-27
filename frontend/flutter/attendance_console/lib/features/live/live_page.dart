import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/app_strings.dart';
import '../../core/app_theme.dart';
import '../../core/formatters.dart';
import '../../data/api_client.dart' show PreviewFrame;
import '../../data/models.dart';
import '../../providers.dart';
import '../../shared/widgets.dart';
import 'preview_poller.dart';

/// Live camera wall: responsive grid of polled MJPEG-style previews.
class LivePage extends ConsumerWidget {
  const LivePage({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = S.of(context);
    final cameras = ref.watch(camerasProvider);

    return PageBody(
      maxWidth: 1500,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          PageHeader(title: s.liveTitle),
          AsyncGate(
            value: cameras,
            data: (List<Camera> list) {
              if (list.isEmpty) {
                return EmptyState(
                  icon: Icons.videocam_outlined,
                  title: s.liveNoCameras,
                  message: s.liveNoCamerasHint,
                  action: FilledButton.icon(
                    onPressed: () => context.go('/cameras'),
                    icon: const Icon(Icons.add_rounded, size: 18),
                    label: Text(s.addCamera),
                  ),
                );
              }
              return LayoutBuilder(builder: (context, constraints) {
                final w = constraints.maxWidth;
                final perRow = w > 1300 ? 3 : (w > 720 ? 2 : 1);
                return Wrap(
                  spacing: 16,
                  runSpacing: 16,
                  children: [
                    for (final cam in list)
                      SizedBox(
                        width: (w - 16 * (perRow - 1)) / perRow,
                        child: LiveTile(camera: cam),
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

// ── Live tile ──────────────────────────────────────────────────────────────

class LiveTile extends ConsumerStatefulWidget {
  const LiveTile({super.key, required this.camera, this.fps = 4});

  final Camera camera;
  final double fps;

  @override
  ConsumerState<LiveTile> createState() => _LiveTileState();
}

class _LiveTileState extends ConsumerState<LiveTile> {
  PreviewPoller? _poller;
  bool _ticking = true;

  @override
  void initState() {
    super.initState();
    if (widget.camera.streamEnabled) {
      _poller = PreviewPoller(
        api: ref.read(apiClientProvider),
        uid: widget.camera.uid,
        fps: widget.fps,
      );
      _poller!.start();
    }
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    // StatefulShellRoute keeps branches mounted; IndexedStack disables
    // tickers on hidden branches — pause polling while off-screen.
    final ticking = TickerMode.valuesOf(context).enabled;
    if (ticking != _ticking) {
      _ticking = ticking;
      final p = _poller;
      if (p != null) {
        ticking ? p.resume() : p.pause();
      }
    }
  }

  @override
  void dispose() {
    _poller?.stop();
    super.dispose();
  }

  void _enableStreaming() async {
    try {
      await ref.read(camerasRepoProvider).setStreaming(widget.camera.uid, true);
      ref.invalidate(camerasProvider);
    } catch (e) {
      if (mounted) context.showError(e);
    }
  }

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;
    final cam = widget.camera;

    final header = Padding(
      padding: const EdgeInsets.fromLTRB(12, 10, 12, 10),
      child: Row(
        children: [
          Expanded(
            child: Text(
              cam.name.isNotEmpty ? cam.name : cam.uid,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontSize: 13.5, fontWeight: FontWeight.w600),
            ),
          ),
          if (!cam.aiEnabled)
            Padding(
              padding: const EdgeInsetsDirectional.only(end: 6),
              child: Text(s.camPreviewOnly,
                  style: TextStyle(fontSize: 11.5, color: scheme.onSurfaceVariant)),
            ),
          Text(
            cam.role == CameraRole.entry
                ? s.ctEntry
                : cam.role == CameraRole.exit
                    ? s.ctExit
                    : s.ctBoth,
            style: TextStyle(fontSize: 11.5, color: scheme.onSurfaceVariant),
          ),
        ],
      ),
    );

    Widget preview;
    if (!cam.streamEnabled) {
      preview = _MessageOverlay(
        icon: Icons.videocam_off_outlined,
        message: s.liveDisabled,
        action: FilledButton.tonal(onPressed: _enableStreaming, child: Text(s.liveEnable)),
      );
    } else {
      preview = AspectRatio(
        aspectRatio: 16 / 9,
        child: LiveView(uid: cam.uid, poller: _poller),
      );
    }

    final card = Material(
      color: scheme.surface,
      borderRadius: BorderRadius.circular(14),
      clipBehavior: Clip.antiAlias,
      child: Ink(
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(14),
          border: Border.all(color: scheme.outlineVariant),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            header,
            Expanded(child: preview),
          ],
        ),
      ),
    );

    return AspectRatio(
      aspectRatio: 16 / 10.4,
      child: cam.streamEnabled
          ? InkWell(
              onTap: () => _openFullscreen(context),
              borderRadius: BorderRadius.circular(14),
              child: card,
            )
          : card,
    );
  }

  void _openFullscreen(BuildContext context) {
    final cam = widget.camera;
    Navigator.of(context).push(
      PageRouteBuilder<void>(
        opaque: false,
        barrierColor: Colors.black87,
        fullscreenDialog: true,
        transitionDuration: Motion.medium,
        reverseTransitionDuration: Motion.fast,
        pageBuilder: (context, _, _) => _FullscreenView(camera: cam),
        transitionsBuilder: (context, animation, _, child) => FadeTransition(
          opacity: animation.drive(CurveTween(curve: Motion.entry)),
          child: child,
        ),
      ),
    );
  }
}

// ── Shared frame renderer ─────────────────────────────────────────────────

class LiveView extends StatelessWidget {
  const LiveView({super.key, required this.uid, this.poller});

  final String uid;
  final PreviewPoller? poller;

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final p = poller;
    if (p == null) {
      return _MessageOverlay(
        icon: Icons.videocam_off_outlined,
        message: s.liveDisabled,
      );
    }

    return ValueListenableBuilder(
      valueListenable: p.state,
      builder: (context, state, _) {
        return ValueListenableBuilder<PreviewFrame?>(
          valueListenable: p.frames,
          builder: (context, frame, _) {
            final hasFrame = frame != null;
            final offline = state == PreviewState.offline;

            // Never streamed yet and offline → plain message.
            if (offline && !hasFrame) {
              return _MessageOverlay(
                icon: Icons.sensors_off_rounded,
                message: '${s.liveOffline} · $uid',
              );
            }
            return _LiveStack(
              frame: frame,
              offline: offline,
              offlineLabel: s.liveOffline,
              waitingLabel: s.liveWaiting,
              fpsLabel: s.liveFps,
              fa: s.isFa,
            );
          },
        );
      },
    );
  }
}

/// Layered live view: placeholder fades out, image fades in once, chips
/// crossfade — nothing pops, frames swap gaplessly on top of the stack.
class _LiveStack extends StatelessWidget {
  const _LiveStack({
    required this.frame,
    required this.offline,
    required this.offlineLabel,
    required this.waitingLabel,
    required this.fpsLabel,
    required this.fa,
  });

  final PreviewFrame? frame;
  final bool offline;
  final String offlineLabel;
  final String waitingLabel;
  final String fpsLabel;
  final bool fa;

  @override
  Widget build(BuildContext context) {
    final hasFrame = frame != null;
    final fps = frame?.fps ?? 0;
    return Stack(
      fit: StackFit.expand,
      children: [
        IgnorePointer(
          child: AnimatedOpacity(
            opacity: hasFrame ? 0.0 : 1.0,
            duration: Motion.fast,
            curve: Motion.entry,
            child: _MessageOverlay(
              icon: Icons.sensors_rounded,
              message: waitingLabel,
            ),
          ),
        ),
        AnimatedOpacity(
          opacity: hasFrame ? 1.0 : 0.0,
          duration: Motion.medium,
          curve: Motion.entry,
          child: hasFrame
              ? Image.memory(
                  frame!.jpeg,
                  fit: BoxFit.contain,
                  gaplessPlayback: true,
                  filterQuality: FilterQuality.medium,
                  errorBuilder: (_, _, _) => const SizedBox.shrink(),
                )
              : const SizedBox.shrink(),
        ),
        PositionedDirectional(
          end: 8,
          top: 8,
          child: AnimatedSwitcher(
            duration: Motion.fast,
            switchInCurve: Motion.entry,
            switchOutCurve: Curves.easeInCubic,
            transitionBuilder: (child, animation) =>
                FadeTransition(opacity: animation, child: child),
            child: offline
                ? Container(
                    key: const ValueKey('offline-chip'),
                    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                    decoration: BoxDecoration(
                      color: Colors.black.withValues(alpha: 0.6),
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Text(
                      offlineLabel,
                      style: const TextStyle(
                          fontSize: 11, color: Colors.white, fontWeight: FontWeight.w600),
                    ),
                  )
                : const SizedBox.shrink(key: ValueKey('offline-none')),
          ),
        ),
        PositionedDirectional(
          end: 8,
          bottom: 8,
          child: AnimatedSwitcher(
            duration: Motion.fast,
            switchInCurve: Motion.entry,
            switchOutCurve: Curves.easeInCubic,
            transitionBuilder: (child, animation) =>
                FadeTransition(opacity: animation, child: child),
            child: fps > 0
                ? Container(
                    key: const ValueKey('fps-chip'),
                    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                    decoration: BoxDecoration(
                      color: Colors.black.withValues(alpha: 0.55),
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Text(
                      '${num(fps.round(), fa: fa)} $fpsLabel',
                      style: const TextStyle(
                          fontSize: 11, color: Colors.white, fontWeight: FontWeight.w600),
                    ),
                  )
                : const SizedBox.shrink(key: ValueKey('fps-none')),
          ),
        ),
      ],
    );
  }
}

class _MessageOverlay extends StatelessWidget {
  const _MessageOverlay({required this.icon, required this.message, this.action});

  final IconData icon;
  final String message;
  final Widget? action;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return ColoredBox(
      color: scheme.surfaceContainerLowest,
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 30, color: scheme.outline),
            const SizedBox(height: 10),
            Text(message,
                style: TextStyle(fontSize: 13, color: scheme.onSurfaceVariant)),
            if (action != null) ...[const SizedBox(height: 12), action!],
          ],
        ),
      ),
    );
  }
}

// ── Fullscreen view ───────────────────────────────────────────────────────

class _FullscreenView extends ConsumerStatefulWidget {
  const _FullscreenView({required this.camera});

  final Camera camera;

  @override
  ConsumerState<_FullscreenView> createState() => _FullscreenViewState();
}

class _FullscreenViewState extends ConsumerState<_FullscreenView> {
  PreviewPoller? _poller;

  @override
  void initState() {
    super.initState();
    if (widget.camera.streamEnabled) {
      _poller = PreviewPoller(
        api: ref.read(apiClientProvider),
        uid: widget.camera.uid,
        fps: 10,
      );
      _poller!.start();
    }
  }

  @override
  void dispose() {
    _poller?.stop();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final s = S.of(context);
    final scheme = Theme.of(context).colorScheme;
    final cam = widget.camera;

    return Scaffold(
      backgroundColor: scheme.surfaceContainerLowest,
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        title: Text(cam.name.isNotEmpty ? cam.name : cam.uid),
        actions: [
          Padding(
            padding: const EdgeInsetsDirectional.only(end: 12),
            child: Center(
              child: Text(
                '${cam.connectionType} · ${cam.role == CameraRole.entry ? s.ctEntry : cam.role == CameraRole.exit ? s.ctExit : s.ctBoth}',
                style: TextStyle(fontSize: 12.5, color: scheme.onSurfaceVariant),
              ),
            ),
          ),
          IconButton(
            tooltip: s.close,
            icon: const Icon(Icons.close_rounded),
            onPressed: () => Navigator.of(context).pop(),
          ),
        ],
      ),
      body: Center(
        child: LiveView(uid: cam.uid, poller: _poller),
      ),
    );
  }
}
