#pragma once

#include <cstdint>

#include "mods/api.h"

struct HookService;
struct LogService;

extern bool g_compassEnabled;
extern bool g_showMinimap;
extern bool g_showChestMarkers;
extern bool g_showVerticalIndicators;
extern bool g_edgeFade;
extern int64_t g_compassX;
extern int64_t g_compassY;
extern int64_t g_compassWidth;
extern int64_t g_compassHeight;
extern int64_t g_compassScale;
extern int64_t g_compassOpacity;
extern int64_t g_backgroundOpacity;
extern bool g_showLocationName;
extern int64_t g_locationX;
extern int64_t g_locationY;
extern int64_t g_locationSize;
extern int64_t g_locationNameOpacity;
extern int64_t g_locationAlignment;

ModResult init_compass(const HookService* hookSvc, ModError* error);
void update_compass(const LogService* logSvc, ModContext* ctx);
void shutdown_compass();
void set_compass_minimap_visibility(bool visible);
