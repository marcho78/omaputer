// Omarchy's screensaver for the Launcher: once nothing has been pressed for Omarchy's idle
// time, Omarchy's logo animation plays until a key is pressed.
#ifndef OMARCHY_SCREENSAVER_H
#define OMARCHY_SCREENSAVER_H

#if defined(OMARCHY_SCREENSAVER)
// Menus call this every time round their loop. If the Launcher has been idle long enough, it
// plays the animation until a key is pressed, swallows that key, clears the screen and
// returns true; the caller must then redraw its whole screen. Otherwise it returns false at once.
bool omarchyScreensaver();

// Plays the animation now, the same way: the main menu's Screensaver item.
void omarchyScreensaverNow();

// Whether there is enough free memory to play it now. With WiFi on there may not be; the
// WebUI asks before starting it, so its page can say so.
bool omarchyScreensaverCanPlay();
#else
inline bool omarchyScreensaver() { return false; }
#endif

#endif
