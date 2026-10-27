import 'package:flutter/widgets.dart';

/// Two-language string table (Persian default / English).
///
/// Deliberately hand-rolled (no .arb/codegen): the app is small, and this
/// keeps every translatable string in one reviewable place. Locale is taken
/// from the MaterialApp locale, which is driven by [AppSettings].
class S {
  const S(this.localeCode);

  /// ISO code such as `fa` or `en`.
  final String localeCode;

  bool get isFa => localeCode.startsWith('fa');

  static S of(BuildContext context) {
    final l = Localizations.maybeLocaleOf(context);
    return S(l?.languageCode ?? 'fa');
  }

  // ── App / brand ────────────────────────────────────────────────────────
  String get appName => 'FaceAuthentication';
  String get console => isFa ? 'کنسول مدیریت' : 'Admin Console';

  // ── Navigation ──────────────────────────────────────────────────────────
  String get navDashboard => isFa ? 'داشبورد' : 'Dashboard';
  String get navLive => isFa ? 'پخش زنده' : 'Live';
  String get navPeople => isFa ? 'افراد' : 'People';
  String get navAttendance => isFa ? 'حاضری' : 'Attendance';
  String get navCameras => isFa ? 'دوربین‌ها' : 'Cameras';
  String get navZones => isFa ? 'نواحی' : 'Zones';
  String get navUnknown => isFa ? 'چهره‌های ناشناس' : 'Unknown Faces';
  String get navReports => isFa ? 'گزارش‌ها' : 'Reports';
  String get navSettings => isFa ? 'تنظیمات' : 'Settings';

  // ── Common ─────────────────────────────────────────────────────────────
  String get save => isFa ? 'ذخیره' : 'Save';
  String get cancel => isFa ? 'انصراف' : 'Cancel';
  String get delete => isFa ? 'حذف' : 'Delete';
  String get edit => isFa ? 'ویرایش' : 'Edit';
  String get add => isFa ? 'افزودن' : 'Add';
  String get search => isFa ? 'جستجو' : 'Search';
  String get close => isFa ? 'بستن' : 'Close';
  String get retry => isFa ? 'تلاش دوباره' : 'Retry';
  String get refresh => isFa ? 'به‌روزرسانی' : 'Refresh';
  String get today => isFa ? 'امروز' : 'Today';
  String get confirm => isFa ? 'تأیید' : 'Confirm';
  String get viewAll => isFa ? 'مشاهده همه' : 'View all';
  String get optional => isFa ? 'اختیاری' : 'optional';
  String get loading => isFa ? 'در حال بارگذاری…' : 'Loading…';
  String get error => isFa ? 'خطا' : 'Error';
  String get name => isFa ? 'نام' : 'Name';
  String get code => isFa ? 'کد' : 'Code';
  String get date => isFa ? 'تاریخ' : 'Date';
  String get time => isFa ? 'ساعت' : 'Time';
  String get status => isFa ? 'وضعیت' : 'Status';
  String get actions => isFa ? 'عملیات' : 'Actions';
  String get notSet => isFa ? 'ثبت نشده' : 'Not set';
  String get serverUnreachable =>
      isFa ? 'سرور در دسترس نیست' : 'Server unreachable';
  String get somethingWentWrong =>
      isFa ? 'خطایی رخ داد. دوباره تلاش کنید.' : 'Something went wrong. Try again.';

  // ── Attendance status ──────────────────────────────────────────────────
  String get stPresent => isFa ? 'حاضر' : 'Present';
  String get stLate => isFa ? 'با تأخیر' : 'Late';
  String get stAbsent => isFa ? 'غایب' : 'Absent';
  String get stNotArrived => isFa ? 'هنوز نرسیده' : 'Not arrived';
  String get stDeparted => isFa ? 'خارج شده' : 'Departed';
  String get stOnTime => isFa ? 'به‌موقع' : 'On time';
  String get dayOff => isFa ? 'روز غیرکاری' : 'Day off';

