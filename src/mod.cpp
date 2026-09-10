#include "compass.hpp"

#include <algorithm>

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.h"
#include "mods/svc/log.h"
#include "mods/svc/ui.h"

DEFINE_MOD();

IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_OPTIONAL_SERVICE(ConfigService, svc_config);
IMPORT_OPTIONAL_SERVICE(UiService, svc_ui);

static ConfigVarHandle s_enabled = 0;
static ConfigVarHandle s_minimap = 0;
static ConfigVarHandle s_chests = 0;
static ConfigVarHandle s_vertical = 0;
static ConfigVarHandle s_edgeFade = 0;
static ConfigVarHandle s_x = 0;
static ConfigVarHandle s_y = 0;
static ConfigVarHandle s_width = 0;
static ConfigVarHandle s_height = 0;
static ConfigVarHandle s_scale = 0;
static ConfigVarHandle s_opacity = 0;
static ConfigVarHandle s_backgroundOpacity = 0;
static ConfigVarHandle s_locationName = 0;
static ConfigVarHandle s_locationX = 0;
static ConfigVarHandle s_locationY = 0;
static ConfigVarHandle s_locationSize = 0;
static ConfigVarHandle s_locationNameOpacity = 0;
static ConfigVarHandle s_locationAlignment = 0;

static void onEnabled(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_compassEnabled = value->bool_value;
}

static void onMinimap(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (!value) return;
    g_showMinimap = value->bool_value;
    set_compass_minimap_visibility(g_showMinimap);
}
static void onChests(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_showChestMarkers = value->bool_value;
}
static void onVertical(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_showVerticalIndicators = value->bool_value;
}
static void onEdgeFade(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_edgeFade = value->bool_value;
}
static void onX(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_compassX = value->int_value;
}
static void onY(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_compassY = value->int_value;
}
static void onWidth(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_compassWidth = value->int_value;
}
static void onHeight(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_compassHeight = value->int_value;
}
static void onScale(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_compassScale = value->int_value;
}
static void onOpacity(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_compassOpacity = value->int_value;
}
static void onBackgroundOpacity(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_backgroundOpacity = value->int_value;
}

static void onLocationName(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_showLocationName = value->bool_value;
}
static void onLocationX(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_locationX = value->int_value;
}
static void onLocationY(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_locationY = value->int_value;
}
static void onLocationSize(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_locationSize = value->int_value;
}
static void onLocationNameOpacity(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_locationNameOpacity = value->int_value;
}
static void onLocationAlignment(ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    if (value) g_locationAlignment = std::clamp<int64_t>(value->int_value, 0, 2);
}

static void registerBool(const char* name, bool def, ConfigVarHandle* handle,
                         void (*callback)(ModContext*, ConfigVarHandle, const ConfigVarValue*, const ConfigVarValue*, void*)) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = CONFIG_VAR_BOOL;
    desc.default_bool = def;

    if (svc_config->register_var(mod_ctx, &desc, handle) == MOD_OK) {
        bool value = def;
        if (svc_config->get_bool(mod_ctx, *handle, &value) == MOD_OK && callback) {
            ConfigVarValue v{};
            v.bool_value = value;
            callback(mod_ctx, *handle, &v, nullptr, nullptr);
        }
        if (callback) svc_config->subscribe(mod_ctx, *handle, callback, nullptr, nullptr);
    }
}

static void registerInt(const char* name, int64_t def, ConfigVarHandle* handle,
                        void (*callback)(ModContext*, ConfigVarHandle, const ConfigVarValue*, const ConfigVarValue*, void*)) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = CONFIG_VAR_INT;
    desc.default_int = def;

    if (svc_config->register_var(mod_ctx, &desc, handle) == MOD_OK) {
        int64_t value = def;
        if (svc_config->get_int(mod_ctx, *handle, &value) == MOD_OK && callback) {
            ConfigVarValue v{};
            v.int_value = value;
            callback(mod_ctx, *handle, &v, nullptr, nullptr);
        }
        if (callback) svc_config->subscribe(mod_ctx, *handle, callback, nullptr, nullptr);
    }
}


