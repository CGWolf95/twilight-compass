cmake_minimum_required(VERSION 3.26)
project(dusklight_twilight_compass CXX)

if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "d281c34a65568ee7be2319985f3a290c0ef65abc")
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

add_mod(dusklight_twilight_compass
        FEATURES game
        SOURCES
            src/mod.cpp
            src/compass.cpp
        MOD_JSON mod.json
        RES_DIR res
)

if (MSVC)
    target_link_options(dusklight_twilight_compass PRIVATE "/OPT:NOREF")
endif ()


# The current-location display reuses Dusklight's pinned map/region definitions.
# The pinned revision makes this deterministic while avoiding a duplicated map database.
target_include_directories(dusklight_twilight_compass PRIVATE
        "${DUSKLIGHT_DIR}/src"
)
