#include "compass.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "global.h"

#include "mods/svc/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/log.h"

#include "d/d_com_inf_game.h"
#include "d/d_meter2_draw.h"
#include "d/d_meter2.h"
#include "d/d_meter_map.h"
#include "d/d_meter2_info.h"
#include "d/d_s_play.h"
#include "d/d_tresure.h"
#include "dusk/map_loader_definitions.h"
#include "dusk/settings.h"
#include "d/actor/d_a_alink.h"
#include "f_op/f_op_camera_mng.h"
#include "m_Do/m_Do_lib.h"
#include "JSystem/JUtility/JUTFont.h"
#include "JSystem/J2DGraph/J2DOrthoGraph.h"
#include "JSystem/JUtility/TColor.h"
#include <dolphin/gx.h>

bool g_compassEnabled = true;
bool g_showMinimap = true;
bool g_showChestMarkers = true;
bool g_showVerticalIndicators = true;
bool g_edgeFade = true;
int64_t g_compassX = 0;
int64_t g_compassY = 38;
int64_t g_compassWidth = 620;
int64_t g_compassHeight = 34;
int64_t g_compassScale = 100;
int64_t g_compassOpacity = 85;
int64_t g_backgroundOpacity = 42;
bool g_showLocationName = true;
int64_t g_locationX = 0;
int64_t g_locationY = 36;
int64_t g_locationSize = 100;
int64_t g_locationNameOpacity = 100;
int64_t g_locationAlignment = 1;

static dusk::UserSettings& (*s_getSettings)() = nullptr;

DEFINE_HOOK(&dMeter2Draw_c::draw, Meter2DrawHook);
DEFINE_HOOK(&dMeterMap_c::draw, MeterMapDrawHook);
DEFINE_HOOK(&dMeterMap_c::keyCheck, MeterMapKeyCheckHook);
DEFINE_HOOK(&dMeter2_c::_execute, Meter2ExecuteHook);

