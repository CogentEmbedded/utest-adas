cmake_minimum_required (VERSION 2.8)
project(adas-utest)

# search for additional CMake modules
SET(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake-modules"
)

MESSAGE(STATUS "CMAKE_FIND_ROOT_PATH:" ${CMAKE_FIND_ROOT_PATH})

# ...unit-test application
add_subdirectory("utest")

set(INSTALL_BIN_DIR bin CACHE PATH "Installation directory for executables")
set(INSTALL_LIB_DIR lib CACHE PATH "Installation directory for libraries")
set(INSTALL_INC_DIR include CACHE PATH "Installation directory for headers")
set(INSTALL_DATA_DIR share/${PROJECT_NAME} CACHE PATH "Installation directory for data files")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
