/// Locale-aware formatting helpers.
///
/// Dates are Jalali for Persian and Gregorian for English.
/// The backend consumes strict `YYYY-MM-DD` (Gregorian) keys —
/// [dateKey] is the only place that builds them.
library;

import 'package:shamsi_date/shamsi_date.dart';
const _faDigits = ['۰', '۱', '۲', '۳', '۴', '۵', '۶', '۷', '۸', '۹'];

/// Converts Latin digits to Persian-Extended digits.
String faDigits(Object input) {
  final s = input.toString();
  var out = s;
  for (var i = 0; i < 10; i++) {
    out = out.replaceAll('$i', _faDigits[i]);
  }
  return out;
}

/// Locale-aware digit rendering for numbers/times.
String num(Object input, {bool fa = true}) => fa ? faDigits(input) : input.toString();

/// `YYYY-MM-DD` for the backend (always Gregorian, always local time).
String dateKey(DateTime d) =>
    '${d.year.toString().padLeft(4, '0')}-'
    '${d.month.toString().padLeft(2, '0')}-'
    '${d.day.toString().padLeft(2, '0')}';

DateTime dateFromKey(String key) {
  final p = key.split('-');
  return DateTime(int.parse(p[0]), int.parse(p[1]), int.parse(p[2]));
}

DateTime dayStart(DateTime d) => DateTime(d.year, d.month, d.day);

/// Long human date: «شنبه ۵ آبان ۱۴۰۵» / "Sun, Oct 26 2026".
String longDate(DateTime d, {bool fa = true}) {
  if (fa) {
    final j = Jalali.fromDateTime(d);
    final f = j.formatter;
    return '${f.wN} ${faDigits(f.d)} ${f.mN} ${faDigits(f.yyyy)}';
  }
  const months = [
    'Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
    'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec',
  ];
  const days = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  return '${days[d.weekday - 1]}, ${months[d.month - 1]} ${d.day} ${d.year}';
}

/// Medium date for tables/cards: «۵ آبان» / "Oct 26".
String mediumDate(DateTime d, {bool fa = true}) {
  if (fa) {
    final f = Jalali.fromDateTime(d).formatter;
    return '${faDigits(f.d)} ${f.mN}';
  }
  const months = [
    'Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
    'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec',
  ];
  return '${months[d.month - 1]} ${d.day}';
}

/// Tiny date for chart axis labels: «۵» / "26".
String dayNum(DateTime d, {bool fa = true}) =>
    fa ? faDigits(d.day) : '${d.day}';

/// Weekday short: «ش» / "Sat".
String weekdayShort(DateTime d, {bool fa = true}) {
  if (fa) {
    // Jalali week: 1=شنبه … 7=جمعه
    const names = ['ش', 'ی', 'د', 'س', 'چ', 'پ', 'ج'];
    final j = Jalali.fromDateTime(d);
    return names[j.weekDay - 1];
  }
  const days = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  return days[d.weekday - 1];
}

/// Clock time from a DateTime: `۸:۰۵` / `8:05`.
String clock(DateTime d, {bool fa = true}) {
  final hh = d.hour.toString().padLeft(2, '0');
  final mm = d.minute.toString().padLeft(2, '0');
  final s = '$hh:$mm';
  return fa ? faDigits(s) : s;
}

/// Unix seconds → DateTime (local).
DateTime fromUnix(int seconds) =>
    DateTime.fromMillisecondsSinceEpoch(seconds * 1000);

/// `HH:mm` from unix seconds; empty string when `ts <= 0`.
String unixClock(int? seconds, {bool fa = true}) {
  final ts = seconds ?? 0;
  if (ts <= 0) return '';
  return clock(fromUnix(ts), fa: fa);
}

/// Relative time for unknown-face stamps: «۳ ساعت پیش» / "3h ago".
String timeAgo(DateTime d, {bool fa = true}) {
  final diff = DateTime.now().difference(d);
  if (fa) {
    if (diff.inMinutes < 1) return 'همین حالا';
    if (diff.inHours < 1) return '${faDigits(diff.inMinutes)} دقیقه پیش';
    if (diff.inDays < 1) return '${faDigits(diff.inHours)} ساعت پیش';
    if (diff.inDays < 30) return '${faDigits(diff.inDays)} روز پیش';
    return mediumDate(d, fa: true);
  }
  if (diff.inMinutes < 1) return 'just now';
  if (diff.inHours < 1) return '${diff.inMinutes}m ago';
  if (diff.inDays < 1) return '${diff.inHours}h ago';
  if (diff.inDays < 30) return '${diff.inDays}d ago';
  return mediumDate(d, fa: false);
}

/// Compact duration: `۷س ۲۵د` / `7h 25m`, or `—` when zero/null.
String compactDuration(int? seconds, {bool fa = true}) {
  final s = seconds ?? 0;
  if (s <= 0) return '';
  final h = s ~/ 3600;
  final m = (s % 3600) ~/ 60;
  final fa0 = fa;
  if (h > 0 && m > 0) {
    return fa0 ? '${faDigits(h)}س ${faDigits(m)}د' : '${h}h ${m}m';
  }
  if (h > 0) return fa0 ? '${faDigits(h)}س' : '${h}h';
  return fa0 ? '${faDigits(m)}د' : '${m}m';
}

/// Long spoken duration: «۷ ساعت و ۲۵ دقیقه» / "7 h 25 min".
String longDuration(int? seconds, {bool fa = true}) {
  final s = seconds ?? 0;
  if (s <= 0) return fa ? '—' : '—';
  final h = s ~/ 3600;
  final m = (s % 3600) ~/ 60;
  if (fa) {
    final parts = <String>[];
    if (h > 0) parts.add('${faDigits(h)} ساعت');
    if (m > 0) parts.add('${faDigits(m)} دقیقه');
    if (parts.isEmpty) return '${faDigits(s)} ثانیه';
    return parts.join(' و ');
  }
  final parts = <String>[];
  if (h > 0) parts.add('$h h');
  if (m > 0) parts.add('$m min');
  return parts.isEmpty ? '$s s' : parts.join(' ');
}

/// Percent of a 24h day for the day-timeline painter.
double dayFraction(int unixSeconds) =>
    (fromUnix(unixSeconds).hour * 60 + fromUnix(unixSeconds).minute) / (24 * 60);
