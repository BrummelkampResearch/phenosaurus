# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2021 NKI/AVL, Netherlands Cancer Institute

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:

# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.

# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.15)

# set the project name
project(screen-analyzer VERSION 2.0.3 LANGUAGES CXX C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CheckFunctionExists)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CMakePackageConfigHelpers)
include(Dart)
include(FindFilesystem)
include(FindPkgConfig)
include(GenerateExportHeader)

set(CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Filesystem REQUIRED)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
endif()

if(NOT "$ENV{CCP4}" STREQUAL "")
	set(CCP4 $ENV{CCP4})
	list(PREPEND CMAKE_MODULE_PATH "${CCP4}/Lib")
	list(APPEND CMAKE_PREFIX_PATH ${CCP4})
	if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
		set(CMAKE_PREFIX_PATH ${CCP4})
	endif()
endif()

if(MSVC)
    # make msvc standards compliant...
    add_compile_options(/permissive-)

	macro(get_WIN32_WINNT version)
		if (WIN32 AND CMAKE_SYSTEM_VERSION)
			set(ver ${CMAKE_SYSTEM_VERSION})
			string(REPLACE "." "" ver ${ver})
			string(REGEX REPLACE "([0-9])" "0\\1" ver ${ver})

			set(${version} "0x${ver}")
		endif()
	endmacro()

	get_WIN32_WINNT(ver)
	add_definitions(-D_WIN32_WINNT=${ver})

	# On Windows, do not install in the system location
	if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND NOT BUILD_FOR_CCP4)
		message(STATUS "The library and auxiliary files will be installed in $ENV{LOCALAPPDATA}/${PROJECT_NAME}")
		set(CMAKE_INSTALL_PREFIX "$ENV{LOCALAPPDATA}/${PROJECT_NAME}" CACHE PATH "..." FORCE)
	endif()

	# Find out the processor type for the target
	if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64")
		set(COFF_TYPE "x64")
	elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "i386")
		set(COFF_TYPE "x86")
	elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "ARM64")
		set(COFF_TYPE "arm64")
	else()
		message(FATAL_ERROR "Unsupported or unknown processor type ${CMAKE_SYSTEM_PROCESSOR}")
	endif()	
endif()

if(UNIX AND NOT APPLE)
	# On Linux, install in the $HOME/.local folder by default
	if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
		message(WARNING "The library and auxiliary files will be installed in $ENV{HOME}/.local")
		set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "..." FORCE)
	endif()
endif()

# Create a revision file, containing the current git version info
include(VersionString)
write_version_header()

# Optionally use mrc to create resources
find_package(Mrc)

if(MRC_FOUND)
	option(USE_RSRC "Use mrc to create resources" ON)
else()
	message(WARNING "Not using resources since mrc was not found")
endif()

if(USE_RSRC STREQUAL "ON")
	set(USE_RSRC 1)

	message("Using resources compiled with ${MRC}")
	add_compile_definitions(USE_RSRC WEBAPP_USES_RESOURCES)

	mrc_write_header(${CMAKE_BINARY_DIR}/mrsrc.hpp)
endif()

set(CMAKE_THREAD_PREFER_PTHREAD)
set(THREADS_PREFER_PTHREAD_FLAG)
find_package(Threads)

set(Boost_DETAILED_FAILURE_MSG ON)
if(NOT BUILD_SHARED_LIBS)
	set(Boost_USE_STATIC_LIBS ON)
endif()

find_package(zeep 5.1.3 REQUIRED)
find_package(Boost COMPONENTS system date_time program_options regex iostreams)
find_package(squeeze 1.0.0 REQUIRED)

pkg_check_modules(PQ libpq REQUIRED)
pkg_check_modules(PQXX libpqxx>=7.2 REQUIRED)
pkg_check_modules(MAILIO mailio REQUIRED)
pkg_check_modules(SSL libssl)
pkg_check_modules(CRYPTO libcrypto)
pkg_check_modules(ZLIB zlib)
# pkg_check_modules(BZIP2 libbz2)
find_library(BZIP2_LIBRARIES bz2)

find_program(YARN yarn REQUIRED)

# yarn rules for javascripts

set(webpack_input
	webapp/context-menu.js
	webapp/dot.js
	webapp/genome-viewer.js
	webapp/index.js
	webapp/multidot.js
	webapp/screenColorMap.js
	webapp/screenData.js
	webapp/script.js
	webapp/tooltip.js
	webapp/admin-group.js
	webapp/admin-user.js
	webapp/gene-selection.js
	webapp/gene-finder.js
	webapp/finder.js
	webapp/similar-finder.js
	webapp/cluster-finder.js
	webapp/compare-3.js
	webapp/genome-browser.js
	webapp/qc.js
	webapp/create-screen.js
	webapp/dot-context-menu.js
	webapp/edit-screen.js
	webapp/list-screen.js
	webapp/screen.js
	webapp/geneInfo.js
	webapp/screenPlot.js
	webapp/sortable.js
	webapp/pvformat.js
	webapp/sl-dot.js
	webapp/sl-screen.js
	scss/contextMenu.css
	scss/sa-style.scss
	scss/signin.css
	scss/styles.css)

