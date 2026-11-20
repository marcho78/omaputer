// Native shim: SPIClass already comes from DisplayDrivers' USE_NATIVE_SDL
// backend; this only adds Arduino's global SPI bus object.
#pragma once

#include <DisplayDrivers.h>

extern SPIClass SPI;
