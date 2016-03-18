cmake_minimum_required (VERSION 2.8)
project("sview")

find_package(Cairo REQUIRED)
find_package(Gstreamer-1.0 REQUIRED)
find_package(GLIB REQUIRED COMPONENTS gobject gmodule gthread gio)
find_package(EGL REQUIRED)
find_package(OpenGLES2 REQUIRED)
find_package(PTHREAD REQUIRED)
find_package(Wayland REQUIRED)
find_package(Spnav QUIET)
find_package(SV REQUIRED)

if(SPNAV_FOUND)
    add_definitions(-DSPACENAV_ENABLED)
endif()

if (COMPILE_WITH_PRIVATE)
    add_subdirectory(private)
    add_definitions(-DCOMPILE_WITH_PRIVATE)
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/private/include")
    set(PRIVATE_LIBRARIES utest-private)
endif()

message(STATUS "PRIVATE_INCLUDE_DIR: " ${PRIVATE_INCLUDE_DIR})

set (PROJECT_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")
set (PROJECT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

message(STATUS "Surround-view unit-test is enabled for compiling")

# ...add sources
file(GLOB UTEST_SRC
"${PROJECT_SOURCE_DIR}/utest-main.c"
"${PROJECT_SOURCE_DIR}/utest-common.c"
"${PROJECT_SOURCE_DIR}/utest-sv.c"
"${PROJECT_SOURCE_DIR}/utest-gui.c"
"${PROJECT_SOURCE_DIR}/utest-vin.c"
"${PROJECT_SOURCE_DIR}/utest-video-decoder.c"
"${PROJECT_SOURCE_DIR}/utest-vsink.c"
"${PROJECT_SOURCE_DIR}/utest-display-wayland.c"
"${PROJECT_SOURCE_DIR}/utest-display.c"
)

MESSAGE(STATUS "APP_C_SRC: " ${UTEST_SRC}) 

SET(GLIB_LIBS 
    ${GLIB_LIBRARIES} 
    ${GLIB_GIO_LIBRARIES} 
    ${GLIB_GOBJECT_LIBRARIES} 
    ${GLIB_GTHREAD_LIBRARIES} 
    ${GLIB_GMODULE_LIBRARIES} 
    )

MESSAGE(STATUS "GLIB_INCLUDE_DIR: " ${GLIB_INCLUDE_DIR})
MESSAGE(STATUS "GLIB_CONFIG_INCLUDE_DIR: " ${GLIBCONFIG_INCLUDE_DIR})
MESSAGE(STATUS "CAIRO_INCLUDE_DIRS: " ${CAIRO_INCLUDE_DIRS})
MESSAGE(STATUS "GSTREAMER_INCLUDE_DIR: " ${GSTREAMER_INCLUDE_DIR}) 

include_directories(
                    ${PROJECT_INCLUDE_DIR}
                    ${PRIVATE_INCLUDE_DIR}
                    ${GSTREAMER_INCLUDE_DIR} 
                    ${GLIB_INCLUDE_DIR}
                    ${GLIBCONFIG_INCLUDE_DIR}
                    ${CAIRO_INCLUDE_DIRS}
		            ${SV_INCLUDE_DIRS}
                    )

add_executable(${PROJECT_NAME} ${UTEST_SRC})
target_link_libraries(${PROJECT_NAME}
  ${SV_LIBRARIES}
  ${CAIRO_LIBRARIES}
  ${GSTREAMER_LIBRARIES}
  ${GLIB_LIBS}
  ${EGL_LIBRARIES}
  ${OPENGLES2_LIBRARY}
  ${PTHREAD_LIBRARIES}
  ${SPNAV_LIBRARIES}
  ${WAYLAND_LIBRARIES}
  ${PRIVATE_LIBRARIES}
)

if(CMAKE_BUILD_TOOL MATCHES "(msdev|devenv|nmake|MSBuild)")
    target_compile_options(${PROJECT_NAME} PUBLIC  /W2)
else()
    target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wextra -Wno-unused-parameter)
endif()


install(TARGETS ${PROJECT_NAME} DESTINATION bin)
