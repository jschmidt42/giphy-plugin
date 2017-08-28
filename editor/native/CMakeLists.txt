cmake_minimum_required(VERSION 3.6)
project(editor_plugin)

# Include common plugin CMake scripts and set the type of plugin to be to and EDITOR_PLUGIN
set(EDITOR_PLUGIN ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${REPOSITORY_DIR}/cmake")
include(CMakePlugin)

# Define automatic namespace for C++
add_compile_options(-DPLUGIN_NAMESPACE=${PROJECT_NAME})

# Scan and add project source files
find_source_files(ALL_SOURCE_FILES)

# Include editor plugin sdk files
include_directories(${REPOSITORY_DIR}/stingray_sdk)

# Fetch stb as an external library.
include(ExternalProject)
set(STB_INSTALL_LOCATION "${PROJECT_BINARY_DIR}/external/stb")
set(STB_INSTALL_LOCATION_LIB_DIR "${STB_INSTALL_LOCATION}/lib")
list(APPEND CMAKE_ARGS "-D${CACHE_VAR}${CACHE_VAR_TYPE}=${${CACHE_VAR}}")
ExternalProject_Add(external_stb
    GIT_REPOSITORY https://github.com/jschmidt42/stb-cmake.git
    CMAKE_ARGS "${CMAKE_ARGS};-DCMAKE_INSTALL_PREFIX=${STB_INSTALL_LOCATION}"
    UPDATE_COMMAND ""
)

# Setup plugin shared library
add_library(${PROJECT_NAME} SHARED ${ALL_SOURCE_FILES})

# Setup stb component
add_library(stb STATIC IMPORTED)
set_property(TARGET stb PROPERTY IMPORTED_LOCATION_DEBUG "${STB_INSTALL_LOCATION_LIB_DIR}/debug/stb${LIB_SUFFIX}")
set_property(TARGET stb PROPERTY IMPORTED_LOCATION_DEV "${STB_INSTALL_LOCATION_LIB_DIR}/dev/stb${LIB_SUFFIX}")
set_property(TARGET stb PROPERTY IMPORTED_LOCATION_RELEASE "${STB_INSTALL_LOCATION_LIB_DIR}/release/stb${LIB_SUFFIX}")
add_dependencies(stb external_stb)
add_dependencies(${PROJECT_NAME} stb)
include_directories(${STB_INSTALL_LOCATION}/include)
target_link_libraries(${PROJECT_NAME} stb)

# Set target properties
set(TARGET_BASE_NAME "${PROJECT_NAME}_${ENGINE_PLUGIN_SUFFIX}_$<LOWER_CASE:$<CONFIG>>")
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${TARGET_BASE_NAME}")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EDITOR_PLUGINS_INSTALL_DIR}")
