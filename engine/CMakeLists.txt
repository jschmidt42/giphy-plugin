cmake_minimum_required(VERSION 3.6)
project(engine_plugin)

# Include common plugin CMake scripts and set the type of plugin to be to and ENGINE_PLUGIN
set(ENGINE_PLUGIN ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${REPOSITORY_DIR}/cmake")
include(CMakePlugin)

# Scan and add project source files
find_source_files(ALL_SOURCE_FILES)

# Add windows version resource if windows dll
if( PLATFORM_WINDOWS )
	if( BUILD_SHARED_LIBS )
		include_directories(${PROJECT_SOURCE_DIR})
		configure_file("${PROJECT_SOURCE_DIR}/${PROJECT_NAME}.rc.in" "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.rc")
		set(RESOURCE_FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.rc")
		source_group("Resources" FILES ${RESOURCE_FILES})
		list(APPEND ALL_SOURCE_FILES ${RESOURCE_FILES})
	endif()
endif()

# Define automatic namespace for C++
add_compile_options(-DPLUGIN_NAMESPACE=${PROJECT_NAME})

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

# Create target and set compile/link options
add_library(${PROJECT_NAME} ${ALL_SOURCE_FILES})

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
set_system_properties(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "${ENGINE_PLUGINS_FOLDER_NAME}")
set(TARGET_BASE_NAME "${PROJECT_NAME}_${ENGINE_PLUGIN_SUFFIX}_$<LOWER_CASE:$<CONFIG>>")
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${TARGET_BASE_NAME}")
if( BUILD_SHARED_LIBS AND PLATFORM_IOS )
	set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${ENGINE_IOS_CODE_SIGN_IDENTITY}")
endif()


# Set engine runtime plugin properties and enable hot-reloading.
set_plugin_runtime_output_directory("${TARGET_BASE_NAME}" "${ENGINE_PLUGINS_INSTALL_DIR}")
