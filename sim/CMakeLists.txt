cmake_minimum_required(VERSION 3.16)
project(kronograf_sim C)

set(CMAKE_C_STANDARD 11)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(SDL2 REQUIRED)

# LVGL: built with the watch's lv_conf.h (KG_SIM disables the RGB565 byte swap).
file(GLOB_RECURSE LVGL_SRC ${ROOT}/lib/lvgl/src/*.c)
add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl PUBLIC ${ROOT}/lib/lvgl ${ROOT}/include)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE KG_SIM)
target_compile_options(lvgl PRIVATE -w)

file(GLOB_RECURSE UI_SRC ${ROOT}/src/ui/*.c)
file(GLOB DATA_SRC ${ROOT}/src/data/*.c)
add_executable(kronograf_sim main.c ${ROOT}/src/hal/sim/hal_sim.c ${ROOT}/src/hal/sim/hal_sim_net.c ${ROOT}/src/hal/sim/hal_sim_media.c
  ${ROOT}/src/third_party/tjpgd/tjpgd.c ${UI_SRC} ${DATA_SRC})
target_compile_definitions(kronograf_sim PRIVATE KG_ROOT="${ROOT}")
target_include_directories(kronograf_sim PRIVATE ${ROOT}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(kronograf_sim PRIVATE ${ROOT}/include)
target_link_libraries(kronograf_sim PRIVATE lvgl SDL2::SDL2 pthread)
if(UNIX AND NOT APPLE)
  target_link_libraries(kronograf_sim PRIVATE m)  # libm is implicit on macOS, not on Linux
endif()
# Our code builds warning-free (generated fonts are excluded from -Werror).
file(GLOB UI_OWN ${ROOT}/src/ui/*.c)
set_source_files_properties(main.c ${ROOT}/src/hal/sim/hal_sim.c ${ROOT}/src/hal/sim/hal_sim_net.c ${ROOT}/src/hal/sim/hal_sim_media.c ${UI_OWN} ${DATA_SRC} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