namespace {

struct CompassState {
    cXyz playerPos{};
    cXyz cameraDir{};
    f32 cameraYaw = 0.0f;
    bool mirrorMode = false;
    bool dungeonCompass = false;
    bool valid = false;
};

CompassState s_state{};
constexpr f32 kPi = 3.14159265358979323846f;

static f32 wrapPi(f32 a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

static u8 alphaMul(u8 alpha, f32 multiplier) {
    return static_cast<u8>(std::clamp(alpha * multiplier, 0.0f, 255.0f));
}

static void drawText(const char* text, f32 x, f32 y, f32 w, f32 h, JUtility::TColor color) {
    JUTFont* font = mDoExt_getSubFont();
    if (!font) font = mDoExt_getMesgFont();
    if (!font) return;
    font->setGX();
    font->setCharColor(JUtility::TColor(0, 0, 0, color.a / 2));
    font->drawString_scale(x + 1.0f, y + 1.0f, w, h, text, true);
    font->setCharColor(color);
    font->drawString_scale(x, y, w, h, text, true);
    if (auto* port = dComIfGp_getCurrentGrafPort()) port->setup2D();
}

static const MapEntry* currentMapEntry(const char* stageName, int roomNo, const char** regionOut) {
    if (stageName == nullptr) return nullptr;
    for (const auto& region : gameRegions) {
        for (const auto& map : region.maps) {
            if (map.mapFile == nullptr || std::strcmp(map.mapFile, stageName) != 0) continue;
            for (const auto& room : map.mapRooms) {
                if (static_cast<int>(room.roomNo) == roomNo) {
                    if (regionOut) *regionOut = region.regionName;
                    return &map;
                }
            }
        }
    }
    return nullptr;
}

static const char* currentLocationName() {
    const char* stageName = dComIfGp_getStartStageName();
    if (stageName == nullptr || stageName[0] == '\0') return nullptr;
    const int roomNo = static_cast<int>(dComIfGp_roomControl_getStayNo());
    const auto* map = currentMapEntry(stageName, roomNo, nullptr);
    return map != nullptr ? map->mapName : nullptr;
}

static void updateState() {
    s_state = {};

    // Use the stable gameplay player/camera access path. Avoid generic player
    // service lookups here because this HUD hook can run during transitions.
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (!link) return;

    s_state.playerPos = link->current.pos;

    // Camera 0 is the normal gameplay camera for both human and Wolf Link.
    // Using it directly avoids transient player-camera bookkeeping during
    // human/Wolf transitions.
    camera_class* camera = dComIfGp_getCamera(0);
    if (!camera) return;

    cXyz* eye = fopCamM_GetEye_p(camera);
    cXyz* center = fopCamM_GetCenter_p(camera);
    if (!eye || !center) return;

    s_state.cameraDir = *center - *eye;
    if (std::fabs(s_state.cameraDir.x) + std::fabs(s_state.cameraDir.z) < 0.001f) return;
		s_state.cameraYaw = std::atan2(s_state.cameraDir.x, s_state.cameraDir.z);

	if (s_getSettings != nullptr) {
		s_state.mirrorMode = s_getSettings().game.enableMirrorMode.getValue();

		if (s_state.mirrorMode) {
        s_state.cameraYaw = -s_state.cameraYaw;
		}
	}

	s_state.valid = true;

    s_state.dungeonCompass = dComIfGs_isDungeonItemCompass() != 0;


}

static f32 edgeVisibility(f32 x, f32 centerX, f32 halfWidth) {
    if (!g_edgeFade) return 1.0f;
    const f32 distance = std::fabs(x - centerX);
    const f32 fadeWidth = std::max(24.0f, halfWidth * 0.16f);
    const f32 fadeStart = std::max(0.0f, halfWidth - fadeWidth);
    if (distance <= fadeStart) return 1.0f;
    return std::clamp((halfWidth - distance) / fadeWidth, 0.0f, 1.0f);
}

static void drawCompass() {
    if (!s_state.valid) return;

    const f32 scale = std::clamp(static_cast<f32>(g_compassScale) / 100.0f, 0.5f, 2.0f);
    const f32 width = std::max(200.0f, static_cast<f32>(g_compassWidth)) * scale;
    const f32 height = std::max(16.0f, static_cast<f32>(g_compassHeight)) * scale;
    const f32 centerX = mDoGph_gInf_c::getWidthF() * 0.5f + static_cast<f32>(g_compassX);
    const f32 y = static_cast<f32>(g_compassY);
    const f32 half = width * 0.5f;
    const u8 alpha = static_cast<u8>(std::clamp<int64_t>(g_compassOpacity, 0, 100) * 2.55f);
    const u8 backgroundAlpha = static_cast<u8>(std::clamp<int64_t>(g_backgroundOpacity, 0, 100) * 2.55f);
    const u8 locationNameAlpha = static_cast<u8>(std::clamp<int64_t>(g_locationNameOpacity, 0, 100) * 2.55f);
    const f32 pxPerRad = width / (2.0f * kPi);

    if (g_compassEnabled) {
        // Draw the background as one continuous GX draw call. The center uses
    // the selected opacity while both edges fade smoothly to transparent.
    auto drawBackground = [&]() {
        const f32 fadeWidth = g_edgeFade ? std::max(24.0f, half * 0.16f) : 0.0f;
        const f32 fadeStart = std::max(0.0f, half - fadeWidth);
        const f32 left = centerX - half;
        const f32 leftFadeEnd = centerX - fadeStart;
        const f32 rightFadeStart = centerX + fadeStart;
        const f32 right = centerX + half;

        if (backgroundAlpha == 0) return;

        if (!g_edgeFade) {
            J2DFillBox(left, y, width, height, JUtility::TColor(0, 0, 0, backgroundAlpha));
            return;
        }

        auto* port = dComIfGp_getCurrentGrafPort();
        if (!port) return;

        port->setup2D();
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GXSetNumChans(1);
        GXSetNumTexGens(0);
        GXSetNumTevStages(1);
        GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);

        const auto vertex = [](f32 px, f32 py, u8 a) {
            GXPosition3f32(px, py, 0.0f);
            GXColor4u8(0, 0, 0, a);
        };

        GXBegin(GX_QUADS, GX_VTXFMT0, 12);
        // Left fade: transparent -> full background opacity.
        vertex(left, y, 0);
        vertex(leftFadeEnd, y, backgroundAlpha);
        vertex(leftFadeEnd, y + height, backgroundAlpha);
        vertex(left, y + height, 0);
        // Solid center.
        vertex(leftFadeEnd, y, backgroundAlpha);
        vertex(rightFadeStart, y, backgroundAlpha);
        vertex(rightFadeStart, y + height, backgroundAlpha);
        vertex(leftFadeEnd, y + height, backgroundAlpha);
        // Right fade: full background opacity -> transparent.
        vertex(rightFadeStart, y, backgroundAlpha);
        vertex(right, y, 0);
        vertex(right, y + height, 0);
        vertex(rightFadeStart, y + height, backgroundAlpha);
        GXEnd();
        port->setup2D();
    };
    drawBackground();

    // Every tick has a world angle and is projected exactly like N/E/S/W.
    constexpr int ticks = 48;
    for (int i = -ticks; i <= ticks; ++i) {
        const f32 worldAngle = static_cast<f32>(i) * (kPi / 24.0f);
        const f32 relative = wrapPi(s_state.cameraYaw - worldAngle);
        const f32 x = centerX + relative * pxPerRad;
        if (x < centerX - half || x > centerX + half) continue;

        const bool major = (i % 6) == 0;
        const f32 tickHeight = major ? height * 0.32f : height * 0.17f;
        const f32 fade = edgeVisibility(x, centerX, half);
        J2DFillBox(x - 0.5f, y + height - tickHeight, 1.0f, tickHeight,
                   JUtility::TColor(220, 205, 165, alphaMul(alpha, 0.8f * fade)));
    }

    auto drawDirection = [&](const char* label, f32 worldAngle) {
        const f32 relative = wrapPi(s_state.cameraYaw - worldAngle);
        const f32 x = centerX + relative * pxPerRad;
        const f32 visibility = edgeVisibility(x, centerX, half);
        if (visibility <= 0.01f) return;
        const f32 w = 13.0f * scale;
        const f32 textHeight = 16.0f * scale;
        drawText(label, x - w * 0.5f, y + height * 0.10f, w, textHeight,
                 JUtility::TColor(245, 235, 205, alphaMul(alpha, visibility)));
    };

    // Twilight Princess world compass: North is world -Z.
    drawDirection("N", kPi);
    drawDirection("E", kPi * 0.5f);
    drawDirection("S", 0.0f);
    drawDirection("W", -kPi * 0.5f);

    // Chest markers stay deliberately compact so the compass does not become
    // a quest-marker HUD.
    if (g_showChestMarkers && s_state.dungeonCompass) {
        dTres_c::typeGroupData_c* chest = dTres_c::getFirstData(0);
        while (chest != nullptr) {
            const int chestNo = chest->getNo();
            if (!dComIfGs_isTbox(chestNo)) {
                const auto* rawPos = chest->getPos();
                if (rawPos != nullptr) {
                    const cXyz chestPos(rawPos->x, rawPos->y, rawPos->z);
                    const f32 dx = chestPos.x - s_state.playerPos.x;
					const f32 dz = chestPos.z - s_state.playerPos.z;
					const f32 distance = std::sqrt(dx * dx + dz * dz);

					f32 worldAngle = std::atan2(dx, dz);
					if (s_state.mirrorMode) {
					worldAngle = -worldAngle;
					}

					const f32 relative = wrapPi(s_state.cameraYaw - worldAngle);
                    const f32 x = centerX + relative * pxPerRad;
                    if (x >= centerX - half && x <= centerX + half) {
                        const bool sameRoom = chest->getRoomNo() == static_cast<int>(dComIfGp_roomControl_getStayNo());
                        const f32 heightDiff = chestPos.y - s_state.playerPos.y;
                        const f32 edgeFade = edgeVisibility(x, centerX, half);
                        const f32 distanceFade = std::clamp(1.0f - (distance / 5000.0f), 0.35f, 1.0f);
                        const f32 roomFade = sameRoom ? 1.0f : 0.58f;
                        const f32 markerFade = edgeFade * distanceFade * roomFade;
                        const f32 markerScale = std::clamp(1.0f - (distance / 6000.0f), 0.65f, 1.0f) * scale;
                        const f32 markerSize = 7.0f * markerScale;
                        const f32 markerY = y + height * 0.48f;
                        const u8 markerAlpha = alphaMul(alpha, markerFade);

                        auto* markerPort = dComIfGp_getCurrentGrafPort();
                        if (markerPort && markerAlpha > 3) {
                            GXClearVtxDesc();
                            GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
                            GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
                            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
                            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
                            GXSetNumChans(1);
                            GXSetNumTexGens(0);
                            GXSetNumTevStages(1);
                            GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
                            GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
                            GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
                            GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
                            GXSetCullMode(GX_CULL_NONE);

                            const f32 cx = x;
                            const f32 cy = markerY;
                            const f32 halfMarker = markerSize * 0.5f;
                            GXBegin(GX_QUADS, GX_VTXFMT0, 4);
                            GXPosition3f32(cx, cy - halfMarker, 0.0f);
                            GXColor4u8(255, 220, 120, markerAlpha);
                            GXPosition3f32(cx + halfMarker, cy, 0.0f);
                            GXColor4u8(255, 220, 120, markerAlpha);
                            GXPosition3f32(cx, cy + halfMarker, 0.0f);
                            GXColor4u8(255, 220, 120, markerAlpha);
                            GXPosition3f32(cx - halfMarker, cy, 0.0f);
                            GXColor4u8(255, 220, 120, markerAlpha);
                            GXEnd();
                            markerPort->setup2D();

                            if (g_showVerticalIndicators && std::fabs(heightDiff) > 90.0f) {
                                // Draw a small open chevron as the vertical cue.
                                const bool above = heightDiff > 0.0f;
                                const f32 chevronWidth = 9.0f * markerScale;
                                const f32 chevronHeight = 5.0f * markerScale;
                                const f32 gap = 2.0f * markerScale;
                                const f32 cy = above
                                    ? markerY - markerSize * 0.5f - gap - chevronHeight * 0.5f
                                    : markerY + markerSize * 0.5f + gap + chevronHeight * 0.5f;
                                auto* indicatorPort = dComIfGp_getCurrentGrafPort();
                                if (indicatorPort) {
                                    indicatorPort->setup2D();
                                    GXClearVtxDesc();
                                    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
                                    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
                                    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
                                    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
                                    GXSetNumChans(1);
                                    GXSetNumTexGens(0);
                                    GXSetNumTevStages(1);
                                    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
                                    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
                                    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
                                    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
                                    GXSetCullMode(GX_CULL_NONE);
                                    GXSetLineWidth(static_cast<u8>(std::clamp(4.0f * markerScale, 2.0f, 6.0f)), GX_TO_ZERO);

                                    const f32 halfW = chevronWidth * 0.5f;
                                    const f32 top = cy - chevronHeight * 0.5f;
                                    const f32 bottom = cy + chevronHeight * 0.5f;
                                    const u8 indicatorAlpha = alphaMul(markerAlpha, 0.95f);
                                    GXBegin(GX_LINES, GX_VTXFMT0, 4);
                                    if (above) {
                                        GXPosition3f32(x - halfW, bottom, 0.0f);
                                        GXColor4u8(245, 235, 205, indicatorAlpha);
                                        GXPosition3f32(x, top, 0.0f);
                                        GXColor4u8(245, 235, 205, indicatorAlpha);
                                        GXPosition3f32(x, top, 0.0f);
                                        GXColor4u8(245, 235, 205, indicatorAlpha);
                                        GXPosition3f32(x + halfW, bottom, 0.0f);
                                        GXColor4u8(245, 235, 205, indicatorAlpha);
                                    } else {
                                        GXPosition3f32(x - halfW, top, 0.0f);
                                        GXColor4u8(245, 235, 205, indicatorAlpha);
                                        GXPosition3f32(x, bottom, 0.0f);
                                        GXColor4u8(245, 235, 205, indicatorAlpha);
                                        GXPosition3f32(x, bottom, 0.0f);
                                        GXColor4u8(245, 235, 205, indicatorAlpha);
                                        GXPosition3f32(x + halfW, top, 0.0f);
                                        GXColor4u8(245, 235, 205, indicatorAlpha);
                                    }
                                    GXEnd();
                                    indicatorPort->setup2D();
                                }
                            }
                        }
                    }
                }
            }
            chest = dTres_c::getNextData(chest);
        }
    }

    }

    if (g_showLocationName) {
        const char* location = currentLocationName();
        if (location != nullptr && location[0] != '\0') {
            const f32 locationScale = std::clamp(static_cast<f32>(g_locationSize) / 100.0f, 0.5f, 2.0f) * scale;
            const f32 locationY = static_cast<f32>(g_locationY);
            const f32 textW = 12.0f * locationScale;
            const f32 textH = 12.0f * locationScale;
            const f32 approxWidth = static_cast<f32>(std::strlen(location)) * 7.0f * locationScale;
            const f32 locationAnchor = mDoGph_gInf_c::getWidthF() * 0.5f + static_cast<f32>(g_locationX);
            f32 locationX = locationAnchor - approxWidth * 0.5f;
            if (g_locationAlignment <= 0) {
                locationX = locationAnchor;
            } else if (g_locationAlignment >= 2) {
                locationX = locationAnchor - approxWidth;
            }
            drawText(location, locationX, locationY,
                     textW, textH, JUtility::TColor(245, 235, 205, locationNameAlpha));
        }
    }

    // Standalone center pointer, with no stem.
    const JUtility::TColor pointerColor(255, 245, 190, alpha);
    const f32 pointerWidth = 8.0f * scale;
    const f32 pointerTop = y - 1.0f * scale;
    const f32 pointerHeadBottom = pointerTop + 6.0f * scale;
    auto* pointerPort = dComIfGp_getCurrentGrafPort();
    if (pointerPort) {
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GXSetNumChans(1);
        GXSetNumTexGens(0);
        GXSetNumTevStages(1);
        GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);

        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition3f32(centerX - pointerWidth * 0.5f, pointerTop, 0.0f);
        GXColor4u8(255, 245, 190, alpha);
        GXPosition3f32(centerX + pointerWidth * 0.5f, pointerTop, 0.0f);
        GXColor4u8(255, 245, 190, alpha);
        GXPosition3f32(centerX, pointerHeadBottom, 0.0f);
        GXColor4u8(255, 245, 190, alpha);
        GXEnd();
        pointerPort->setup2D();
    }

}