  // ── Dashboard ──────────────────────────────────────────────────────────
  String get dashPresentToday => isFa ? 'حاضر امروز' : 'Present today';
  String get dashLateToday => isFa ? 'تأخیر امروز' : 'Late today';
  String get dashAbsentToday => isFa ? 'غیبت امروز' : 'Absent today';
  String get dashPendingUnknown => isFa ? 'ناشناس در انتظار' : 'Unknown pending';
  String get dashWeekly => isFa ? 'حاضری ۷ روز اخیر' : 'Attendance · last 7 days';
  String get dashCameras => isFa ? 'وضعیت دوربین‌ها' : 'Cameras';
  String get dashTodayBoard => isFa ? 'حاضری امروز' : "Today's attendance";
  String get dashPeople => isFa ? 'افراد' : 'People';
  String get lastUpdate => isFa ? 'آخرین به‌روزرسانی' : 'Last updated';

  // ── Live ───────────────────────────────────────────────────────────────
  String get liveTitle => isFa ? 'پخش زنده دوربین‌ها' : 'Live cameras';
  String get liveNoCameras =>
      isFa ? 'هنوز دوربینی ثبت نشده است' : 'No cameras registered yet';
  String get liveNoCamerasHint => isFa
      ? 'از صفحه دوربین‌ها یک دوربین اضافه یا اسکن کنید.'
      : 'Add or discover a camera from the Cameras page.';
  String get liveWaiting =>
      isFa ? 'در انتظار تصویر…' : 'Waiting for frames…';
  String get liveDisabled =>
      isFa ? 'پخش غیرفعال است' : 'Streaming disabled';
  String get liveEnable =>
      isFa ? 'فعال‌سازی پخش' : 'Enable streaming';
  String get liveOffline => isFa ? 'قطع' : 'Offline';
  String get liveFps => 'fps';
  String get active => isFa ? 'فعال' : 'Active';
  String get camPreviewOnly => isFa ? 'فقط پخش' : 'Stream only';

  // ── People ────────────────────────────────────────────────────────────
  String get peopleTitle => isFa ? 'افراد' : 'People';
  String get addPerson => isFa ? 'افزودن فرد' : 'Add person';
  String get editPerson => isFa ? 'ویرایش فرد' : 'Edit person';
  String get personName => isFa ? 'نام و نام خانوادگی' : 'Full name';
  String get personCode => isFa ? 'کد پرسنلی' : 'Personnel code';
  String get peopleEmpty => isFa ? 'فردی ثبت نشده است' : 'No people registered';
  String get peopleEmptyHint => isFa
      ? 'اولین فرد را اضافه کنید یا از چهره‌های ناشناس ثبت کنید.'
      : 'Add your first person or enroll an unknown face.';
  String get deletePersonQ => isFa
      ? 'این فرد به همراه همه چهره‌های مرجع و سوابقش حذف می‌شود. مطمئن هستید؟'
      : 'This person, their reference faces and history will be deleted. Are you sure?';
  String get accessLevel => isFa ? 'سطح دسترسی' : 'Access level';

  String accessLevelName(int level) {
    if (!isFa) {
      return switch (level) {
        0 => 'Guest',
        1 => 'Staff',
        2 => 'Supervisor',
        3 => 'Admin',
        _ => 'Level $level',
      };
    }
    return switch (level) {
      0 => 'مهمان',
      1 => 'پرسنل',
      2 => 'سرپرست',
      3 => 'مدیر',
      _ => 'سطح $level',
    };
  }

  // ── Person detail ─────────────────────────────────────────────────────
  String get profile => isFa ? 'پرونده' : 'Profile';
  String get arrival => isFa ? 'ورود' : 'Arrival';
  String get departure => isFa ? 'خروج' : 'Departure';
  String get worked => isFa ? 'کار کرده' : 'Worked';
  String get onBreak => isFa ? 'استراحت' : 'Break';
  String get lateBy => isFa ? 'دیرکرد' : 'Late by';
  String get currentLocation => isFa ? 'آخرین موقعیت' : 'Last seen';
  String get currentLocationHint =>
      isFa ? 'آخرین ناحیه‌ای که فرد در آن دیده شده' : 'Last zone the person was seen in';
  String get dayTimeline => isFa ? 'خط روز' : 'Day timeline';
  String get breaksTitle => isFa ? 'زمان‌های خروج و بازگشت' : 'Breaks';
  String get noBreaks => isFa ? 'خروج موقتی ثبت نشده' : 'No breaks recorded';
  String get schedule => isFa ? 'شیفت کاری' : 'Work schedule';
  String get scheduleHint => isFa
      ? 'مقایسه ورود با این ساعت‌ها، تأخیر را تعیین می‌کند.'
      : 'Arrivals are compared against these times to flag tardiness.';
  String get expectedArrival => isFa ? 'ساعت ورود مورد انتظار' : 'Expected arrival';
  String get expectedDeparture => isFa ? 'ساعت خروج مورد انتظار' : 'Expected departure';
  String get graceMinutes => isFa ? 'دقت مجاز تأخیر (دقیقه)' : 'Grace period (minutes)';
  String get workDays => isFa ? 'روزهای کاری' : 'Work days';
  String get scheduleSaved => isFa ? 'شیفت ذخیره شد' : 'Schedule saved';
  String get photoUpdated => isFa ? 'تصویر به‌روزرسانی شد' : 'Photo updated';
  String get changePhoto => isFa ? 'تغییر تصویر' : 'Change photo';
  String get uploadPhoto => isFa ? 'بارگذاری تصویر' : 'Upload photo';
  String get photoHint => isFa
      ? 'تصویری روبه‌رو و باکیفیت از چهره انتخاب کنید.'
      : 'Pick a clear, front-facing photo of the face.';

