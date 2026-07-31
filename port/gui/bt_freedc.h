// =============================================================================
// bt_freedc.h - links out to Free-DC's stats pages
//
// Two different pages, both reachable from a Projects row:
//
//   host by CPID     one page per computer, keyed on the BOINC cross-project
//                    host ID. That value arrives in CC_STATE::host_info, so
//                    nothing extra is polled for it.
//
//   host by project  one page per (computer, project), keyed on the ID that
//                    project assigned this host - PROJECT::hostid - plus
//                    Free-DC's own short code for the project ("mine" for
//                    Minecraft@Home). BOINC knows nothing about the short code,
//                    so it comes from a mapping file.
//
// The mapping file is plain text, one "master-url = shortcode" per line:
//
//     # comments allowed
//     rake.boincfast.ru/rakesearch = rakesearch
//
// It lives next to the settings (BtFreeDcMapPath()). A small built-in table
// seeds it; anything not in either is simply not offered, rather than guessed -
// a wrong short code produces a link to somebody else's project.
// =============================================================================
#pragma once
#include <wx/string.h>

// https://stats.free-dc.org/stats.php?page=hostbycpid&cpid=...
// Empty when the client did not report a CPID.
wxString BtFreeDcHostCpidUrl(const wxString& hostCpid);

// Free-DC's short code for a project, or empty when it is not known. The master
// URL is checked first (exact, and what the mapping file overrides), then the
// project name against Free-DC's own table.
wxString BtFreeDcShortCode(const wxString& masterUrl, const wxString& projectName);

// https://stats.free-dc.org/host/<short>/<hostId>
// Empty when the short code is unknown or the host has no ID on that project.
wxString BtFreeDcHostIdUrl(const wxString& masterUrl, const wxString& projectName,
                           int hostId);

// Where the mapping file lives, for the message shown when a code is missing.
wxString BtFreeDcMapPath();

// Re-read the mapping file (it is loaded once, on first use).
void BtFreeDcReload();
