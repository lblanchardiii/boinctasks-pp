cmake_minimum_required(VERSION 3.16)
project(boinctasks-pp C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

set(BT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# MSVC secure-CRT names appear in vendored headers; make the shims universal
add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/compat/msvc_crt_shim.h)

# On Linux the compat dir goes first so #include <winsock2.h> in unmodified
# sources resolves to our shim. On Windows the real headers must win, so the
# compat dir goes last - the shims disable themselves there anyway.
if(WIN32)
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

# ---- core (non-UI) library --------------------------------------------------
add_library(btcore STATIC
    ${BT_ROOT}/BoincTasks/RpcClient.cpp
    ${BT_ROOT}/BoincTasks/Base64.cpp
    compat/bt_port_shim.cpp
)
set_source_files_properties(${BT_ROOT}/BoincLibrary/Boinc/md5.c PROPERTIES LANGUAGE C)
target_link_libraries(btcore ${wxWidgets_LIBRARIES})

# ---- BOINC's own cross-platform gui-rpc library ------------------------------
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
    ${BT_ROOT}/BoincLibrary/Boinc/unix_util.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/prefs.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/notice.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/hostinfo.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/cc_config.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/coproc.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/filesys.cpp
    ${BT_ROOT}/BoincLibrary/Boinc/url.cpp
)
target_include_directories(boinclib PUBLIC ${BT_ROOT}/BoincLibrary/Boinc)

# ---- headless smoke test ----------------------------------------------------
add_executable(bt_rpc_test tests/bt_rpc_test.cpp)
target_link_libraries(bt_rpc_test btcore boinclib ${wxWidgets_LIBRARIES})

add_executable(bt_boincrpc_test tests/bt_boincrpc_test.cpp)
target_link_libraries(bt_boincrpc_test boinclib btcore ${wxWidgets_LIBRARIES})

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
    gui/bt_taskinfo.cpp
)
if(WIN32)
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