static void resetDefaults(ModContext*, void*) {
    if (!svc_config) return;

    svc_config->set_bool(mod_ctx, s_enabled, true);
    svc_config->set_bool(mod_ctx, s_minimap, false);
    svc_config->set_bool(mod_ctx, s_chests, true);
    svc_config->set_bool(mod_ctx, s_vertical, true);
    svc_config->set_bool(mod_ctx, s_edgeFade, true);
    svc_config->set_int(mod_ctx, s_x, -95);
    svc_config->set_int(mod_ctx, s_y, 15);
    svc_config->set_int(mod_ctx, s_width, 400);
    svc_config->set_int(mod_ctx, s_height, 16);
    svc_config->set_int(mod_ctx, s_scale, 100);
    svc_config->set_int(mod_ctx, s_opacity, 100);
    svc_config->set_int(mod_ctx, s_backgroundOpacity, 60);
    svc_config->set_bool(mod_ctx, s_locationName, true);
    svc_config->set_int(mod_ctx, s_locationX, 0);
    svc_config->set_int(mod_ctx, s_locationY, 36);
    svc_config->set_int(mod_ctx, s_locationSize, 100);
    svc_config->set_int(mod_ctx, s_locationNameOpacity, 100);
    svc_config->set_int(mod_ctx, s_locationAlignment, 1);
}

