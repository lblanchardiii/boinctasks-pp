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

wxString BtConfigPath();     // ~/.config/boinctasks-pp.conf
wxString BtHistoryPath();    // ~/.config/boinctasks-pp-history.db

// Carry settings and history over from the boinctasks-linux naming, once.
void BtMigrateLegacyConfig();

std::vector<BtComputer> BtLoadComputers();
void BtSaveComputers(const std::vector<BtComputer>& computers);
