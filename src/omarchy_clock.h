#ifndef OMARCHY_CLOCK_H
#define OMARCHY_CLOCK_H

#if defined(OMARCHY_CLOCK)

#include <Arduino.h>

// The Cardputer has no clock that survives power-off, so the time is set from the internet
// each time WiFi connects, or from the WebUI's Settings page, in the time zone saved there
// (Pacific, the user's time zone, until one is saved).

void omarchyClockStart();      // Starts setting the clock; call once WiFi is up. Repeats are ignored.
String omarchyClockTime();     // Like "4:21 PM", or empty until the clock is set.
String omarchyClockDateTime(); // Like "Sunday, November 22, 2026, 4:21 PM", or empty.

// The time zone, as a POSIX TZ string like "CET-1CEST,M3.5.0,M10.5.0/3".
String omarchyClockTimeZone();
// Saves and applies a time zone; empty goes back to Pacific. False if it isn't one.
bool omarchyClockSetTimeZone(const String &tz);
// Sets the clock without the internet: seconds since 1970 (UTC), or a local date and time
// like "2026-11-24T16:21" in the time zone above. False if it isn't a usable time.
bool omarchyClockSetUtc(time_t seconds);
bool omarchyClockSetLocal(const String &local);

#endif
#endif
