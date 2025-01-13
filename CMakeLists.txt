# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2021 NKI/AVL, Netherlands Cancer Institute

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:

# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.

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
project(phenosaurus VERSION 3.0.1 LANGUAGES CXX C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CheckFunctionExists)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CMakePackageConfigHelpers)
include(FindFilesystem)
include(FindPkgConfig)
include(GenerateExportHeader)
include(AddGitSubmodule)

set(CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Filesystem REQUIRED)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers")
elseif(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
endif()

# Create a revision file, containing the current git version info
include(VersionString)
write_version_header()

# Optionally use mrc to create resources
find_package(Mrc QUIET)

if(MRC_FOUND)
	option(USE_RSRC "Use mrc to create resources" ON)
else()
	message(STATUS "Not using resources since mrc was not found")
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

find_package(zeep 6.0.16 REQUIRED)
find_package(Boost REQUIRED)
find_package(gxrio REQUIRED)
find_package(mcfp REQUIRED)
find_package(libpqxx 7.7.4 REQUIRED)

pkg_check_modules(PKG_PQ REQUIRED IMPORTED_TARGET libpq)

add_git_submodule(squeeze EXCLUDE_FROM_ALL)
add_git_submodule(mailio EXCLUDE_FROM_ALL)

find_program(YARN yarn REQUIRED)

# yarn rules for javascripts
set(webpack_input
	webapp/admin-user.js
	webapp/compare-screens.js
	webapp/context-menu.js
	webapp/create-screen.js
	webapp/dot-context-menu.js
	webapp/dot.js
	webapp/edit-screen.js
	webapp/find-cluster.js
	webapp/finder.js
	webapp/find-gene.js
	webapp/find-similar.js
	webapp/gene-info.js
	webapp/gene-line.js
	webapp/gene-selection.js
	webapp/genome-browser.js
	webapp/genome-viewer.js
	webapp/index.js
	webapp/list-screen.js
	webapp/multidot.js
	webapp/pvformat.js
	webapp/qc.js
	webapp/screen-color-map.js
	webapp/screen-data.js
	webapp/screen.js
	webapp/screen-plot.js
	webapp/script.js
	webapp/sl-dot.js
	webapp/sl-screen.js
	webapp/sortable.js
	webapp/table-utils.js
	webapp/tooltip.js
	webapp/sa-style.scss
	webapp/signin.css
	webapp/styles.css)

set(webpack_output ${CMAKE_SOURCE_DIR}/docroot/dist/index.js)

add_custom_command(
	OUTPUT ${webpack_output}
	BYPRODUCTS
	DEPENDS ${webpack_input}
	COMMAND ${YARN} run build
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_custom_target(webpack DEPENDS ${webpack_output})

set(CONFIG_PATH "${CMAKE_INSTALL_FULL_SYSCONFDIR}")
set(CONFIG_FILE "${CMAKE_INSTALL_FULL_SYSCONFDIR}/screen-analyzer.conf")
set(SCREEN_ANALYZER_EXE "${CMAKE_INSTALL_FULL_SBINDIR}/screen-analyzer")
set(SCREEN_ANALYZER_PID "/var/run/screen-analyzer")

configure_file(${PROJECT_SOURCE_DIR}/src/config.hpp.in
	${PROJECT_SOURCE_DIR}/src/config.hpp @ONLY)

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

target_include_directories(screen-analyzer PRIVATE zeep::zeep squeeze::squeeze ${CMAKE_BINARY_DIR})
target_link_libraries(screen-analyzer
	PRIVATE zeep::zeep squeeze::squeeze mcfp::mcfp gxrio::gxrio Boost::headers
	PkgConfig::PKG_PQ mailio::mailio ZLIB::ZLIB libpqxx::pqxx
	std::filesystem ${CMAKE_THREAD_LIBS_INIT})

add_dependencies(screen-analyzer webpack)

if(USE_RSRC)
	mrc_target_resources(screen-analyzer ${CMAKE_SOURCE_DIR}/docroot/ ${CMAKE_SOURCE_DIR}/rsrc/)
endif()

install(TARGETS screen-analyzer DESTINATION ${CMAKE_INSTALL_SBINDIR})

configure_file(screen-analyzer.service.in ${CMAKE_CURRENT_BINARY_DIR}/screen-analyzer.service @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/screen-analyzer.service" DESTINATION "/etc/systemd/system")
