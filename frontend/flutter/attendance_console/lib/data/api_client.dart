import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;

/// Error surfaced by the API layer with a user-presentable message.
class ApiException implements Exception {
  ApiException(this.message, [this.statusCode]);

  final String message;
  final int? statusCode;

  @override
  String toString() => message;
}

/// Thin REST client for the FaceAuthentication backend.
///
/// - [baseUrl] empty ⇒ same-origin `/api` (the nginx deployment).
/// - All timestamps/dates are Gregorian `YYYY-MM-DD` (server local time).
/// - JSON errors from the backend keep their `error` message.
class ApiClient {
  ApiClient({this.baseUrl = ''}) : _http = http.Client();

  /// '' → same-origin. Otherwise like `http://localhost:8080` (no trailing /).
  final String baseUrl;
  final http.Client _http;

  /// Releases the underlying HTTP client.
  void close() => _http.close();

  static const _timeout = Duration(seconds: 15);
  static const _longTimeout = Duration(minutes: 6); // deep discovery scan

  /// Sensible dev default: talk straight to a locally running backend.
  static String get defaultBaseUrl => kDebugMode ? 'http://localhost:8080' : '';

  Uri _uri(String path, [Map<String, String>? query]) {
    final base = baseUrl.isEmpty ? '' : (baseUrl.endsWith('/') ? baseUrl.substring(0, baseUrl.length - 1) : baseUrl);
    final q = query == null || query.isEmpty ? null : query.entries.map((e) => '${Uri.encodeComponent(e.key)}=${Uri.encodeComponent(e.value)}').join('&');
    return Uri.parse('$base$path${q == null ? '' : '?$q'}');
  }

  Map<String, String> get _headers => {
        'Accept': 'application/json',
        'Cache-Control': 'no-cache',
      };

  Never _fail(int status, String body) {
    String msg;
    try {
      final decoded = jsonDecode(body);
      if (decoded is Map && decoded['error'] is String) {
        msg = decoded['error'] as String;
      } else {
        msg = body;
      }
    } catch (_) {
      msg = body.isEmpty ? 'HTTP $status' : body;
    }
    throw ApiException(msg, status);
  }

  Future<dynamic> _send(http.Request request, {Duration timeout = _timeout}) async {
    http.StreamedResponse response;
    try {
      response = await _http.send(request).timeout(timeout);
    } on TimeoutException {
      throw ApiException('Request timed out');
    } on http.ClientException catch (e) {
      throw ApiException(e.message.isNotEmpty ? e.message : 'Network error');
    }
    final body = await response.stream.bytesToString();
    final status = response.statusCode;
    if (status >= 400) _fail(status, body);
    if (body.isEmpty) return null;
    try {
      return jsonDecode(body);
    } catch (_) {
      return body;
    }
  }

  dynamic get(String path, {Map<String, String>? query, bool long = false}) =>
      _send(http.Request('GET', _uri(path, query))..headers.addAll(_headers),
          timeout: long ? _longTimeout : _timeout);

  Future<Map<String, Object?>> getObj(String path, {Map<String, String>? query}) async =>
      Map<String, Object?>.from(await get(path, query: query) as Map);

  Future<List<Map<String, Object?>>> getList(String path, {Map<String, String>? query}) async {
    final r = await get(path, query: query);
    return r is List ? r.whereType<Map>().map((e) => Map<String, Object?>.from(e)).toList() : const [];
  }

  Future<Map<String, Object?>> post(String path, [Map<String, Object?>? body]) async =>
      Map<String, Object?>.from(await _send(
        http.Request('POST', _uri(path))
          ..headers.addAll({..._headers, 'Content-Type': 'application/json'})
          ..body = jsonEncode(body ?? {}),
      ));

  /// POST whose response body is a JSON array (e.g. camera discovery).
  Future<List<Map<String, Object?>>> postList(String path, [Map<String, Object?>? body, bool long = false]) async {
    final r = await _send(
      http.Request('POST', _uri(path))
        ..headers.addAll({..._headers, 'Content-Type': 'application/json'})
        ..body = jsonEncode(body ?? {}),
      timeout: long ? _longTimeout : _timeout,
    );
    return r is List ? r.whereType<Map>().map((e) => Map<String, Object?>.from(e)).toList() : const [];
  }

  Future<Map<String, Object?>> put(String path, [Map<String, Object?>? body]) async =>
      Map<String, Object?>.from(await _send(
        http.Request('PUT', _uri(path))
          ..headers.addAll({..._headers, 'Content-Type': 'application/json'})
          ..body = jsonEncode(body ?? {}),
      ));

  Future<void> delete(String path) async => _send(http.Request('DELETE', _uri(path))..headers.addAll(_headers));

  /// Binary GET that maps 404 → null (used for person photos & face crops).
  Future<Uint8List?> getImage(String path) async {
    http.StreamedResponse response;
    try {
      response = await _http
          .send(http.Request('GET', _uri(path))..headers.addAll(_headers))
          .timeout(_timeout);
    } on TimeoutException {
      throw ApiException('Request timed out');
    } on http.ClientException catch (e) {
      throw ApiException(e.message.isNotEmpty ? e.message : 'Network error');
    }
    if (response.statusCode == 404) return null;
    if (response.statusCode >= 400) {
      _fail(response.statusCode, await response.stream.bytesToString());
    }
    final bytes = await response.stream.toBytes();
    return Uint8List.fromList(bytes);
  }

  // ── Live preview plumbing ─────────────────────────────────────────────
  //
  // The backend only publishes frames while at least one subscriber exists
  // (CameraPreviewHub::publishFrame early-returns otherwise), so callers must
  // [subscribe] before polling frames and [unsubscribe] when done.

  Future<void> subscribePreview(String uid) async {
    try {
      await get('/api/preview/$uid/subscribe');
    } on ApiException catch (e) {
      // Subscribing to a camera that isn't streaming yet → 404/503; ignore.
      debugPrint('subscribe($uid) ignored: ${e.statusCode}');
    }
  }

  Future<void> unsubscribePreview(String uid) async {
    try {
      await get('/api/preview/$uid/unsubscribe');
    } on ApiException catch (e) {
      debugPrint('unsubscribe($uid) ignored: ${e.statusCode}');
    }
  }

  /// One JPEG frame. Returns null when no frame is currently available.
  Future<PreviewFrame?> previewFrame(String uid) async {
    final req = http.Request('GET', _uri('/api/preview/$uid/jpeg', {'_': '${DateTime.now().millisecondsSinceEpoch}'}))
      ..headers.addAll(_headers);
    http.StreamedResponse response;
    try {
      response = await _http.send(req).timeout(_timeout);
    } on TimeoutException {
      throw ApiException('Preview request timed out');
    } on http.ClientException {
      throw ApiException('Network error');
    }
    if (response.statusCode == 404) return null;
    if (response.statusCode >= 400) {
      _fail(response.statusCode, await response.stream.bytesToString());
    }
    final bytes = await response.stream.toBytes();
    final fps = double.tryParse(response.headers['x-fps'] ?? '') ?? 0;
    return PreviewFrame(
      jpeg: Uint8List.fromList(bytes),
      fps: fps,
    );
  }
}

class PreviewFrame {
  const PreviewFrame({required this.jpeg, required this.fps});

  final Uint8List jpeg;
  final double fps;
}

// ── Connection test result for the Settings page / camera dialog ─────────

Future<Map<String, Object?>> testConnectionRaw(
  ApiClient client,
  String streamUri,
  String username,
  String password,
) {
  return client.post('/api/cameras/test', {
    'streamUri': streamUri,
    'username': username,
    'password': password,
  });
}
