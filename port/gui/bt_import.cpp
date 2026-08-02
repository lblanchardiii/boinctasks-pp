#include "bt_import.h"
#include <wx/xml/xml.h>
#include <wx/filename.h>
#include <algorithm>

/*
 * The format, taken from the upstream reader (BoincTasks/BoincTasks.cpp):
 *
 *   <computers><computer>
 *     <id_name>   name in the tree
 *     <id_group>  group, may be empty
 *     <ip>        host or address
 *     <mac>       Wake-on-LAN; no equivalent here
 *     <checked>   0 or 1  -> our "enabled"
 *     <port>      port, or -1 meaning "use the default"
 *     <password>  see below
 *     <encryption>yes or no
 *   </computer>...
 *
 * Passwords: Classic runs TranslateFromXml over the value unconditionally but
 * only *assigns* the result when encryption is on, so with encryption=no the
 * plaintext is used as-is and the translation is discarded. Reading that code
 * quickly suggests every password is %number% encoded; it is not.
 *
 * With encryption=yes the password cannot come across at all - Decrypt uses the
 * Windows CryptoAPI with a key held on the machine that wrote the file. Those
 * entries import with an empty password and are counted so the user can be told
 * which ones need re-entering.
 */

static wxString childText(wxXmlNode* parent, const wxString& name)
{
    for (wxXmlNode* c = parent->GetChildren(); c; c = c->GetNext())
        if (c->GetName().IsSameAs(name, false)) return c->GetNodeContent().Trim().Trim(false);
    return wxString();
}

BtImportResult BtParseClassicComputers(const wxString& path)
{
    BtImportResult res;
    if (!wxFileName::FileExists(path)) { res.error = "File not found: " + path; return res; }

    wxXmlDocument doc;
    if (!doc.Load(path)) { res.error = "Not readable as XML: " + path; return res; }

    wxXmlNode* root = doc.GetRoot();
    if (!root || !root->GetName().IsSameAs("computers", false)) {
        res.error = "This does not look like a BoincTasks computers.xml "
                    "(expected a <computers> root).";
        return res;
    }

    for (wxXmlNode* n = root->GetChildren(); n; n = n->GetNext()) {
        if (!n->GetName().IsSameAs("computer", false)) continue;

        wxString name = childText(n, "id_name");
        if (name.IsEmpty()) continue;                 // Classic skips these too

        BtImportEntry e;
        e.computer.name  = name;
        e.computer.group = childText(n, "id_group");
        e.computer.host  = childText(n, "ip").Lower();

        // Classic folds anything resembling localhost, and drops the MAC when
        // it does; match that so an imported local entry behaves like a
        // hand-made one.
        if (e.computer.host.Contains("127.0.0.1") || e.computer.host == "localhost") {
            e.computer.host = "localhost";
        } else {
            e.mac = childText(n, "mac");
        }

        long port = 0;
        if (!childText(n, "port").ToLong(&port) || port <= 0) port = 31416;   // -1 = default
        e.computer.port = port;

        e.encrypted = childText(n, "encryption").Lower() == "yes";
        e.computer.password = e.encrypted ? wxString() : childText(n, "password");

        // "checked" is Classic's enabled flag; leaving a parked host switched on
        // would start polling machines the user had deliberately stopped
        long checked = 0;
        childText(n, "checked").ToLong(&checked);
        e.computer.enabled = (checked != 0);

        res.entries.push_back(std::move(e));
    }
    if (res.entries.empty() && res.error.IsEmpty())
        res.error = "No computers found in that file.";
    return res;
}

void BtMergeComputers(const std::vector<BtImportEntry>& entries,
                      std::vector<BtComputer>& into,
                      int& added, int& skipped, int& needPassword)
{
    added = skipped = needPassword = 0;
    for (const auto& e : entries) {
        auto clash = std::find_if(into.begin(), into.end(),
            [&](const BtComputer& c) { return c.name.IsSameAs(e.computer.name, false); });
        if (clash != into.end()) { skipped++; continue; }   // never overwrite
        into.push_back(e.computer);
        added++;
        if (e.encrypted) needPassword++;
    }
}
