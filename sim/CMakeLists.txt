cmake_minimum_required(VERSION 3.16)
project(kronograf_sim C)

set(CMAKE_C_STANDARD 11)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(SDL2 REQUIRED)

# LVGL : compilée avec le même lv_conf.h que la montre (KG_SIM désactive le swap RGB565).
file(GLOB_RECURSE LVGL_SRC ${ROOT}/lib/lvgl/src/*.c)
add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl PUBLIC ${ROOT}/lib/lvgl ${ROOT}/include)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE KG_SIM)
target_compile_options(lvgl PRIVATE -w)

file(GLOB_RECURSE UI_SRC ${ROOT}/src/ui/*.c)
add_executable(kronograf_sim main.c ${ROOT}/src/hal/sim/hal_sim.c ${UI_SRC})
target_include_directories(kronograf_sim PRIVATE ${ROOT}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kronograf_sim PRIVATE lvgl SDL2::SDL2)
# Notre code compile sans avertissement (les polices générées sont exclues du -Werror).
file(GLOB UI_OWN ${ROOT}/src/ui/*.c)
set_source_files_properties(main.c ${ROOT}/src/hal/sim/hal_sim.c ${UI_OWN} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
