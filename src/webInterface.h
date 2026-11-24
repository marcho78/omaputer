#ifndef __WEBINTERFACE_H
#define __WEBINTERFACE_H

#include <FS.h>
#include <SPI.h>
#include <webFiles.h>

String humanReadableSize(uint64_t bytes);
String listFiles(const String &folder);
String readLineFromFile(File myFile);

void configureWebServer();
void startWebUi(const String &ssid, int encryptation, bool mode_ap = false);

void webUIMyNet();
void loopOptionsWebUi();

// True once at boot if the WebUI asked for this restart less than a minute ago, so the
// Launcher should start it again; apMode says whether it ran on the Launcher's own WiFi.
bool webUiComeBackPending(bool &apMode);

#endif //__WEBINTERFACE_H
