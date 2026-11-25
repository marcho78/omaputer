#ifndef OMARCHY_TILE_MENU_H
#define OMARCHY_TILE_MENU_H

#if defined(OMARCHY_TILE_MENU)

#include <globals.h>
#include <vector>

// The Cardputer's main menu as sliding colour tiles (lib/omarchy_menu), drawn in place of
// the Omarchy list menu. Moving to another item slides the row with a short spring
// animation.

// Draws the menu with `index` selected, sliding from the last selection unless fullRedraw.
// False when it can't (not a 240x135 screen, or not enough memory): draw the list instead.
bool omarchyTileMenuDraw(std::vector<MenuOptions> &opt, int index, bool fullRedraw);

// True while the tile menu is what is on screen.
bool omarchyTileMenuShown();

// Redraws the status bar if the battery, WiFi or time changed. True if it drew.
bool omarchyTileMenuStatus();

// Frees the menu's memory before a menu item runs; the next draw takes it again.
void omarchyTileMenuRelease();

#endif
#endif
