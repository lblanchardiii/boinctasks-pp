#include "bt_taskinfo.h"
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/datetime.h>

static wxString hms(double secs)
{
    if (secs <= 0) return "-";
    long s = (long)secs, d = s / 86400;
    s %= 86400;
    wxString t = wxString::Format("%02ld:%02ld:%02ld", s/3600, (s%3600)/60, s%60);
    return d > 0 ? wxString::Format("%ldd %s", d, t) : t;
}

wxString BtFormatTaskInfo(const BtTaskRow& t)
{
    wxString s;
    auto line = [&](const wxString& k, const wxString& v) {
        s += wxString::Format("%-22s %s\n", k + ":", v);
    };
    line("Name",         t.name);
    line("Computer",     t.computer);
    line("Project",      t.project);
    line("Application",  t.application);
    line("Status",       t.status);
    line("Uses GPU",     t.isGpu ? "yes" : "no");
    line("CPU usage",    t.useCpus > 0 ? wxString::Format("%.3g CPUs", t.useCpus) : "-");
    line("CPU %",        t.cpuPct > 0 ? wxString::Format("%.2f", t.cpuPct) : "-");
    line("Progress",     wxString::Format("%.3f %%", t.progress));
    line("Elapsed time", hms(t.elapsed));
    line("CPU time",     hms(t.cpuTime));
    line("Time left",    hms(t.timeLeft));
    line("Deadline",     t.deadline > 0
                            ? wxDateTime((time_t)t.deadline).Format("%c")
                            : wxString("-"));
    line("Project URL",  t.projectUrl);
    return s;
}

BtTaskInfoDlg::BtTaskInfoDlg(wxWindow* parent, const BtTaskRow& task)
    : wxDialog(parent, wxID_ANY, "Task: " + task.name,
               wxDefaultPosition, wxSize(640, 460),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    m_text = new wxTextCtrl(this, wxID_ANY, BtFormatTaskInfo(task),
                            wxDefaultPosition, wxDefaultSize,
                            wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    m_text->SetFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE)));

    auto* top = new wxBoxSizer(wxVERTICAL);
    top->Add(m_text, 1, wxEXPAND | wxALL, 10);
    top->Add(CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, 10);
    SetSizer(top);
}

void BtTaskInfoDlg::SetDetail(const wxString& text)
{
    m_text->SetValue(text);
}