set(webpack_output ${CMAKE_SOURCE_DIR}/docroot/scripts/index.js)

add_custom_command(
	OUTPUT ${webpack_output}
	BYPRODUCTS 
	DEPENDS ${webpack_input}
	COMMAND ${YARN} run build
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_custom_target(webpack DEPENDS ${webpack_output})

add_executable(screen-analyzer
	${CMAKE_SOURCE_DIR}/src/refseq.cpp
	${CMAKE_SOURCE_DIR}/src/job-scheduler.cpp
	${CMAKE_SOURCE_DIR}/src/utils.hpp
	${CMAKE_SOURCE_DIR}/src/screen-qc.cpp
	${CMAKE_SOURCE_DIR}/src/bowtie.hpp
	${CMAKE_SOURCE_DIR}/src/db-connection.hpp
	${CMAKE_SOURCE_DIR}/src/screen-creator.hpp
	${CMAKE_SOURCE_DIR}/src/screen-service.hpp
	${CMAKE_SOURCE_DIR}/src/screen-data.cpp
	${CMAKE_SOURCE_DIR}/src/fisher.hpp
	${CMAKE_SOURCE_DIR}/src/screen-server.cpp
	${CMAKE_SOURCE_DIR}/src/config.hpp
	${CMAKE_SOURCE_DIR}/src/screen-analyzer.cpp
	${CMAKE_SOURCE_DIR}/src/job-scheduler.hpp
	${CMAKE_SOURCE_DIR}/src/user-service.hpp
	${CMAKE_SOURCE_DIR}/src/screen-creator.cpp
	${CMAKE_SOURCE_DIR}/src/screen-data.hpp
	${CMAKE_SOURCE_DIR}/src/utils.cpp
	${CMAKE_SOURCE_DIR}/src/screen-service.cpp
	${CMAKE_SOURCE_DIR}/src/user-service.cpp
	${CMAKE_SOURCE_DIR}/src/screen-analyzer.hpp
	${CMAKE_SOURCE_DIR}/src/bowtie.cpp
	${CMAKE_SOURCE_DIR}/src/bsd-closefrom.h
	${CMAKE_SOURCE_DIR}/src/db-connection.cpp
	${CMAKE_SOURCE_DIR}/src/genome-browser.cpp
	${CMAKE_SOURCE_DIR}/src/fisher.cpp
	${CMAKE_SOURCE_DIR}/src/binom.cpp
	${CMAKE_SOURCE_DIR}/src/refseq.hpp
	${CMAKE_SOURCE_DIR}/src/genome-browser.hpp
	${CMAKE_SOURCE_DIR}/src/screen-qc.hpp
	${CMAKE_SOURCE_DIR}/src/binom.hpp
	${CMAKE_SOURCE_DIR}/src/screen-server.hpp
	${CMAKE_SOURCE_DIR}/src/bsd-closefrom.c)

target_include_directories(screen-analyzer PRIVATE zeep::zeep squeeze::squeeze ${CMAKE_SOURCE_DIR}/include ${Boost_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})
target_link_libraries(screen-analyzer
	PRIVATE zeep::zeep Boost::date_time Boost::program_options Boost::regex Boost::iostreams
	${PQXX_LIBRARIES} ${PQ_LIBRARIES} ${MAILIO_LINK_LIBRARIES} ${SSL_LIBRARIES} ${CRYPTO_LIBRARIES} ${ZLIB_LIBRARIES} ${BZIP2_LIBRARIES} std::filesystem
	${CMAKE_THREAD_LIBS_INIT})

add_dependencies(screen-analyzer webpack)

if(USE_RSRC)
	mrc_target_resources(screen-analyzer ${CMAKE_SOURCE_DIR}/docroot/ ${CMAKE_SOURCE_DIR}/rsrc/)
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${BIN_INSTALL_DIR}
)

# manual
# ...

# # test

# add_executable(unit-test ${PROJECT_SOURCE_DIR}/test/unit-test.cpp ${PROJECT_SOURCE_DIR}/src/dssp.cpp ${SA_RESOURCE})

# target_include_directories(unit-test PRIVATE
# 	${CMAKE_CURRENT_SOURCE_DIR}/src
# 	${CMAKE_CURRENT_SOURCE_DIR}/include
# )

# target_link_libraries(unit-test Threads::Threads cifpp::cifpp Boost::date_time)

# if(MSVC)
# 	# Specify unwind semantics so that MSVC knowns how to handle exceptions
# 	target_compile_options(unit-test PRIVATE /EHsc)
# endif()

# enable_testing()

# add_test(NAME unit-test
# 	COMMAND $<TARGET_FILE:unit-test>
# 	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/test)

# option(DSSP_BUILD_INSTALLER "Build an installer" OFF)
# if(DSSP_BUILD_INSTALLER)
# 	include(InstallRequiredSystemLibraries)
# 	set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
	
# 	# NSIS options
# 	set(CPACK_NSIS_MODIFY_PATH ON)

# 	# configuration done, include CPack

# 	include(CPack)
# endif()