static HookAction onMeterMapDrawPre(ModContext*, void* args, void*, void*) {
    if (!g_showMinimap && args != nullptr) {
        auto* map = *static_cast<dMeterMap_c**>(args);
        if (map != nullptr) {
            // Keep the minimap in the same hidden state as vanilla Left-D-pad.
            map->setDispPosOutsideFlg_SE_On();
        }
        return HOOK_SKIP_ORIGINAL;
    }

    return HOOK_CONTINUE;
}

static HookAction onMeterMapKeyCheckPre(ModContext*, void* args, void*, void*) {
    if (g_showMinimap || args == nullptr) return HOOK_CONTINUE;

    // With the setting off, Left-D-pad does nothing while Right-D-pad keeps
    // vanilla's larger-map behavior through the game's map-move path.
    auto* map = *static_cast<dMeterMap_c**>(args);
    if (map != nullptr) {
        map->setDispPosOutsideFlg_SE_On();
        dMeterMap_c::meter_map_move(0);
    }

    return HOOK_SKIP_ORIGINAL;
}

static void onMeter2ExecutePost(ModContext*, void*, void*, void*) {
    // After the large map closes, keep the minimap object hidden when the
    // setting is off. Running this after the normal HUD update lets vanilla
    // large-map processing finish first.
    if (!g_showMinimap) {
        const u8 mapStatus = dMeter2Info_getMapStatus();
        if (mapStatus == 0 || mapStatus == 1) {
            if (dMeterMap_c* map = dMeter2Info_getMeterMapClass()) {
                if (map->isDispPosInsideFlg()) {
                    map->setDispPosOutSide();
                }
            }
        }
    }
}