  List<String> get weekDayNames => isFa
      ? ['شنبه', 'یکشنبه', 'دوشنبه', 'سه‌شنبه', 'چهارشنبه', 'پنجشنبه', 'جمعه']
      : ['Sat', 'Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri'];

  // ── Attendance ────────────────────────────────────────────────────────
  String get attendanceTitle => isFa ? 'حاضری' : 'Attendance';
  String get attendanceEmpty =>
      isFa ? 'برای این تاریخ رکوردی نیست' : 'No records for this date';

  // ── Cameras ───────────────────────────────────────────────────────────
  String get camerasTitle => isFa ? 'دوربین‌ها' : 'Cameras';
  String get addCamera => isFa ? 'افزودن دوربین' : 'Add camera';
  String get editCamera => isFa ? 'ویرایش دوربین' : 'Edit camera';
  String get scanNetwork => isFa ? 'اسکن شبکه' : 'Scan network';
  String get scanning => isFa ? 'در حال اسکن…' : 'Scanning…';
  String get scanHint => isFa
      ? 'اسکن عمیق ممکن است چند دقیقه طول بکشد؛ پنجره را نبندید.'
      : 'A deep scan can take a few minutes; keep this dialog open.';
  String get deepScan => isFa ? 'اسکن عمیق شبکه' : 'Deep subnet scan';
  String get scanResults => isFa ? 'نتایج اسکن' : 'Scan results';
  String get noResults => isFa ? 'چیزی پیدا نشد' : 'Nothing found';
  String get cameraType => isFa ? 'نوع دوربین' : 'Camera role';
  String get ctEntry => isFa ? 'ورود' : 'Entry';
  String get ctExit => isFa ? 'خروج' : 'Exit';
  String get ctBoth => isFa ? 'ورود/خروج' : 'Both';
  String get zone => isFa ? 'ناحیه' : 'Zone';
  String get noZone => isFa ? 'بدون ناحیه' : 'No zone';
  String get streamUri => isFa ? 'آدرس استریم (RTSP/ONVIF)' : 'Stream URI (RTSP/ONVIF)';
  String get username => isFa ? 'نام کاربری' : 'Username';
  String get password => isFa ? 'گذرواژه' : 'Password';
  String get transport => isFa ? 'پروتکل انتقال' : 'Transport';
  String get testConnection => isFa ? 'تست اتصال' : 'Test connection';
  String get testOk => isFa ? 'اتصال موفق' : 'Connection OK';
  String get testFailed => isFa ? 'اتصال ناموفق' : 'Connection failed';
  String get needsAuth => isFa ? 'احراز هویت لازم است' : 'Authentication required';
  String get streamEnabled => isFa ? 'پخش فعال' : 'Streaming';
  String get aiEnabled => isFa ? 'تشخیص چهره' : 'Face AI';
  String get deleteCameraQ =>
      isFa ? 'این دوربین حذف شود؟' : 'Delete this camera?';
  String get connLocal => isFa ? 'دستگاه محلی' : 'Local device';
  String get connRtsp => isFa ? 'RTSP' : 'RTSP';
  String get connOnvif => isFa ? 'ONVIF' : 'ONVIF';
  String get camerasEmpty =>
      isFa ? 'دوربینی ثبت نشده است' : 'No cameras registered';
  String get cameraAdded => isFa ? 'دوربین اضافه شد' : 'Camera added';
  String get cameraSaved => isFa ? 'دوربین ذخیره شد' : 'Camera saved';

