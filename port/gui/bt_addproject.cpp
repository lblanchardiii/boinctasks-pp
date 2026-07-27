#include "bt_addproject.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/statline.h>

BtAddProjectDlg::BtAddProjectDlg(wxWindow* parent,
                                 const std::vector<wxString>& computers,
                                 const std::vector<BtProjectChoice>& knownProjects)
    : wxDialog(parent, wxID_ANY, "Add project",
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_known(knownProjects)
{
    auto* top = new wxBoxSizer(wxVERTICAL);

    top->Add(new wxStaticText(this, wxID_ANY, "Attach these computers:"),
             0, wxLEFT | wxRIGHT | wxTOP, 12);
    wxArrayString names;
    for (const auto& c : computers) names.Add(c);
    m_computers = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition,
                                     wxSize(-1, 120), names);
    for (unsigned i = 0; i < m_computers->GetCount(); i++) m_computers->Check(i, true);
    top->Add(m_computers, 1, wxEXPAND | wxALL, 12);

    auto* grid = new wxFlexGridSizer(2, 8, 8);
    grid->AddGrowableCol(1, 1);
    auto row = [&](const wxString& label, wxWindow* ctrl) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    m_project = new wxChoice(this, wxID_ANY);
    m_project->Append("(enter a URL below)");
    for (const auto& p : m_known) m_project->Append(p.name);
    m_project->SetSelection(0);

    m_url      = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(340, -1));
    m_email    = new wxTextCtrl(this, wxID_ANY);
    m_password = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
                                wxDefaultSize, wxTE_PASSWORD);
    m_key      = new wxTextCtrl(this, wxID_ANY);

    row("Project",      m_project);
    row("URL",          m_url);
    row("Email",        m_email);
    row("Password",     m_password);
    row("Account key",  m_key);

    top->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    top->Add(new wxStaticText(this, wxID_ANY,
        "Give either your project email and password, or an account key.\n"
        "An account key attaches without contacting the project for a lookup."),
        0, wxALL, 12);
    top->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
    top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);

    m_project->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SyncUrlFromChoice(); });

    SetSizerAndFit(top);
}

void BtAddProjectDlg::SyncUrlFromChoice()
{
    int sel = m_project->GetSelection();
    if (sel > 0 && sel - 1 < (int)m_known.size())
        m_url->SetValue(m_known[sel - 1].url);
}

std::vector<BtAttachRequest> BtAddProjectDlg::Result() const
{
    std::vector<BtAttachRequest> out;
    wxString url = m_url->GetValue().Trim().Trim(false);
    if (url.IsEmpty()) return out;
    if (!url.EndsWith("/")) url += "/";        // BOINC expects a trailing slash

    wxString name = url;
    int sel = m_project->GetSelection();
    if (sel > 0 && sel - 1 < (int)m_known.size()) name = m_known[sel - 1].name;

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
