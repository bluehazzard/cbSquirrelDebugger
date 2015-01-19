/*
 * This file is part of the Code::Blocks IDE and licensed under the GNU General Public License, version 3
 * http://www.gnu.org/licenses/gpl-3.0.html
 *
 */

#include <sdk.h>
#include "debuggeroptionsdlg.h"
#ifndef CB_PRECOMP
    #include <wx/checkbox.h>
    #include <wx/choice.h>
//    #include <wx/filedlg.h>
    #include <wx/intl.h>
    #include <wx/radiobox.h>
//    #include <wx/spinctrl.h>
    #include <wx/textctrl.h>
    #include <wx/xrc/xmlres.h>

    #include <configmanager.h>
    #include <macrosmanager.h>
#endif


class DebuggerConfigurationPanel : public wxPanel
{
    public:
        DebuggerConfigurationPanel(SqDebuggerConfiguration *dbgcfg): m_dbgcfg(dbgcfg) {}
    protected:
        void OnChangeEngine(wxCommandEvent &event)
        {
            //int state = XRCCTRL(*this, "DebugEngine",       wxRadioBox)->GetSelection();
            //XRCCTRL(*this, "InitCommands",      wxTextCtrl)->ChangeValue(m_dbgcfg->GetInitCommands(state));
            //XRCCTRL(*this, "CommandLine",       wxTextCtrl)->ChangeValue(m_dbgcfg->GetCommandLine(state));
        }
    private:
        SqDebuggerConfiguration *m_dbgcfg;
        DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(DebuggerConfigurationPanel, wxPanel)
    EVT_RADIOBOX(XRCID("DebugEngine"), DebuggerConfigurationPanel::OnChangeEngine)
END_EVENT_TABLE()

SqDebuggerConfiguration::SqDebuggerConfiguration(const ConfigManagerWrapper &config) : cbDebuggerConfiguration(config)
{
}

cbDebuggerConfiguration* SqDebuggerConfiguration::Clone() const
{
    return new SqDebuggerConfiguration(*this);
}

wxPanel* SqDebuggerConfiguration::MakePanel(wxWindow *parent)
{
    DebuggerConfigurationPanel *panel = new DebuggerConfigurationPanel(this);
    if (!wxXmlResource::Get()->LoadPanel(panel, parent, wxT("dlgSquirrelDebuggerOptions")))
        return panel;

    XRCCTRL(*panel, "DebugEngine",     wxRadioBox)->SetSelection(GetEngine());
    XRCCTRL(*panel, "ID_target_port",  wxTextCtrl)->ChangeValue(wxString::Format(_("%d"),GetTargetPort()));
    XRCCTRL(*panel, "ID_target_ip",    wxTextCtrl)->ChangeValue(GetTargetIP());
    return panel;
}

bool SqDebuggerConfiguration::SaveChanges(wxPanel *panel)
{
    int sel = XRCCTRL(*panel, "DebugEngine",  wxRadioBox)->GetSelection();
    m_config.Write(wxT("active_engine"),  sel);
    m_config.Write(wxT("target_port"),    XRCCTRL(*panel, "ID_target_port", wxTextCtrl)->GetValue());
    m_config.Write(wxT("target_ip"),      XRCCTRL(*panel, "ID_target_ip", wxTextCtrl)->GetValue());

    return true;
}

int SqDebuggerConfiguration::GetEngine()
{
    return m_config.ReadInt(wxT("active_engine"), 0);
}

int SqDebuggerConfiguration::GetTargetPort()
{
    int port = m_config.ReadInt(wxT("target_port"),5351);
    if(port <= 0)   // Ports can't be negative, return the default one
        port = 5351;
    return port;
}

wxString SqDebuggerConfiguration::GetTargetIP()
{
    wxString ip_address_str = m_config.Read(wxT("target_ip"),wxT("127.0.0.1"));
    wxIPV4address tmp_addr;
    if(tmp_addr.Hostname(ip_address_str) == false)
    {
        // The ip address could not be parsed...
        // return a default one
        return wxT("127.0.0.1");
    }
    return ip_address_str;
}

