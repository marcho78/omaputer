#if defined(OMARCHY_CLOCK)

#include "omarchy_clock.h"

#include "nvs_helpers.h"
#include <sys/time.h>
#include <time.h>

namespace {

constexpr const char *kDefaultTimeZone = "PST8PDT,M3.2.0,M11.1.0"; // Pacific, with daylight saving.
constexpr const char *kTimeZoneKey = "tz";                           // In the "launcher" NVS namespace.
constexpr time_t kClockSet = 1700000000; // Any time before this means "not set yet".
bool started = false;
bool zoneApplied = false;

void applyTimeZone() {
    setenv("TZ", omarchyClockTimeZone().c_str(), 1);
    tzset();
    zoneApplied = true;
}

bool now(tm &local) {
    const time_t t = time(nullptr);
    if (t < kClockSet) return false;
    if (!zoneApplied) applyTimeZone(); // Also when the clock was set without WiFi.
    localtime_r(&t, &local);
    return true;
}

String clockText(const tm &t) {
    char buf[12];
    const int hour = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    snprintf(buf, sizeof buf, "%d:%02d %s", hour, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

} // namespace

void omarchyClockStart() {
    if (started) return;
    started = true;
    configTzTime(omarchyClockTimeZone().c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    zoneApplied = true;
}

String omarchyClockTimeZone() {
    const String saved = lnvs::getString("launcher", kTimeZoneKey, 64);
    return saved.isEmpty() ? String(kDefaultTimeZone) : saved;
}

bool omarchyClockSetTimeZone(const String &tz) {
    if (tz.isEmpty()) {
        lnvs::Handle handle("launcher", true);
        if (handle && lnvs::eraseKey(handle.raw(), kTimeZoneKey)) handle.commit();
    } else {
        // A POSIX time zone is names, offsets and rules: nothing but these characters.
        if (tz.length() > 63) return false;
        for (char c : tz) {
            if (!isalnum(static_cast<unsigned char>(c)) && !strchr("<>+-:,./", c)) return false;
        }
        if (!lnvs::setString("launcher", kTimeZoneKey, tz.c_str())) return false;
    }
    applyTimeZone();
    return true;
}

bool omarchyClockSetUtc(time_t seconds) {
    if (seconds < kClockSet) return false;
    const timeval tv = {seconds, 0};
    if (settimeofday(&tv, nullptr) != 0) return false;
    if (!zoneApplied) applyTimeZone();
    return true;
}

bool omarchyClockSetLocal(const String &local) {
    // "YYYY-MM-DDTHH:MM", maybe with ":SS": what the WebUI's date and time fields give. Read
    // by position rather than with sscanf, which would pull the whole scanf code into flash.
    if (local.length() < 16 || local[4] != '-' || local[7] != '-' || local[10] != 'T' || local[13] != ':') return false;
    tm t = {};
    t.tm_year = local.substring(0, 4).toInt() - 1900;
    t.tm_mon = local.substring(5, 7).toInt() - 1;
    t.tm_mday = local.substring(8, 10).toInt();
    t.tm_hour = local.substring(11, 13).toInt();
    t.tm_min = local.substring(14, 16).toInt();
    if (local.length() >= 19 && local[16] == ':') t.tm_sec = local.substring(17, 19).toInt();
    t.tm_isdst = -1; // mktime works out daylight saving.
    applyTimeZone(); // mktime reads the date in it.
    return omarchyClockSetUtc(mktime(&t));
}

String omarchyClockTime() {
    tm t;
    return now(t) ? clockText(t) : String();
}

String omarchyClockDateTime() {
    static const char *const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *const kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                          "July",    "August",   "September", "October", "November", "December"};
    tm t;
    if (!now(t)) return String();
    char buf[64];
    snprintf(buf, sizeof buf, "%s, %s %d, %d, ", kDays[t.tm_wday], kMonths[t.tm_mon], t.tm_mday, t.tm_year + 1900);
    return String(buf) + clockText(t);
}

#endif
