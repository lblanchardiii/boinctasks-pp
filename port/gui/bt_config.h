// =============================================================================
// bt_config.h - where settings live, and the computer list
//
// Everything persistent lives under ~/.config with the "boinctasks-pp" prefix.
// The paths are built here rather than spelled out at each call site: they were
// duplicated a dozen times before, which is how a rename goes wrong.
// =============================================================================
#pragma once
#include "bt_types.h"
#include <wx/string.h>
#include <vector>

// "BoincTasks++" for anything a person reads; the short form for files, package
// names and anything that has to survive a URL or a shell.
#define BTPP_NAME       "BoincTasks++"
#define BTPP_SHORT      "boinctasks-pp"

// Linux: ~/.config/boinctasks-pp.conf   Windows: %APPDATA%\\BoincTasks++\\...
wxString BtConfigPath();
wxString BtHistoryPath();

// Where the local BOINC client keeps gui_rpc_auth.cfg and its job logs.
wxString BtBoincDataDir();

// Carry settings and history over from the boinctasks-linux naming, once.
void BtMigrateLegacyConfig();

std::vector<BtComputer> BtLoadComputers();
void BtSaveComputers(const std::vector<BtComputer>& computers);