  // ── Zones ──────────────────────────────────────────────────────────────
  String get zonesTitle => isFa ? 'نواحی' : 'Zones';
  String get addZone => isFa ? 'افزودن ناحیه' : 'Add zone';
  String get zoneName => isFa ? 'نام ناحیه' : 'Zone name';
  String get description => isFa ? 'توضیح' : 'Description';
  String get requiredLevel => isFa ? 'حداقل سطح دسترسی' : 'Required access level';
  String get zonesEmpty => isFa ? 'ناحیه‌ای ثبت نشده است' : 'No zones yet';

  // ── Unknown faces ──────────────────────────────────────────────────────
  String get unknownTitle => isFa ? 'چهره‌های ناشناس' : 'Unknown faces';
  String get unknownEmpty => isFa ? 'چهره ناشناسی در انتظار نیست' : 'Nothing waiting for review';
  String get unknownEmptyHint => isFa
      ? 'چهره‌هایی که شناسایی نشوند اینجا برای ثبت ظاهر می‌شوند.'
      : 'Faces the system could not identify will queue here.';
  String get enrollAsNew => isFa ? 'ثبت به‌عنوان فرد جدید' : 'Enroll as new person';
  String get mergeExisting => isFa ? 'ادغام با فرد موجود' : 'Merge with existing person';
  String get discard => isFa ? 'دور انداختن' : 'Discard';
  String get discardQ => isFa ? 'این چهره دور انداخته شود؟' : 'Discard this face?';
  String get choosePerson => isFa ? 'انتخاب فرد' : 'Choose a person';
  String get enrolledOk => isFa ? 'فرد جدید ثبت شد' : 'Person enrolled';
  String get mergedOk => isFa ? 'چهره با فرد انتخاب‌شده ادغام شد' : 'Face merged';
  String get discardedOk => isFa ? 'چهره حذف شد' : 'Face discarded';
  String get goProfile => isFa ? 'رفتن به پرونده' : 'Open profile';
  String get seenOn => isFa ? 'دیده‌شده در' : 'Seen on';

  // ── Reports ───────────────────────────────────────────────────────────
  String get reportsTitle => isFa ? 'گزارش‌ها' : 'Reports';
  String get selectPerson => isFa ? 'انتخاب فرد' : 'Select a person';
  String get weeklyHours => isFa ? 'ساعات کاری هفته' : 'Weekly work hours';
  String get totalWorked => isFa ? 'مجموع کار' : 'Total worked';
  String get dailyAvg => isFa ? 'میانگین روزانه' : 'Daily average';
  String get totalLate => isFa ? 'مجموع دیرکرد' : 'Total tardiness';
  String get tardinessByPerson => isFa ? 'تأخیر افراد (هفته جاری)' : 'Tardiness by person (this week)';
  String get lateMinutes => isFa ? 'دیرکرد (دقیقه)' : 'Late (minutes)';

  // ── Settings ──────────────────────────────────────────────────────────
  String get connection => isFa ? 'اتصال به سرور' : 'Server connection';
  String get serverUrl => isFa ? 'نشانی سرور' : 'Server URL';
  String get serverUrlHint => isFa
      ? 'در حالت استقرار معمول همین مقدار خالی بماند (پروکسی داخلی). برای اجرای محلی مثل http://localhost:8080'
      : 'Leave empty for the standard deployment (same-origin /api proxy). For local dev e.g. http://localhost:8080';
  String get connected => isFa ? 'متصل' : 'Connected';
  String get notConnected => isFa ? 'قطع' : 'Disconnected';
  String get backendVersion => isFa ? 'نسخه سرور' : 'Backend version';
  String get appearance => isFa ? 'ظاهر' : 'Appearance';
  String get themeMode => isFa ? 'پوسته' : 'Theme';
  String get tmSystem => isFa ? 'سیستم' : 'System';
  String get tmLight => isFa ? 'روشن' : 'Light';
  String get tmDark => isFa ? 'تاریک' : 'Dark';
  String get language => isFa ? 'زبان' : 'Language';
  String get about => isFa ? 'درباره' : 'About';
}
