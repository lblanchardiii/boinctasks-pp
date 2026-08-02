cmake_minimum_required(VERSION 3.16)
project(boinctasks-pp C CXX)

# Without RC in the enabled languages CMake silently ignores .rc files, and the
# manifest never reaches the binary - which costs you themed controls and DPI
# awareness with no error to explain why.
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The one place the release version is written. It reaches the About box, the
# update check and the Windows version block from here. It used to be spelled
# out separately in bt_main.cpp, boinctasks.rc and installer.nsi, and had
# already drifted between them.
#
# A test build appends a letter: 0.9.4b. Releases never carry one. The letter
# keeps test builds from eating release numbers, and dpkg orders them correctly
# (0.9.4 < 0.9.4b < 0.9.4c < 0.9.5).
#
# Only a single letter, a-z. "0.9.4aa" sorts *below* "0.9.4z" in dpkg's
# comparison, so a double-letter suffix would silently break upgrade ordering;
# past z, bump the number instead.
#
if(NOT DEFINED BT_VERSION)
    set(BT_VERSION "0.9.5")
endif()

# Windows version resources and NSIS accept four integers and nothing else, so
# the letter cannot appear there. It becomes the fourth field instead, which
# was previously a wasted constant zero: 0.9.4b -> 0,9,4,2. A release keeps 0.
if(NOT BT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)([a-z]?)$")
    message(FATAL_ERROR
        "BT_VERSION must be 1.2.3 or 1.2.3x with a single lowercase letter; got '${BT_VERSION}'")
endif()
set(_bt_major "${CMAKE_MATCH_1}")
set(_bt_minor "${CMAKE_MATCH_2}")
set(_bt_patch "${CMAKE_MATCH_3}")
set(_bt_letter "${CMAKE_MATCH_4}")
set(_bt_build 0)
if(_bt_letter)
    string(FIND "abcdefghijklmnopqrstuvwxyz" "${_bt_letter}" _bt_idx)
    math(EXPR _bt_build "${_bt_idx} + 1")
endif()
set(BT_VERSION_COMMA "${_bt_major},${_bt_minor},${_bt_patch},${_bt_build}")
set(BT_VERSION_NUM   "${_bt_major}.${_bt_minor}.${_bt_patch}.${_bt_build}")
message(STATUS "BoincTasks++ version ${BT_VERSION} (numeric ${BT_VERSION_NUM})")

# xml: reading Classic's computers.xml for the import
find_package(wxWidgets 3.2 REQUIRED COMPONENTS xml core base)
include(${wxWidgets_USE_FILE})

set(BT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# MSVC secure-CRT names appear in vendored headers; make the shims universal
add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/compat/msvc_crt_shim.h)

# On Linux the compat dir goes first so #include <winsock2.h> in unmodified
# sources resolves to our shim. On Windows the real headers must win, so the
# compat dir goes last - the shims disable themselves there anyway.
if(WIN32)
    # BOINC's socket code only compiles its Winsock paths - including the
    # WSAStartup wrapper - when this is defined.
    #
    # WIN32_LEAN_AND_MEAN stops windows.h dragging in Winsock 1, which would
    # then collide with the Winsock 2 headers the sockets actually use.
    # boinc_win.h includes windows.h without it, so it has to be set globally.
    #
    # _WIN32_WINNT 0x0601 pins the API level at Windows 7: high enough for
    # everything used here, low enough not to bar older systems.
    add_compile_definitions(USE_WINSOCK WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
    include_directories(
        ${BT_ROOT}/BoincTasks
        ${BT_ROOT}/BoincLibrary
        ${CMAKE_CURRENT_SOURCE_DIR}/compat
    )
else()
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/compat
        ${BT_ROOT}/BoincTasks
        ${BT_ROOT}/BoincLibrary
    )
endif()

set_source_files_properties(${BT_ROOT}/BoincLibrary/Boinc/md5.c PROPERTIES LANGUAGE C)

# ---- BOINC's own cross-platform gui-rpc library ------------------------------
# The only path to a client. Phase 1 went through eFMer's CRpcClient wrapper,
# kept compiling on Linux by a shim over the Win32 socket calls; nothing uses it
# now, so neither it nor the shim is built.
add_library(boinclib STATIC
    ${BT_ROOT}/BoincLibrary/Boinc/gui_rpc_client.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/gui_rpc_client_ops.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/gui_rpc_client_print.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/network.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/miofile.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/mfile.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/parse.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/str_util.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/util.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/md5_file.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/md5.c
    ${BT_ROOT}/BoincLibrary/Boinc/proxy_info.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/prefs.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/notice.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/hostinfo.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/cc_config.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/coproc.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/filesys.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/url.cpp
)

# daemonising, fork/setsid - Unix only, and not needed by a GUI client anyway
if(NOT WIN32)
    target_sources(boinclib PRIVATE ${BT_ROOT}/BoincLibrary/Boinc/unix_util.cpp)
endif()
target_include_directories(boinclib PUBLIC ${BT_ROOT}/BoincLibrary/Boinc)

# ---- headless smoke test ----------------------------------------------------
# Connects to a live client and reads its status: the quickest way to tell a
# broken build from a broken BOINC configuration.
add_executable(bt_boincrpc_test tests/bt_boincrpc_test.cpp)
target_link_libraries(bt_boincrpc_test boinclib ${wxWidgets_LIBRARIES})

# ---- Phase 2: wxWidgets GUI skeleton ----------------------------------------
add_executable(boinctasks
    gui/bt_main.cpp
    gui/bt_config.cpp
    gui/bt_poller.cpp
    gui/bt_views.cpp
    gui/bt_prefs.cpp
    gui/bt_rules.cpp
    gui/bt_rulesdlg.cpp
    gui/bt_taskmodel.cpp
    gui/bt_history.cpp
    gui/bt_scan.cpp
    gui/bt_graphs.cpp
    gui/bt_settings.cpp
    gui/bt_addproject.cpp
    gui/bt_freedc.cpp
    gui/bt_import.cpp
    gui/bt_estimate.cpp
    gui/bt_taskinfo.cpp
)
target_compile_definitions(boinctasks PRIVATE BT_VERSION_STR="${BT_VERSION}")

if(WIN32)
    # The resource script carries the manifest (themed controls + per-monitor
    # DPI), the application icon and the version block Explorer shows.
    target_sources(boinctasks PRIVATE packaging/win/boinctasks.rc)

    # BT_VERSION_COMMA already carries all four fields, letter included as the
    # build number. The .rc keeps matching defaults for a bare windres run.
    set_source_files_properties(packaging/win/boinctasks.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/win/boinctasks.manifest"
        COMPILE_DEFINITIONS "BT_VERSION_STR=\\\"${BT_VERSION}\\\";BT_VERSION_COMMA=${BT_VERSION_COMMA}")

    # SQLite is cross-compiled from the amalgamation; there is no MinGW package
    target_include_directories(boinctasks PRIVATE ${BT_SQLITE_INCLUDE})
    target_link_libraries(boinctasks boinclib ${wxWidgets_LIBRARIES}
                          ${BT_SQLITE_LIB} ws2_32)
    # GUI subsystem: no console window behind the application
    set_target_properties(boinctasks PROPERTIES WIN32_EXECUTABLE TRUE
                          OUTPUT_NAME "BoincTasksPP")
    # carry the runtime in the binary so there is nothing to install alongside
    target_link_options(boinctasks PRIVATE -static -static-libgcc -static-libstdc++)
else()
    target_link_libraries(boinctasks boinclib ${wxWidgets_LIBRARIES} sqlite3)
endif()
