#include "bt_config.h"
#include <wx/fileconf.h>
#include <wx/filefn.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <memory>

static wxFileConfig* OpenConfig()
{
    return new wxFileConfig(BTPP_SHORT, "eFMer", BtConfigPath());
}

wxString BtConfigPath()
{
    return wxGetHomeDir() + "/.config/" + BTPP_SHORT + ".conf";
}

wxString BtHistoryPath()
{
    return wxGetHomeDir() + "/.config/" + BTPP_SHORT + "-history.db";
}

void BtMigrateLegacyConfig()
{
    // Earlier builds called themselves boinctasks-linux. Bring a user's
    // computers, rules and history across rather than starting them empty.
    struct { wxString from, to; } moves[] = {
        { wxGetHomeDir() + "/.config/boinctasks-linux.conf", BtConfigPath() },
        { wxGetHomeDir() + "/.config/boinctasks-history.db", BtHistoryPath() },
    };
    for (const auto& m : moves) {
        if (wxFileExists(m.to) || !wxFileExists(m.from)) continue;
        wxCopyFile(m.from, m.to, false);
    }
}

std::vector<BtComputer> BtLoadComputers()
{
    std::vector<BtComputer> list;
    std::unique_ptr<wxFileConfig> cfg(OpenConfig());
    long n = cfg->ReadLong("/Computers/count", 0);
    for (long i = 0; i < n; i++) {
        wxString g = wxString::Format("/Computers/%ld/", i);
        BtComputer c;
        c.name     = cfg->Read(g + "name", "");
        c.group    = cfg->Read(g + "group", "");
        c.host     = cfg->Read(g + "host", "");
        c.port     = cfg->ReadLong(g + "port", 31416);
        c.password = cfg->Read(g + "password", "");
        c.enabled  = cfg->ReadBool(g + "enabled", true);
        if (!c.host.empty())
            list.push_back(c);
    }
    if (list.empty()) {
        BtComputer localhost;
        localhost.name = "localhost";
        localhost.host = "127.0.0.1";
        list.push_back(localhost);
    }
    return list;
}

void BtSaveComputers(const std::vector<BtComputer>& computers)
{
    std::unique_ptr<wxFileConfig> cfg(OpenConfig());
    cfg->DeleteGroup("/Computers");
    cfg->Write("/Computers/count", (long)computers.size());
    for (size_t i = 0; i < computers.size(); i++) {
        wxString g = wxString::Format("/Computers/%zu/", i);
        cfg->Write(g + "name",     computers[i].name);
        cfg->Write(g + "group",    computers[i].group);
        cfg->Write(g + "host",     computers[i].host);
        cfg->Write(g + "port",     computers[i].port);
        cfg->Write(g + "password", computers[i].password);
        cfg->Write(g + "enabled",  computers[i].enabled);
    }
    cfg->Flush();
}
