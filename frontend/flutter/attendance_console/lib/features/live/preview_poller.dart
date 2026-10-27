import 'dart:async';

import 'package:flutter/foundation.dart';

import '../../data/api_client.dart';

enum PreviewState { waiting, live, offline }

/// Polls `/api/preview/{uid}/jpeg` for a camera's live annotated frames.
///
/// Backend contract (verified in `CameraPreviewHub.cpp`):
/// 1. Frames are published **only while at least one subscriber exists** —
///    so we subscribe before polling and unsubscribe on stop.
/// 2. The endpoint returns the latest JPEG (404 while no frame is available).
///
/// Render with `Image.memory(bytes, gaplessPlayback: true)` to swap frames
/// without flicker (the standard MJPEG-style pattern in Flutter).
class PreviewPoller {
  PreviewPoller({required ApiClient api, required this.uid, this.fps = 4})
      : _api = api;

  final ApiClient _api;
  final String uid;
  final double fps;

  final frames = ValueNotifier<PreviewFrame?>(null);
  final state = ValueNotifier<PreviewState>(PreviewState.waiting);

  bool _disposed = false;
  bool _paused = false;
  bool _started = false;
  int _misses = 0;

  Future<void> start() async {
    if (_started || _disposed) return;
    _started = true;
    await _api.subscribePreview(uid);
    if (_disposed) return;
    unawaited(_loop());
  }

  /// Temporary pause (page hidden) — keeps the server-side subscription.
  void pause() => _paused = true;

  /// Resume polling after [pause].
  void resume() {
    if (!_started || _disposed || !_paused) return;
    _paused = false;
    unawaited(_loop());
  }

  /// Permanent stop — releases the server-side subscription.
  Future<void> stop() async {
    if (_disposed) return;
    _disposed = true;
    unawaited(_api.unsubscribePreview(uid));
  }

  Future<void> _loop() async {
    final target = Duration(milliseconds: (1000 / fps).round());
    while (!_disposed && !_paused) {
      final sw = Stopwatch()..start();
      try {
        final frame = await _api.previewFrame(uid);
        if (_disposed || _paused) break;
        if (frame == null) {
          _misses++;
          // Persistent misses can also mean a lost subscription (race with a
          // previous unsubscribe) — re-subscribe every few misses.
          if (_misses % 5 == 0) await _api.subscribePreview(uid);
          if (_misses >= 10) {
            state.value = frames.value != null
                ? PreviewState.waiting // had frames before: brief stall
                : PreviewState.offline; // never had any: likely not streaming
          }
        } else {
          _misses = 0;
          frames.value = frame;
          state.value = PreviewState.live;
        }
      } on ApiException {
        if (_disposed || _paused) break;
        state.value = PreviewState.offline;
        await Future<void>.delayed(const Duration(seconds: 2));
      }
      final wait = target - sw.elapsed;
      if (wait > Duration.zero && !_disposed && !_paused) {
        await Future<void>.delayed(wait);
      }
    }
  }
}
