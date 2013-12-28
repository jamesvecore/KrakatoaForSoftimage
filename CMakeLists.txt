cmake_minimum_required (VERSION 2.8)

project (SoftimageKrakatoa)

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/CMake/Modules)

find_package (Softimage REQUIRED)

message (STATUS "---configuring (SoftimageKrakatoa) plugin---")

set (KRAKATOA_SR_SDK_DIR
	$ENV{KRAKATOA_SR_SDK}
	CACHE
	PATH
	"Krakatoa SR C++ SDK Directory"
)

set (KRAKATOA_SR_INCLUDE_DIR
	${KRAKATOA_SR_SDK_DIR}/include
)

if (WIN32)
	set (KRAKATOA_SR_LIB_DIR ${KRAKATOA_SR_SDK_DIR}/lib-windows-x64)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else ()
	set (KRAKATOA_SR_LIB_DIR ${KRAKATOA_SR_SDK_DIR}/lib-linux-x64)
endif ()

set (INCLUDE_DIRS
	${KRAKATOA_SR_INCLUDE_DIR}
)

set (LINK_DIRS
	${KRAKATOA_SR_LIB_DIR}
)

include_directories (${INCLUDE_DIRS})

link_directories (${LINK_DIRS})

set (SOURCES
 KrakatoaRendererPlugin.cpp
)

set (HEADERS
)

set (LINK_LIBS
 KrakatoaSR
)

add_softimage_plugin (SoftimageKrakatoa ${SOURCES} ${HEADERS})
add_softimage_scripted_plugins (KrakatoaPropertyPlugin.py)

target_link_libraries (SoftimageKrakatoa ${LINK_LIBS})

if (WIN32)
	add_custom_command(TARGET SoftimageKrakatoa POST_BUILD
		COMMAND xcopy \"${KRAKATOA_SR_LIB_DIR}/*.dll\" \"$<TARGET_FILE_DIR:SoftimageKrakatoa>\" /F /Y)
endif ()