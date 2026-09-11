# Copyright (c) 2012-2025 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

#
# webos-connman-adapter/CMakeLists.txt
#


cmake_minimum_required(VERSION 3.10)

project(webos-connman-adapter C)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC4)
webos_component(1 1 0)

set(WIFI_IFACE_NAME "wlan0" CACHE STRING "WiFi interface name")
set(WIRED_IFACE_NAME "eth0" CACHE STRING "Wired interface name")
set(CELLULAR_IFACE_NAME "rmnet_usb0" CACHE STRING "Cellular interface name")
set(CONNMAN_CONFIG_DIR "/var/lib/connman" CACHE STRING "Default connman config folder")

find_program(GDBUS_CODEGEN_EXECUTABLE NAMES gdbus-codegen DOC "gdbus-codegen executable")
if(NOT GDBUS_CODEGEN_EXECUTABLE)
	message(FATAL_ERROR "Executable gdbus-codegen not found")
endif()

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS_OTHER})

pkg_check_modules(LUNASERVICE2 REQUIRED luna-service2)
include_directories(${LUNASERVICE2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNASERVICE2_CFLAGS_OTHER})

pkg_check_modules(GIO-UNIX REQUIRED gio-unix-2.0)
include_directories(${GIO-UNIX_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GIO-UNIX_CFLAGS_OTHER})

pkg_check_modules(PBNJSON_C REQUIRED pbnjson_c)
include_directories(${PBNJSON_C_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PBNJSON_C_CFLAGS_OTHER})

