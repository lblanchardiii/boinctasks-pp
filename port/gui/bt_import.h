// =============================================================================
// bt_import.h - bring computers across from eFMer's BoincTasks
//
// Classic keeps its computer list in computers.xml. Somebody arriving with a
// real farm should not have to retype every host, port and password, so this
// reads that file and merges it into ours.
// =============================================================================
#pragma once
#include "bt_types.h"
#include <wx/string.h>
#include <vector>

struct BtImportEntry
{
    BtComputer computer;
    bool encrypted = false;   // password could not be brought across
    wxString mac;             // Classic uses it for Wake-on-LAN; we have none
};

struct BtImportResult
{
    std::vector<BtImportEntry> entries;
    wxString error;           // non-empty when the file could not be read
};

// Parse a Classic computers.xml. Does not touch the configured list.
BtImportResult BtParseClassicComputers(const wxString& path);

// Merge parsed entries into `into`, never removing anything already there.
// Returns counts via the out params; a name already present is left alone.
void BtMergeComputers(const std::vector<BtImportEntry>& entries,
                      std::vector<BtComputer>& into,
                      int& added, int& skipped, int& needPassword);
