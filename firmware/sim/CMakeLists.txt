cmake_minimum_required(VERSION 3.16)
project(pt_sim C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(LVGL ${ROOT}/managed_components/lvgl__lvgl)

# All LVGL core/draw/widgets/libs sources, minus the platform display/touch drivers (need SDL/X11).
file(GLOB_RECURSE LVGL_SRC ${LVGL}/src/*.c)
list(FILTER LVGL_SRC EXCLUDE REGEX "/src/drivers/")

set(APP_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/sim_main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/sim_stubs.c
  ${ROOT}/main/ui.c
  ${ROOT}/main/skin.c
  ${ROOT}/main/pt_ic_nozzle.c
  ${ROOT}/main/pt_ic_bed.c
  ${ROOT}/main/pt_ic_speed.c
  ${ROOT}/main/pt_core_one.c
  ${ROOT}/main/pt_core_one_l.c
  ${ROOT}/main/pt_mini.c
  ${ROOT}/main/pt_mk4s.c
  ${ROOT}/main/pt_xl.c
  ${ROOT}/main/pt_fluidd.c
)

add_executable(pt_sim ${APP_SRC} ${LVGL_SRC})
target_compile_definitions(pt_sim PRIVATE LV_CONF_INCLUDE_SIMPLE _DEFAULT_SOURCE PP_HOST_SIM)
# ui.c is device code; don't fail the host build on its warnings.
target_compile_options(pt_sim PRIVATE -w)
target_include_directories(pt_sim PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}            # lv_conf.h
  ${CMAKE_CURRENT_SOURCE_DIR}/shim       # esp_*.h host shims
  ${ROOT}/main
  ${ROOT}/components/PandaTouch_IDF/include
  ${LVGL}
  ${LVGL}/src
)
target_link_libraries(pt_sim m)
