#include "bt_freedc.h"
#include "bt_config.h"
#include <wx/filename.h>
#include <wx/textfile.h>
#include <map>

static const wxString kStatsBase = "https://stats.free-dc.org";

// Master URLs are compared without scheme, without a trailing slash and in
// lower case, so http/https and a missing slash all land on the same key.
// Keying on host *and* path matters: one server often runs several projects.
static wxString NormaliseUrl(const wxString& url)
{
    wxString u = url;
    u.MakeLower();
    if (u.StartsWith("https://")) u = u.Mid(8);
    else if (u.StartsWith("http://")) u = u.Mid(7);
    while (u.EndsWith("/")) u.RemoveLast();
    return u;
}

// Project names reduced to [a-z0-9], so "Rosetta@home", "Rosetta@Home" and
// "rosetta home" all land on the same key. BOINC's project_name and Free-DC's
// description rarely agree on punctuation or case.
static wxString NormaliseName(const wxString& name)
{
    wxString out;
    for (wxUniChar c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
        else if (c >= 'A' && c <= 'Z') out += wxUniChar(c.GetValue() + 32);
    }
    return out;
}

// Free-DC's whole project table, keyed on the normalised project name. This is
// the general case; the URL map below is the exception mechanism for the few
// projects whose BOINC name does not match Free-DC's.
static const struct { const char* name; const char* code; } kByName[] = {
#include "bt_freedc_projects.inc"
};

// Where BOINC's project_name and Free-DC's description disagree by more than
// punctuation, so neither the URL table nor the name table catches them.
// All four confirmed against Free-DC rather than inferred.
static const struct { const char* name; const char* code; } kNameAlias[] = {
    { "lhchome",              "lhc"   },  // BOINC "LHC@home" / Free-DC "LHC@Home 1.0"
    { "climatepredictionnet", "bcpdn" },  // BOINC "climateprediction.net"
    { "ithenacomputational",  "ithc"  },  // BOINC "iThena.Computational"
    { "odlk1",                "lts"   },  // BOINC "ODLK1" / Free-DC "Latin Squares".
                                          // Distinct from ODLK (odl) and ODLK25.
};

// Free-DC's project table keyed on the project website. Exact where BOINC's
// master URL and the website agree, which is most of the time; the name table
// covers the rest. The mapping file overrides both.
static const struct { const char* url; const char* code; } kByUrl[] = {
#include "bt_freedc_urls.inc"
};

static void LoadInto(std::map<wxString, wxString>& m)
{
    m.clear();
    for (const auto& s : kByUrl) m[NormaliseUrl(s.url)] = s.code;

    const wxString path = BtFreeDcMapPath();
    if (!wxFileName::FileExists(path)) return;

    wxTextFile f(path);
    if (!f.Open()) return;
    for (size_t i = 0; i < f.GetLineCount(); i++) {
        wxString line = f[i];
        line = line.Trim().Trim(false);
        if (line.IsEmpty() || line.StartsWith("#")) continue;
        if (line.Find('=') == wxNOT_FOUND) continue;
        wxString key = line.BeforeFirst('=').Trim().Trim(false);
        wxString val = line.AfterFirst('=').Trim().Trim(false);
        // the file wins over the seed: that is how a wrong entry gets corrected
        if (!key.IsEmpty() && !val.IsEmpty()) m[NormaliseUrl(key)] = val;
    }
    f.Close();
}

static std::map<wxString, wxString>& Map()
{
    static std::map<wxString, wxString> m;
    static bool loaded = false;
    if (!loaded) { loaded = true; LoadInto(m); }
    return m;
}

void BtFreeDcReload()
{
    LoadInto(Map());
}

wxString BtFreeDcMapPath()
{
    wxFileName fn(BtConfigPath());
    fn.SetFullName(wxString(BTPP_SHORT) + "-freedc.conf");
    return fn.GetFullPath();
}

wxString BtFreeDcHostCpidUrl(const wxString& hostCpid)
{
    wxString cpid = hostCpid;
    cpid.Trim().Trim(false);
    if (cpid.IsEmpty()) return wxEmptyString;
    return kStatsBase + "/stats.php?page=hostbycpid&cpid=" + cpid;
}

wxString BtFreeDcShortCode(const wxString& masterUrl, const wxString& projectName)
{
    // URL first: it is exact, and it is what the mapping file overrides with.
    const auto& m = Map();
    auto it = m.find(NormaliseUrl(masterUrl));
    if (it != m.end()) return it->second;

    // Then the name table, which covers Free-DC's full project list.
    const wxString key = NormaliseName(projectName);
    if (key.IsEmpty()) return wxEmptyString;
    for (const auto& e : kByName)
        if (key == e.name) return e.code;
    for (const auto& e : kNameAlias)
        if (key == e.name) return e.code;

    return wxEmptyString;
}

wxString BtFreeDcHostIdUrl(const wxString& masterUrl, const wxString& projectName,
                           int hostId)
{
    if (hostId <= 0) return wxEmptyString;
    const wxString code = BtFreeDcShortCode(masterUrl, projectName);
    if (code.IsEmpty()) return wxEmptyString;
    return kStatsBase + "/host/" + code + wxString::Format("/%d", hostId);
}