pkg_check_modules(OPENSSL REQUIRED openssl)
include_directories(${OPENSSL_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${OPENSSL_CFLAGS_OTHER})

pkg_check_modules(LUNAPREFS REQUIRED luna-prefs)
include_directories(${LUNAPREFS_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNAPREFS_CFLAGS_OTHER})

pkg_check_modules(PMLOG REQUIRED PmLogLib)
include_directories(${PMLOG_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PMLOG_CFLAGS_OTHER})

pkg_check_modules(WCA_SUPPORT REQUIRED wca-support)
include_directories(${WCA_SUPPORT_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${WCA_SUPPORT_CFLAGS_OTHER})

pkg_check_modules(WCA_SUPPORT_API REQUIRED wca-support-api>=0.1.0)
include_directories(${WCA_SUPPORT_API_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${WCA_SUPPORT_API_CFLAGS_OTHER})

webos_add_compiler_flags(ALL -Wall -g)

webos_add_linker_options(ALL --no-undefined)

# --- Testing & hardening options -------------------------------------------

# Treat warnings as errors (useful in CI; off by default so new toolchains
# with new diagnostics don't break existing builds).
set(ENABLE_WERROR FALSE CACHE BOOL "Treat compiler warnings as errors")
if (ENABLE_WERROR)
	webos_add_compiler_flags(ALL -Werror)
endif()

# Address/undefined sanitizers for development builds (host or on-target
# debugging). Not for production images.
set(ENABLE_SANITIZERS FALSE CACHE BOOL "Build with AddressSanitizer and UBSan")
if (ENABLE_SANITIZERS)
	webos_add_compiler_flags(ALL -fsanitize=address,undefined
	                             -fno-omit-frame-pointer -O1)
	string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=address,undefined")
endif()

# Baseline hardening for builds whose toolchain does not already inject the
# distro hardening flags (OE/Yocto passes its own via CFLAGS; these guards
# avoid duplicating them there).
if (NOT CMAKE_C_FLAGS MATCHES "-fstack-protector")
	webos_add_compiler_flags(ALL -fstack-protector-strong)
endif()
if (NOT CMAKE_C_FLAGS MATCHES "FORTIFY_SOURCE" AND CMAKE_BUILD_TYPE MATCHES "Rel")
	webos_add_compiler_flags(ALL -D_FORTIFY_SOURCE=2)
endif()

enable_testing()

# ----------------------------------------------------------------------------

set(GDBUS_IF_DIR ${WEBOS_BINARY_CONFIGURED_DIR}/src)

file (MAKE_DIRECTORY ${GDBUS_IF_DIR})

execute_process(COMMAND ${GDBUS_CODEGEN_EXECUTABLE} --c-namespace ConnmanInterface --generate-c-code
			connman-interface --interface-prefix net.connman.
			${CMAKE_CURRENT_SOURCE_DIR}/files/xml/connman.xml
			WORKING_DIRECTORY ${GDBUS_IF_DIR}
			RESULT_VARIABLE codegen_failed)
if(codegen_failed)
		message(FATAL_ERROR "Error in generating code for connman interface using gdbus-codegen")
endif()

execute_process(COMMAND ${GDBUS_CODEGEN_EXECUTABLE} --c-namespace PacrunnerInterface --generate-c-code
			pacrunner-interface --interface-prefix org.pacrunner.
			${CMAKE_CURRENT_SOURCE_DIR}/files/xml/pacrunner.xml
			WORKING_DIRECTORY ${GDBUS_IF_DIR}
			RESULT_VARIABLE codegen_failed)
if(codegen_failed)
		message(FATAL_ERROR "Error in generating code for pacrunner interface using gdbus-codegen")
endif()


set(ENABLE_SINGLE_PROFILE FALSE CACHE BOOL "Enable support for maintaining only a single profile")
if (ENABLE_SINGLE_PROFILE)
webos_add_compiler_flags(ALL -DENABLE_SINGLE_PROFILE)
endif()

if (ENABLE_QUICK_WOL)
webos_add_compiler_flags(ALL -DENABLE_QUICK_WOL)
endif()

if (ENABLE_SCAN_ON_SOFTAP)
webos_add_compiler_flags(ALL -DENABLE_SCAN_ON_SOFTAP)
endif()

if (MULTIPLE_ROUTING_TABLE)
webos_add_compiler_flags(ALL -DMULTIPLE_ROUTING_TABLE)
endif()

include_directories(src ${GDBUS_IF_DIR})
webos_configure_header_files(src)

add_subdirectory(src/wfdsie)
# plain set() instead of file(GLOB): no wildcards are used, and GLOB
# silently dropped missing files (src/state_recovery.c was listed here for
# years without existing)
set(SOURCE_FILES
    src/common.c
    src/connectionmanager_service.c
    src/connman_agent.c
    src/connman_counter.c
    src/connman_group.c
    src/connman_manager.c
    src/connman_service.c
    src/connman_service_discovery.c
    src/connman_technology.c
    src/json_utils.c
    src/lunaservice_utils.c
    src/main.c
    src/pacrunner_client.c
    src/utils.c
    src/wan_service.c
    src/wifi_p2p_service.c
    src/wifi_tethering_service.c
    src/wifi_profile.c
    src/wifi_service.c
    src/wifi_ap_info.c
    src/wifi_setting.c
    src/wifi_scan.c
    ${GDBUS_IF_DIR}/connman-interface.c
    ${GDBUS_IF_DIR}/pacrunner-interface.c)

add_executable(webos-connman-adapter ${SOURCE_FILES})
target_link_libraries(webos-connman-adapter
                        ${GLIB2_LDFLAGS}
                        ${LUNASERVICE2_LDFLAGS}
                        ${GIO-UNIX_LDFLAGS}
                        ${PBNJSON_C_LDFLAGS}
                        ${OPENSSL_LDFLAGS}
                        ${LUNAPREFS_LDFLAGS}
                        ${PMLOG_LDFLAGS}
                        ${WCA_SUPPORT_LDFLAGS}
                        rt
                        pthread
                        wfdsie)

webos_build_daemon()
webos_build_system_bus_files()

webos_config_build_doxygen(doc Doxyfile)

add_subdirectory(tests)