static void onMeterDrawPost(ModContext*, void*, void*, void*) {
    if (!g_compassEnabled) return;

    // Avoid drawing while pause/save-load UI is initializing, when the
    // player and camera may not yet be usable.
    if (dComIfGp_isPauseFlag() || dScnPly_c::isPause()) return;
    if (!dComIfGp_getCurrentGrafPort()) return;

    updateState();
    drawCompass();
}

} // namespace

void set_compass_minimap_visibility(bool visible) {
    if (dMeterMap_c* map = dMeter2Info_getMeterMapClass()) {
        if (visible) {
            // Restore vanilla minimap presentation and D-pad map state.
            map->setDispPosInsideFlg_SE_On();
            dMeter2Info_setMapStatus(1);
        } else {
            // Hide the minimap without touching the large-map screen state.
            map->setDispPosOutsideFlg_SE_On();
            map->setDispPosOutSide();
            dMeter2Info_setMapStatus(0);
        }
    }
}

ModResult init_compass(const HookService* hookSvc, ModContext* ctx, ModError* error) {
    if (!hookSvc) return MOD_OK;
	
	void* settingsAddress = nullptr;
ModResult settingsResult =
    hookSvc->resolve(ctx, "dusk::getSettings", &settingsAddress, nullptr);

if (settingsResult == MOD_OK) {
    s_getSettings =
        reinterpret_cast<dusk::UserSettings& (*)()>(settingsAddress);
	}

    ModResult result = mods::hook::add_pre<MeterMapDrawHook>(hookSvc, onMeterMapDrawPre);
    if (result != MOD_OK) return result;

    result = mods::hook::add_pre<MeterMapKeyCheckHook>(hookSvc, onMeterMapKeyCheckPre);
    if (result != MOD_OK) return result;

    result = mods::hook::add_post<Meter2ExecuteHook>(hookSvc, onMeter2ExecutePost);
    if (result != MOD_OK) return result;

    return mods::hook::add_post<Meter2DrawHook>(hookSvc, onMeterDrawPost);
}

void update_compass(const LogService*, ModContext*) {}
void shutdown_compass() {
    s_state = {};
    s_getSettings = nullptr;
}
