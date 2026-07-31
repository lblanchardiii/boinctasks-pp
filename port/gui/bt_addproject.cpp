#include "bt_addproject.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/checklst.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>
#include <wx/statline.h>
#include <wx/splitter.h>
#include <map>
#include <algorithm>

// Each leaf remembers which entry of m_known it stands for.
namespace {
class ProjItem : public wxTreeItemData
{
public:
    explicit ProjItem(int idx) : index(idx) {}
    int index;
};
}

BtAddProjectDlg::BtAddProjectDlg(wxWindow* parent,
                                 const std::vector<wxString>& computers,
                                 const std::vector<BtProjectChoice>& knownProjects)
    : wxDialog(parent, wxID_ANY, "Add project",
               wxDefaultPosition, wxSize(760, 640),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_known(knownProjects)
{
    auto* top = new wxBoxSizer(wxVERTICAL);

    top->Add(new wxStaticText(this, wxID_ANY, "Attach these computers:"),
             0, wxLEFT | wxRIGHT | wxTOP, 12);
    wxArrayString names;
    for (const auto& c : computers) names.Add(c);
    m_computers = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition,
                                     wxSize(-1, 110), names);
    for (unsigned i = 0; i < m_computers->GetCount(); i++) m_computers->Check(i, true);
    top->Add(m_computers, 0, wxEXPAND | wxALL, 12);

    // ---- project picker --------------------------------------------------
    // Two hundred projects in a dropdown is unusable, which is why Classic
    // groups them. The categories come from the master list itself.
    auto* filterRow = new wxBoxSizer(wxHORIZONTAL);
    filterRow->Add(new wxStaticText(this, wxID_ANY, "Find:"),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_filter = new wxTextCtrl(this, wxID_ANY);
    filterRow->Add(m_filter, 1);
    top->Add(filterRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    auto* split = new wxBoxSizer(wxHORIZONTAL);
    m_tree = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(320, 220),
                            wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE |
                            wxTR_LINES_AT_ROOT);
    split->Add(m_tree, 1, wxEXPAND | wxRIGHT, 8);

    m_desc = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(320, 220),
                            wxTE_MULTILINE | wxTE_READONLY | wxTE_BESTWRAP |
                            wxBORDER_THEME);
    split->Add(m_desc, 1, wxEXPAND);
    top->Add(split, 1, wxEXPAND | wxALL, 12);

    // ---- credentials -----------------------------------------------------
    auto* grid = new wxFlexGridSizer(2, 8, 8);
    grid->AddGrowableCol(1, 1);
    auto row = [&](const wxString& label, wxWindow* ctrl) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_url      = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(340, -1));
    m_email    = new wxTextCtrl(this, wxID_ANY);
    m_password = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
                                wxDefaultSize, wxTE_PASSWORD);
    m_key      = new wxTextCtrl(this, wxID_ANY);

    row("URL",          m_url);
    row("Email",        m_email);
    row("Password",     m_password);
    row("Account key",  m_key);

    top->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    top->Add(new wxStaticText(this, wxID_ANY,
        "Pick a project above, or type a URL directly.\n"
        "Give either your project email and password, or an account key.\n"
        "An account key attaches without contacting the project for a lookup."),
        0, wxALL, 12);
    top->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);

    m_filter->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        BuildTree(m_filter->GetValue());
    });
    m_tree->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent&) { OnTreeSelect(); });

    BuildTree("");
    if (m_known.empty())
        m_desc->SetValue(
            "The list of known projects is empty.\n\n"
            "It comes from a BOINC client - the local one if there is one, "
            "otherwise the first computer on your list that answers. A client "
            "that has never fetched it will not have one.\n\n"
            "You can still attach by typing the project's URL below.");

    SetSizer(top);
}

void BtAddProjectDlg::BuildTree(const wxString& filter)
{
    m_tree->Freeze();
    m_tree->DeleteAllItems();
    wxTreeItemId root = m_tree->AddRoot("projects");

    const wxString needle = filter.Lower();

    // group by general area, keeping each area's projects in name order
    std::map<wxString, std::vector<int>> byArea;
    for (int i = 0; i < (int)m_known.size(); i++) {
        const auto& p = m_known[i];
        if (!needle.IsEmpty()) {
            wxString hay = (p.name + " " + p.area + " " + p.specificArea + " " +
                            p.description + " " + p.url).Lower();
            if (!hay.Contains(needle)) continue;
        }
        byArea[p.area.IsEmpty() ? wxString("Other") : p.area].push_back(i);
    }

    for (auto& [area, items] : byArea) {
        wxTreeItemId node = m_tree->AppendItem(root, area);
        std::sort(items.begin(), items.end(), [this](int a, int b) {
            return m_known[a].name.CmpNoCase(m_known[b].name) < 0;
        });
        for (int idx : items)
            m_tree->AppendItem(node, m_known[idx].name, -1, -1, new ProjItem(idx));
    }

    // A filtered tree is only useful open; the full one is easier to scan shut.
    if (!needle.IsEmpty()) m_tree->ExpandAll();
    m_tree->Thaw();
}

void BtAddProjectDlg::OnTreeSelect()
{
    wxTreeItemId id = m_tree->GetSelection();
    if (!id.IsOk()) return;
    auto* data = dynamic_cast<ProjItem*>(m_tree->GetItemData(id));
    if (!data) return;                       // an area heading, not a project

    m_selected = data->index;
    const auto& p = m_known[m_selected];
    m_url->SetValue(p.url);

    wxString info = p.name + "\n\n";
    if (!p.home.IsEmpty())         info += "Run by: " + p.home + "\n";
    if (!p.specificArea.IsEmpty()) info += "Area: " + p.specificArea + "\n";
    if (!p.platforms.IsEmpty())    info += "Runs on: " + p.platforms + "\n";
    if (!p.description.IsEmpty())  info += "\n" + p.description;
    m_desc->SetValue(info);
}

std::vector<BtAttachRequest> BtAddProjectDlg::Result() const
{
    std::vector<BtAttachRequest> out;
    wxString url = m_url->GetValue().Trim().Trim(false);
    if (url.IsEmpty()) return out;
    if (!url.EndsWith("/")) url += "/";        // BOINC expects a trailing slash

    // Use the picked project's name, but only while the URL still matches it -
    // typing over the URL by hand means the name no longer describes it.
    wxString name = url;
    if (m_selected >= 0 && m_selected < (int)m_known.size()) {
        wxString picked = m_known[m_selected].url;
        if (!picked.EndsWith("/")) picked += "/";
        if (picked.IsSameAs(url, false)) name = m_known[m_selected].name;
    }

    for (unsigned i = 0; i < m_computers->GetCount(); i++) {
        if (!m_computers->IsChecked(i)) continue;
        BtAttachRequest r;
        r.computer    = m_computers->GetString(i);
        r.url         = url;
        r.projectName = name;
        r.email       = m_email->GetValue().Trim().Trim(false);
        r.password    = m_password->GetValue();
        r.accountKey  = m_key->GetValue().Trim().Trim(false);
        out.push_back(std::move(r));
    }
    return out;
}
