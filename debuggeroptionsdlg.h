/*
 * This file is part of the Code::Blocks IDE and licensed under the GNU General Public License, version 3
 * http://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef DEBUGGEROPTIONSDLG_H
#define DEBUGGEROPTIONSDLG_H

#include <debuggermanager.h>

class ConfigManagerWrapper;

class SqDebuggerConfiguration : public cbDebuggerConfiguration
{
    public:
        explicit SqDebuggerConfiguration(const ConfigManagerWrapper &config);

        virtual cbDebuggerConfiguration* Clone() const;
        virtual wxPanel* MakePanel(wxWindow *parent);
        virtual bool SaveChanges(wxPanel *panel);
    public:
        int GetEngine();
        wxString GetTargetIP();
        int GetTargetPort();

};

#endif // DEBUGGEROPTIONSDLG_H