static ModResult buildUi(ModContext*, UiElementHandle panel, void*, ModError*) {
    if (!svc_ui) return MOD_OK;

    {
        UiControlDesc reset = UI_CONTROL_DESC_INIT;
        reset.kind = UI_CONTROL_BUTTON;
        reset.label = "Reset to defaults";
        reset.on_pressed = resetDefaults;
        svc_ui->pane_add_control(mod_ctx, panel, &reset, nullptr);
    }

    svc_ui->pane_add_section(mod_ctx, panel, "Compass");

    auto toggle = [&](const char* label, ConfigVarHandle handle) {
        if (!handle) return;
        UiControlDesc desc = UI_CONTROL_DESC_INIT;
        desc.kind = UI_CONTROL_TOGGLE;
        desc.label = label;
        desc.binding = UI_BINDING_CONFIG_VAR;
        desc.config_var = handle;
        svc_ui->pane_add_control(mod_ctx, panel, &desc, nullptr);
    };

    auto number = [&](const char* label, ConfigVarHandle handle, int min, int max, int step, const char* suffix) {
        if (!handle) return;
        UiControlDesc desc = UI_CONTROL_DESC_INIT;
        desc.kind = UI_CONTROL_NUMBER;
        desc.label = label;
        desc.binding = UI_BINDING_CONFIG_VAR;
        desc.config_var = handle;
        desc.min = min;
        desc.max = max;
        desc.step = step;
        desc.suffix = suffix;
        svc_ui->pane_add_control(mod_ctx, panel, &desc, nullptr);
    };

    toggle("Show Compass", s_enabled);

    number("X position", s_x, -999, 999, 1, " px");
    number("Y position", s_y, 0, 999, 1, " px");
    number("Width", s_width, 200, 1000, 10, " px");
    number("Height", s_height, 16, 80, 2, " px");
    number("Scale", s_scale, 50, 200, 5, "%");
    number("Compass opacity", s_opacity, 0, 100, 5, "%");
    number("Background opacity", s_backgroundOpacity, 0, 100, 5, "%");
    toggle("Fade edges", s_edgeFade);

    svc_ui->pane_add_section(mod_ctx, panel, "Dungeon Chest Markers");
    toggle("Show dungeon chest markers", s_chests);
    toggle("Show above/below indicators", s_vertical);

    svc_ui->pane_add_section(mod_ctx, panel, "Location name");
    toggle("Show location name", s_locationName);
    number("X position", s_locationX, -999, 999, 1, " px");
    number("Y position", s_locationY, 0, 999, 1, " px");
    number("Size", s_locationSize, 50, 200, 5, "%");
    number("Location Name Opacity", s_locationNameOpacity, 0, 100, 5, "%");

    svc_ui->pane_add_text(mod_ctx, panel, "Location Name Alignment", nullptr);
    auto alignmentButton = [&](const char* label, int value) {
        UiControlDesc desc = UI_CONTROL_DESC_INIT;
        desc.kind = UI_CONTROL_BUTTON;
        desc.label = label;
        desc.on_pressed = [](ModContext*, void* user_data) {
            const int64_t alignment = static_cast<int64_t>(reinterpret_cast<intptr_t>(user_data));
            if (svc_config) svc_config->set_int(mod_ctx, s_locationAlignment, alignment);
        };
        desc.is_selected = [](ModContext*, void* user_data) {
            const int64_t alignment = static_cast<int64_t>(reinterpret_cast<intptr_t>(user_data));
            return g_locationAlignment == alignment;
        };
        desc.user_data = reinterpret_cast<void*>(static_cast<intptr_t>(value));
        svc_ui->pane_add_control(mod_ctx, panel, &desc, nullptr);
    };
    alignmentButton("Left-Aligned", 0);
    alignmentButton("Center-Aligned", 1);
    alignmentButton("Right-Aligned", 2);

    svc_ui->pane_add_section(mod_ctx, panel, "Minimap");
    toggle("Show Minimap", s_minimap);

    return MOD_OK;
}

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    if (!svc_hook) return mods::set_error(error, MOD_ERROR, "HookService unavailable");

    if (svc_config) {
        registerBool("enabled", true, &s_enabled, onEnabled);
        registerBool("showVanillaMinimap", false, &s_minimap, onMinimap);
        registerBool("showChestMarkers", true, &s_chests, onChests);
        registerBool("showVerticalIndicators", true, &s_vertical, onVertical);
        registerBool("edgeFade", true, &s_edgeFade, onEdgeFade);
        registerInt("compassX", -95, &s_x, onX);
        registerInt("compassY", 15, &s_y, onY);
        registerInt("compassWidth", 400, &s_width, onWidth);
        registerInt("compassHeight", 16, &s_height, onHeight);
        registerInt("compassScale", 100, &s_scale, onScale);
        registerInt("compassOpacity", 100, &s_opacity, onOpacity);
        registerInt("backgroundOpacity", 60, &s_backgroundOpacity, onBackgroundOpacity);
        registerBool("showLocationName", true, &s_locationName, onLocationName);
        registerInt("locationNameX", 0, &s_locationX, onLocationX);
        registerInt("locationNameY", 36, &s_locationY, onLocationY);
        registerInt("locationNameSize", 100, &s_locationSize, onLocationSize);
        registerInt("locationNameOpacity", 100, &s_locationNameOpacity, onLocationNameOpacity);
        registerInt("locationNameAlignment", 1, &s_locationAlignment, onLocationAlignment);
    }

    if (svc_ui) {
        UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
        panel.build = buildUi;
        svc_ui->register_mods_panel(mod_ctx, &panel);
    }

    ModResult result = init_compass(svc_hook, error);
    if (result == MOD_OK && svc_log) svc_log->info(mod_ctx, "Twilight Compass v1.0 initialized");
    return result;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    update_compass(svc_log, mod_ctx);
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    shutdown_compass();
    return MOD_OK;
}
}

extern "C" MOD_EXPORT const void* const g_keep_mod_records[] = {
    &mod_meta_header_record,
    &mod_meta_import_svc_log,
    &mod_meta_import_svc_hook,
    &mod_meta_import_svc_config,
    &mod_meta_import_svc_ui,
};
