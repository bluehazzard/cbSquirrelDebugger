#include "SquirrelDebugger.h"
#include <configurationpanel.h>
#include <wx/regex.h>
#include <cbdebugger_interfaces.h>
#include <cbstyledtextctrl.h>
#include <wx/xml/xml.h>
#include <wx/sstream.h>
#include <wx/progdlg.h>
#include "utils.h"
//#include <watchesdlg.h>

// Register the plugin with Code::Blocks.
// We are using an anonymous namespace so we don't litter the global one.
namespace
{
    PluginRegistrant<SquirrelDebugger> reg(_T("SquirrelDebugger"));
}


using namespace SquirrelDebugger_util;


int ID_LangMenu_Settings=wxNewId();
int ID_LangMenu_Run=wxNewId();
int ID_LangMenu_ShowWatch=wxNewId();
int ID_LangMenu_UpdateWatch=wxNewId();

int ID_PipedProcess=wxNewId();
int ID_TimerPollDebugger=wxNewId();

//assign menu IDs to correspond with toolbar buttons
int ID_LangMenu_RunPiped = wxNewId();//XRCID("idPyDebuggerMenuDebug");

int ID_Socket = wxNewId();

// events handling
BEGIN_EVENT_TABLE(SquirrelDebugger, cbDebuggerPlugin)
    EVT_SOCKET(ID_Socket,SquirrelDebugger::OnSocketEvt)
END_EVENT_TABLE()

unsigned long SquirrelWatch::m_unic_id = 0x00001;

// constructor
SquirrelDebugger::SquirrelDebugger() :  cbDebuggerPlugin(_T("SquirrelDebugger"),_T("sq_debugger")),
                                        m_socket(wxSOCKET_NONE),
                                        m_curline(0),
                                        m_changeposition(false)
{
    // Make sure our resources are available.
    // In the generated boilerplate code we have no resources but when
    // we add some, it will be nice that this code is in place already ;)
    if(!Manager::LoadResource(_T("SquirrelDebugger.zip")))
    {
        NotifyMissingFile(_T("SquirrelDebugger.zip"));
    }

    m_RunTargetSelected=false;
    m_DebuggerActive = false;
    m_stopped = true;
    m_stackinfo.activeframe = 0;

}

void SquirrelDebugger::OnSocketEvt(wxSocketEvent &event)
{
    switch(event.GetSocketEvent())
    {
    case wxSOCKET_LOST:
        m_DebugLog->Append(_("*** Lost socket connection..."));
        StopDebugging();
        break;
    case wxSOCKET_CONNECTION:
        m_connected = true;
        m_DebuggerActive = true;
        m_DebugLog->Append(_("*** Connection established..."));
        break;
    case wxSOCKET_INPUT:
        {
            //Read and process input...
            // Start a timeout timer for 2 sec
            wxTimer timeout_timer;
            timeout_timer.Start(2000,true);
            //wxString msg;
            wxUint32 bytes_read = 0;
            do
            {
                #define Buffer_size 2048
                char tmp_rec[Buffer_size];
                memset(tmp_rec,0,Buffer_size);
                event.GetSocket()->Read(tmp_rec,Buffer_size);
                bytes_read = event.GetSocket()->LastCount();
                for(size_t i = 0; i < bytes_read;i++)
                {
                    m_rec_buffer.Append(tmp_rec[i]);
                    if(tmp_rec[i] == '\n' && !m_rec_buffer.IsEmpty())
                    {
                        // We have received a whole message
                        if(cbDebuggerCommonConfig::GetFlag(cbDebuggerCommonConfig::ShowDebuggersLog))
                        {
                            m_DebugLog->Append(wxT("*** Buffer received:"));
                            m_DebugLog->Append(m_rec_buffer);
                        }

                        ProcessResponse(m_rec_buffer);
                        m_rec_buffer.clear();
                        if(m_DispatchedCommands.empty())
                            DispatchCommand();
                    }
                }

            // If no bytes are in the buffer or we get a timeout stop the loop
            }while(!timeout_timer.IsRunning() /*&& bytes_read != 0*/);
            if(!timeout_timer.IsRunning())
                m_DebugLog->Append(wxT("*** Timeout..."));
            //ProcessResponse(msg);

        break;
        }

    case wxSOCKET_OUTPUT:
        //Write queued output...
        break;
    }
}


void SquirrelDebugger::StopDebugging()
{
    m_stopped = true;
    m_DebuggerActive = false;
    m_socket.Close();
    m_connected = false;
    ClearActiveMarkFromAllEditors();
    m_DispatchedCommands.clear();
    m_CommandQueue.clear();

    // Notify other plugins that the debugger has finished
    PluginManager *plm = Manager::Get()->GetPluginManager();
    CodeBlocksEvent evt(cbEVT_DEBUGGER_PAUSED);
    plm->NotifyPlugins(evt);

    // switch to old Layout
    SwitchToPreviousLayout();
}

int SquirrelDebugger::ProcessResponse(wxString &resp)
{
    wxStringInputStream* in_str = new wxStringInputStream(resp);
    wxXmlDocument doc = wxXmlDocument(*in_str);
    delete in_str;
    if(!doc.IsOk())
    {
        m_DebugLog->Append(_T("*** Error reading input stream:\n"));
        m_DebugLog->Append(resp);
        return -1;
    }

    m_changeposition = false;

    wxXmlNode *root_node = doc.GetRoot();
    if(root_node == nullptr)
    {
        m_DebugLog->Append(_("*** Error: Could not get root_node in:\n"));
        m_DebugLog->Append(resp);
        return -2;
    }

    sqCMDType recv_cmd = SQDBG_CMD_FLOW_CTRL;


    wxString root_name = root_node->GetName();
    if(root_name == wxT("resumed"))
    {
        m_stopped = false;
        m_DebugLog->Append(_("*** resumed\n"));
        ClearActiveMarkFromAllEditors();
        recv_cmd = SQDBG_CMD_RESUME;
        // The debugger resumes his operation
    }
    else if(root_name == wxT("addbreakpoint"))
    {
        // A Breakpoint was added
        wxString line = root_node->GetAttribute(wxT("line"),wxT("-1"));
        wxString src = root_node->GetAttribute(wxT("src"),wxT("invalid"));
        m_DebugLog->Append(_("*** Breakpoint added:")+src+wxT(":")+line);
        recv_cmd = SQDBG_CMD_ADD_BP;

    }
    else if(root_name == wxT("removebreakpoint"))
    {
        // Remove a breakpoint
        wxString line = root_node->GetAttribute(wxT("line"),wxT("-1"));
        wxString src = root_node->GetAttribute(wxT("src"),wxT("invalid"));
        m_DebugLog->Append(_("*** Breakpoint removed:")+src+wxT(":")+line);
        recv_cmd = SQDBG_CMD_REM_BP;
    }
    else if(root_name == wxT("break"))
    {
        // We have received something that needs user interaction, so we will bring c::b in front
        BringCBToFront();

        recv_cmd = SQDBG_CMD_FLOW_CTRL;
        m_stopped = true;
        // The debugger signaled a break
        wxString line = root_node->GetAttribute(wxT("line"),wxT("-1"));
        wxString src = root_node->GetAttribute(wxT("src"),wxT("invalid"));
        wxString type = root_node->GetAttribute(wxT("type"),wxT("invalid"));
        long tmp_line = -1;

        line.ToLong(&tmp_line,10);
        m_curline = tmp_line;
        m_curfile = src;
        m_changeposition=true;

        if(type == wxT("step"))
        {
            // we made a step
        }
        else if(type == wxT("breakpoint"))
        {
            // we reached a breakpoint
            m_changeposition=true;
            m_DebugLog->Append(_("*** Halt on breakpoint:")+src);


            BPList::iterator itr = FindBp(SqBreakpoint(src,tmp_line),m_tmp_bp);
            if(itr != m_tmp_bp.end())
            {
                // This is a temporary breakpoint from "Run to cursor"
                m_tmp_bp.erase(itr);
                wxString cmd=_T("rb")+wxString::Format(_T(":%x:"),tmp_line)+src+_T("\n");

                if(cbDebuggerCommonConfig::GetFlag(cbDebuggerCommonConfig::ShowDebuggersLog))
                    m_DebugLog->Append(wxT("*** Remove temporary breakpoint : ")+cmd);

                QueueCommand(cmd,SQDBG_CMD_REM_BP);
            }
        }
        else if(type == wxT("error"))
        {
            // we encountered an error
            wxString error_msg = root_node->GetAttribute(wxT("error"),wxT("No \"error\" attribute found"));
            m_DebugLog->Append(_("*** Unhandled throw found:")+error_msg);

            // Show the backtrace window if a not handled throw has been found
            Manager::Get()->GetDebuggerManager()->ShowBacktraceDialog();
            // Inform the user with a nice popup window
            InfoWindow::Display(_("Unhandled throw found:\n"),error_msg + _T("\n\nIn:") + src + _T("\n\n"));
        }
        else
        {
           m_DebugLog->Append(_("*** Parsing error: could not parse type: ")+type);
        }

        wxXmlNode *child = root_node->GetChildren();
        while(child)
        {
            if(child->GetName() == wxT("objs"))
            {
                //! We have found the object Table (watches)
                wxXmlNode *objects = child->GetChildren();
                m_sq_objects.clear();
                while(objects)
                {

                    //! parse all objects
                    wxString obj_type_enc = DecodeType(objects->GetAttribute(wxT("type"),wxT("unknown")));
                    wxString obj_ref_tmp = objects->GetAttribute(wxT("ref"),wxT("-1"));
                    long obj_ref = -1;
                    obj_ref_tmp.ToLong(&obj_ref);

                    if(cbDebuggerCommonConfig::GetFlag(cbDebuggerCommonConfig::ShowDebuggersLog))
                        m_DebugLog->Append(_("Obj found: ")+obj_type_enc + wxT(" ref: ")+obj_ref_tmp);

                    SquirrelObjects::Pointer obj =  SquirrelObjects::Pointer(new SquirrelObjects(obj_ref));

                    wxXmlNode *obj_child = objects->GetChildren();
                    while(obj_child)
                    {
                        if(obj_child->GetName() == wxT("e"))
                        {
                            wxString key_type = DecodeType(obj_child->GetAttribute(wxT("kt"),wxT("unknown")));
                            wxString key_value = obj_child->GetAttribute(wxT("kv"),wxT("unknown"));
                            wxString val_type = DecodeType(obj_child->GetAttribute(wxT("vt"),wxT("unknown")));
                            wxString val_value = obj_child->GetAttribute(wxT("v"),wxT("unknown"));

                            if(cbDebuggerCommonConfig::GetFlag(cbDebuggerCommonConfig::ShowDebuggersLog))
                                m_DebugLog->Append(_("Object children: ")+key_type + wxT(":")+key_value + wxT(" = ")+ val_type + wxT(":")+ val_value);

                            SquirrelObjectChilds::Pointer obj_child =  SquirrelObjectChilds::Pointer(new SquirrelObjectChilds(key_value,key_type,val_value,val_type));
                            obj->AddChild(obj_child);
                        }
                        else
                        {
                            m_DebugLog->Append(_("*** Unknown object children: ")+obj_child->GetName() +wxString::Format(wxT(" of object ref: %d"),obj_ref));
                        }
                        obj_child = obj_child->GetNext();
                    }

                    m_sq_objects.insert(obj);

                    objects = objects->GetNext();
                }
            }
            else if(child->GetName() == wxT("calls"))
            {
                //! We have found the call stack
                wxXmlNode *call = child->GetChildren();

                // Remove the old watches
                DebuggerManager &dbg_manager = *Manager::Get()->GetDebuggerManager();
                if(m_stackinfo.activeframe < m_stackinfo.frames.size() &&
                   m_stackinfo.frames[m_stackinfo.activeframe]->m_locals)
                    dbg_manager.GetWatchesDialog()->RemoveWatch(m_stackinfo.frames[m_stackinfo.activeframe]->m_locals);

                int call_stack_nr = 0;
                StackList old_stack_list = m_stackinfo.frames;
                m_stackinfo.frames.clear();

                while(call)
                {
                    //! parse all objects
                    wxString call_fnc = call->GetAttribute(wxT("fnc"),wxT("unknown"));
                    wxString call_src = call->GetAttribute(wxT("src"),wxT("unknown"));
                    wxString call_line = call->GetAttribute(wxT("line"),wxT("-1"));
                    long l_call_line = -1;
                    call_line.ToLong(&l_call_line,10);

                    m_stackinfo.activeframe = 0;

                    //SquirrelStackFrame::Pointer f = cb::shared_ptr<SquirrelStackFrame>(new SquirrelStackFrame);
                    SquirrelStackFrame::Pointer f = SquirrelStackFrame::Pointer(new SquirrelStackFrame);
                    f->SetFile(call_src,wxString::Format(wxT("%d"),l_call_line));
                    f->SetNumber(call_stack_nr);
                    call_stack_nr++;

                    f->SetSymbol(call_fnc);
                    f->SetAddress(0);

                    SquirrelStackFrame::Pointer old_frame = FindStackFrame(f,old_stack_list);
                    if(old_frame)
                        f->m_locals = old_frame->m_locals;

                    if(cbDebuggerCommonConfig::GetFlag(cbDebuggerCommonConfig::ShowDebuggersLog))
                        m_DebugLog->Append(_("Call found: ")+call_fnc + wxT(" file: ")+call_src + wxT(" line:")+wxString::Format(wxT("%d"),l_call_line));
                    // search for variables
                    wxXmlNode *variables = call->GetChildren();
                    std::set<wxString> known_symbols;
                    //f->m_locals->Reset();
                    while(variables)
                    {
                        if(variables->GetName() == wxT("l"))
                        {
                            // we have found a local variable
                            wxString var_name   = variables->GetAttribute(wxT("name"),wxT("unknown"));
                            wxString var_type   = DecodeType(variables->GetAttribute(wxT("type"),wxT("unknown")));
                            wxString var_value  = variables->GetAttribute(wxT("val"),wxT("unknown"));
                            known_symbols.insert(var_name);

                            if(cbDebuggerCommonConfig::GetFlag(cbDebuggerCommonConfig::ShowDebuggersLog))
                                m_DebugLog->Append(_("Local var found : ")+var_name + wxT(" type: ")+var_type + wxT(" value: ")+var_value);

                            if(var_type == wxT("table") ||
                               var_type == wxT("array") ||
                               var_type == wxT("class") ||
                               var_type == wxT("instance"))
                            {
                                // The value is only a reference to the object, so get the value from the object
                                long ref = -1;
                                var_value.ToLong(&ref);
                                var_value = PrintNiceValueFromSquirrelObject(m_sq_objects,ref);
                            }


                            cb::shared_ptr<cbWatch> old_watch = f->m_locals->FindChild(var_name);

                            if(!old_watch)
                            {
                                old_watch = cb::shared_ptr<cbWatch>(new SquirrelWatch(var_name));
                                old_watch->SetType(var_type);
                                old_watch->SetValue(var_value);
                                cbWatch::AddChild(f->m_locals,old_watch);
                            }
                            else
                            {
                                wxString oldtype, oldval;
                                old_watch->GetValue(oldval);
                                old_watch->GetType(oldtype);
                                old_watch->MarkAsChanged(oldval!=var_value || oldtype!=var_type);
                                old_watch->SetType(var_type);
                                old_watch->SetValue(var_value);
                            }

                        }
                        else if(variables->GetName() == wxT("w"))
                        {
                            // we have found a watch
                            wxString watch_id       = variables->GetAttribute(wxT("id"),wxT("unknown"));
                            wxString watch_exp      = variables->GetAttribute(wxT("exp"),wxT("unknown"));
                            wxString watch_status   = variables->GetAttribute(wxT("status"),wxT("unknown"));
                            wxString watch_type     = DecodeType(variables->GetAttribute(wxT("type"),wxT("unknown")));
                            wxString watch_value    = variables->GetAttribute(wxT("val"),wxT("unknown"));
                            if(cbDebuggerCommonConfig::GetFlag(cbDebuggerCommonConfig::ShowDebuggersLog))
                                m_DebugLog->Append(_("Watch found : ")+watch_exp + wxT(" type: ")+watch_type + wxT(" value: ")+watch_value);


                            if(watch_status == wxT("ok"))
                            {
                                known_symbols.insert(watch_exp);

                                if(watch_type == wxT("table") ||
                                   watch_type == wxT("array") ||
                                   watch_type == wxT("class") ||
                                   watch_type == wxT("instance"))
                                {
                                    // The value is only a reference to the object, so get the value from the object
                                    long ref = -1;
                                    watch_value.ToLong(&ref);
                                    watch_value = PrintNiceValueFromSquirrelObject(m_sq_objects,ref);
                                }
                                // The watch is valid
                                long id = -1;
                                watch_id.ToLong(&id);
                                SquirrelWatch::Pointer watch = FindWatch(id,m_watchlist);

                                if(!watch)
                                {
                                    watch = cb::shared_ptr<SquirrelWatch>(new SquirrelWatch(watch_exp));
                                    watch->SetType(watch_type);
                                    watch->SetValue(watch_value);
                                    m_watchlist.push_back(watch);
                                    DebuggerManager &dbg_manager = *Manager::Get()->GetDebuggerManager();
                                    dbg_manager.GetWatchesDialog()->AddWatch(watch);

                                    //Store it in the StackFrame watches
                                    int child_count = f->m_watches->GetChildCount();
                                    SquirrelWatch::Pointer old_child;
                                    for(int i = 0;i < child_count;i++)
                                    {
                                        wxString watch_sym, child_sym;
                                        watch->GetSymbol(watch_sym);
                                        f->m_watches->GetChild(i)->GetSymbol(child_sym);
                                        if(child_sym== watch_sym)
                                            old_child = std::tr1::dynamic_pointer_cast<SquirrelWatch>(f->m_watches->GetChild(i));
                                    }
                                    if(old_child)
                                    {
                                        old_child->SetType(watch_value);
                                        old_child->SetValue(watch_type);
                                    }
                                    else
                                    {
                                        cbWatch::AddChild(f->m_watches,watch);
                                    }
                                }
                                else
                                {
                                    wxString oldtype, oldval;
                                    watch->GetValue(oldval);
                                    watch->GetType(oldtype);
                                    watch->MarkAsChanged(oldval!=watch_value || oldtype!=watch_type);
                                    watch->SetType(watch_type);
                                    watch->SetValue(watch_value);
                                }
                            }
                        }

                        variables = variables->GetNext();
                    }
                    RemoveMissingChildren(m_locals_watch,known_symbols);
                    m_stackinfo.frames.push_back(f);
                    call = call->GetNext();
                }
            }
            child = child->GetNext();
        }

        // Update the Call stack
        DebuggerManager &dbg_manager = *Manager::Get()->GetDebuggerManager();
        dbg_manager.GetBacktraceDialog()->Reload();
        m_stackinfo.frames[m_stackinfo.activeframe]->m_locals->Expand(true);
        dbg_manager.GetWatchesDialog()->AddWatch(m_stackinfo.frames[m_stackinfo.activeframe]->m_locals);
        dbg_manager.GetWatchesDialog()->UpdateWatches();

    }
    else if(root_name == wxT("error"))
    {
        recv_cmd = SQDBG_CMD_FLOW_CTRL;
        // The debugger encountered an error
        wxString error_msg = root_node->GetAttribute(wxT("desc"),wxT("No \"desc\" attribute found"));
        m_DebugLog->Append(_("*** Error from debugger:\n")+error_msg);
        cbMessageBox(_("*** Error from debugger:\n")+error_msg,_("Squirrel Debugger error"),wxOK|wxICON_ERROR);
        m_stopped = true;
        return 0;
    }
    else if(root_name == wxT("file_loaded"))
    {
        wxString loaded_file = root_node->GetAttribute(wxT("src"),wxT("unknown"));
        if(loaded_file != wxT("unknown"));
        {
            Manager::Get()->GetEditorManager()->Open(loaded_file);
        }
        m_DebugLog->Append(_("*** New file loaded:\n")+loaded_file);
    }
    else
    {
        m_DebugLog->Append(_("*** Error parsing respond from debugger:\n")+root_name);
    }

    SquirrelCmdDispatchData cmd;
    if(m_DispatchedCommands.size() > 0)
    {
        cmd = m_DispatchedCommands.back();
        m_DispatchedCommands.pop_back();
    }

    // Should we check the rec_cmd?

    if(m_changeposition)
    {
        if(m_curline<1)
        {
            cbMessageBox(_T("Invalid line position reported by sqdbg"));
            return -3;
        } else
        {
            SyncEditor(m_curfile,m_curline,true);
        }
    }
}


bool SquirrelDebugger::SupportsFeature(cbDebuggerFeature::Flags f)
{
    switch(f)
    {
        case cbDebuggerFeature::Breakpoints:
        case cbDebuggerFeature::Callstack:
        case cbDebuggerFeature::Watches:
        //case cbDebuggerFeature::ValueTooltips:
//        case cbDebuggerFeature::Threads:
        case cbDebuggerFeature::RunToCursor:
//        case cbDebuggerFeature::SetNextStatement:
            return true;
        default:
            return false;
    }
    return true;
}

void SquirrelDebugger::SendCommand(const wxString& cmd, bool debugLog)
{
    wxString scmd = cmd;
    if(!m_DebuggerActive) //could be unsafe, but allows user to provide program input
        return;
    if(!scmd.EndsWith(_T("\n")))
        scmd+=_T("\n");
    QueueCommand(scmd,SQDBG_CMD_USR,true);
}



// sends a newline delimited string of cmdcount debugger commands
//bool SquirrelDebugger::DispatchCommands(wxString cmd, sqCMDType cmdtype, bool poll)
//{
//    //if(m_TimerPollDebugger.IsRunning())
//    //    return false;
//    if(cmd.Last() != '\n')
//        cmd.Append(wxT("\n"));
//
//    if(cmd.Len()>0)
//    {
//        char *cmdc=new char[cmd.Len()];
//        for(size_t i=0;i<cmd.Len();i++)
//        {
//            cmdc[i]=cmd[i];
//            if(cmdc[i]=='\n')
//            {
//                if(WaitForResponse(cmdtype))
//                {
//                    SquirrelCmdDispatchData dd;
//                    dd.cmdtext=cmd;
//                    dd.type=cmdtype;
//                    m_DispatchedCommands.push_back(dd);
//                    m_DebugCommandCount++;
//                }
//            }
//        }
//        if(m_DispatchedCommands.size() == 1)
//        {
//            m_socket.Write(cmdc,cmd.Len());
//        }
//
//        delete[] cmdc;
//    }
//
//    return true;
//}

bool SquirrelDebugger::DispatchCommand()
{
    if(m_CommandQueue.size() == 0 ||
       !m_connected || !m_DebuggerActive ||
       m_DispatchedCommands.size() != 0)  // We have to wait for response before we send a other command
        return false;

    SquirrelCmdDispatchData cmd = m_CommandQueue.back();

    if(cmd.cmdtext.Last() != '\n')
        cmd.cmdtext.Append(wxT("\n"));

    if(cbDebuggerCommonConfig::GetFlag(cbDebuggerCommonConfig::ShowDebuggersLog))
        m_DebugLog->Append(wxT("*** Dispatch command: ") + cmd.cmdtext);

    if(cmd.cmdtext.Len()>1)
    {
        char *cmdc=new char[cmd.cmdtext.Len()];
        for(size_t i=0;i<cmd.cmdtext.Len();i++)
        {
            cmdc[i]=cmd.cmdtext[i];
        }
        m_socket.Write(cmdc,cmd.cmdtext.Len());
        delete[] cmdc;
    }

    if(WaitForResponse(cmd.type) || cmd.poll)
        m_DispatchedCommands.push_front(cmd);

    m_CommandQueue.pop_back();

    return true;
}

bool SquirrelDebugger::QueueCommand(wxString cmd, sqCMDType cmdtype, bool poll)
{
    if(cmd.Last() != '\n')
        cmd.Append(wxT("\n"));

    SquirrelCmdDispatchData dd;
    dd.cmdtext=cmd;
    dd.type=cmdtype;
    dd.poll = poll;
    m_CommandQueue.push_front(dd);
    if(m_CommandQueue.size() == 1)
        DispatchCommand();
}


static wxString SquirrelFileExtensions=wxT("*.nut;*.script");

bool SquirrelDebugger::IsSquirrelFile(const wxString &file) const
{
    if(WildCardListMatch(SquirrelFileExtensions,file))
        return true;
    return false;
}

void SquirrelDebugger::DispatchBreakpointCommands()
{
    for(BPList::iterator itr=m_bplist.begin();itr!=m_bplist.end();++itr)
    {
        wxString sfile=(*itr)->GetLocation();

        int line=(*itr)->GetLine();
        wxString cmd=wxString::Format(_T("ab:%x:"),line) +sfile+_T("\n");
        QueueCommand(cmd,SQDBG_CMD_ADD_BP);
    }
}


void SquirrelDebugger::DispatchWatchCommands()
{
    wxString command;

    for (unsigned int i=0;i<m_watchlist.size();++i)
    {
        SquirrelWatch::Pointer w=m_watchlist[i];
        wxString s;
        w->GetSymbol(s);

        command=wxString::Format(_T("aw:%x:"),w->GetNumID()) + s +_T("\n");
        QueueCommand(command,SQDBG_CMD_ADD_WA);
    }
}


void SquirrelDebugger::RequestUpdate(DebugWindows window)
{
//    switch (window)
//    {
//        case Backtrace:
//            RunCommand(CMD_BACKTRACE);
//            break;
//        case CPURegisters:
//            RunCommand(CMD_REGISTERS);
//            break;
//        case Disassembly:
//            RunCommand(CMD_DISASSEMBLE);
//            break;
//        case ExamineMemory:
//            RunCommand(CMD_MEMORYDUMP);
//            break;
//        case Threads:
//            RunCommand(CMD_RUNNINGTHREADS);
//            break;
//        case Watches:
//            if (IsWindowReallyShown(Manager::Get()->GetDebuggerManager()->GetWatchesDialog()->GetWindow()))
//            {
//                if (!m_locals_watch)
//                {
//                    m_locals_watch = SquirrelWatch::Pointer(new SquirrelWatch(_T("*Locals:")));
//                    cbWatch::AddChild(m_locals_watch,SquirrelWatch::Pointer(new SquirrelWatch(_("#child"))));
//                    Manager::Get()->GetDebuggerManager()->GetWatchesDialog()->AddSpecialWatch(m_locals_watch,false);
//                }
//            }
//            break;
//        default:
//            break;
//    }

}


void SquirrelDebugger::ClearActiveMarkFromAllEditors()
{
    EditorManager* edMan = Manager::Get()->GetEditorManager();
    for (int i = 0; i < edMan->GetEditorsCount(); ++i)
    {
        cbEditor* ed = edMan->GetBuiltinEditor(i);
        if (ed)
            ed->SetDebugLine(-1);
    }
}

bool SquirrelDebugger::IsAttachedToProcess() const
{
//    return false;
    EditorBase *ed=Manager::Get()->GetEditorManager()->GetActiveEditor();
    if(!ed)
        return false;
    wxString s=ed->GetFilename();
    if(!(wxFileName(s).FileExists() && IsSquirrelFile(s)))
        return false;

    return true;
}


bool SquirrelDebugger::Debug(bool breakOnEntry)
{
    if(m_DebuggerActive)
        return 0;

    m_DispatchedCommands.clear();
    m_curline=0;


    if(!m_RunTarget)
    {
        // We don't need any open files...

        //cbEditor *ed=Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
        //if(!ed)
        //    return false;
        //wxString s=ed->GetFilename();
        //m_RunTarget=s;
    }

    PluginManager *plm = Manager::Get()->GetPluginManager();
    CodeBlocksEvent evt(cbEVT_DEBUGGER_STARTED);
    plm->NotifyPlugins(evt);
    int nRet = evt.GetInt();
    if (nRet < 0)
    {
        cbMessageBox(_T("A plugin interrupted the debug process."));
        m_DebugLog->Append(_("*** Aborted by plugin"));
        return false;
    }


    SqDebuggerConfiguration &cfg =  GetActiveConfigEx();
    wxString ip = cfg.GetTargetIP();
    int port = cfg.GetTargetPort();
    Manager::Get()->GetLogManager()->Log(_T("Running Squirrel debugger with target: ")+ip + wxString::Format(wxT(":%d"),port));
    wxIPV4address ip_addr;
    ip_addr.Hostname(ip);
    ip_addr.Service(port);

    m_socket.SetEventHandler(*this, ID_Socket);
    m_socket.SetNotify(wxSOCKET_CONNECTION_FLAG |
                    wxSOCKET_INPUT_FLAG |
                    wxSOCKET_OUTPUT_FLAG|
                    wxSOCKET_LOST_FLAG);
    m_socket.Notify(true);


    wxString address_string = ip + wxT(":") + wxString::Format(_("%d"),port);
    wxString update_msg = wxString::Format(_("Try to connect to target (%d) "),1) + address_string;

    wxProgressDialog dialog(_("Connect to target"),update_msg
                            ,100
                            ,NULL
                            ,wxPD_AUTO_HIDE|wxPD_APP_MODAL|wxPD_CAN_ABORT);
    dialog.Update(50);
    int try_counter = 1;
    while(true)
    {
        if(m_socket.Connect(ip_addr))
            break;
        wxMilliSleep(800);
        try_counter++;
        update_msg = wxString::Format(_("Try to connect to target (%d) "),try_counter) + address_string;
        if(!dialog.Pulse(update_msg))
        {
            //user abort
            Manager::Get()->GetLogManager()->Log(_T("Squirrel Debugger: user abort"));
            return false;
        }
    }
    m_DebuggerActive=true;
    m_connected = true;

    m_DebugLog->Append(_("*** Connected to ") + address_string);

    // Te debugger has started, so lets switch to appropriate layout
    SwitchToDebuggingLayout();


    //wxSetWorkingDirectory(olddir);

    // Send "ready" to the target tos signal that we are ready
    QueueCommand(wxT("rd"),SQDBG_CMD_RDY);
    m_stopped = false;

    DispatchBreakpointCommands();
    DispatchWatchCommands();


    CodeBlocksLogEvent evtlog(cbEVT_SWITCH_TO_LOG_WINDOW,m_DebugLog);
    Manager::Get()->ProcessEvent(evtlog);

    return 0;
}

void SquirrelDebugger::Continue()
{
    if(m_DebuggerActive)
    {
        m_DebugLog->Append(wxT("*** continue"));
        QueueCommand(_T("go"),SQDBG_CMD_RESUME,false);
    }
}

void SquirrelDebugger::Next()
{
    if(m_DebuggerActive)
    {
        m_DebugLog->Append(wxT("*** next"));
        QueueCommand(_T("so"),SQDBG_CMD_STEP_OVER,false);
    }
}

void SquirrelDebugger::NextInstruction()
{
    m_DebugLog->Append(wxT("*** Next instruction"));
    Next();
}

void SquirrelDebugger::Step()
{
    if(m_DebuggerActive)
    {
        m_DebugLog->Append(wxT("*** Step into"));
        QueueCommand(_T("si"),SQDBG_CMD_STEP_INTO,false);
    }
}

void SquirrelDebugger::StepIntoInstruction()
{
    if(m_DebuggerActive)
    {
        m_DebugLog->Append(wxT("*** Step into"));
        QueueCommand(_T("si"),SQDBG_CMD_STEP_INTO,false);
    }
}

void SquirrelDebugger::StepOut()
{
    if(m_DebuggerActive)
    {
        m_DebugLog->Append(wxT("*** Step out"));
        QueueCommand(_T("sr"),SQDBG_CMD_STEP_INTO,false);
    }
}

void SquirrelDebugger::Break()
{
    if(!m_DebuggerActive)
        return;
    if(IsStopped())
        return;

    m_DebugLog->Append(wxT("*** Break"));
    QueueCommand(_T("sp"),SQDBG_CMD_SUSPEND,false);
}


void SquirrelDebugger::Stop()
{
    if(m_DebuggerActive)
    {
        m_DebugLog->Append(wxT("*** Terminate"));
        QueueCommand(_T("tr"),SQDBG_CMD_TERMINATE,false);
        StopDebugging();
    }
    m_RunTarget=_("");
}

bool SquirrelDebugger::RunToCursor(const wxString& filename, int line, const wxString& line_text)
{
//    if(filename!=m_curfile)
//        return false;
    if(!m_DebuggerActive)
        return false;
    wxString sfile=filename;

    // The debugger does not support directly "run to cursor", so we add a temporary breakpoint
    SqBreakpoint::Pointer breakpoint = SqBreakpoint::Pointer(new SqBreakpoint(filename,line));
    m_tmp_bp.push_back(breakpoint);
    wxString cmd=_T("ab")+wxString::Format(_T(":%x:"),line)+filename+_T("\n");
    m_DebugLog->Append(wxT("*** Add tmp breakpoint : ")+cmd);
    QueueCommand(cmd,SQDBG_CMD_ADD_BP);

    // If the debugger is stopped we have to restart it
    if(m_stopped)
        QueueCommand(wxT("go"),SQDBG_CMD_RESUME);

    return true;
}

void SquirrelDebugger::SetNextStatement(const wxString& filename, int line)
{
    if(filename!=m_curfile)
        return;
    if(m_DebuggerActive)
    {

    }
}


cb::shared_ptr<cbBreakpoint>  SquirrelDebugger::GetBreakpoint(int index)
{
    if(index < 0 || index > m_bplist.size())
        return cb::shared_ptr<cbBreakpoint>();

    BPList::iterator itr = m_bplist.begin();
    for(int i = 0; itr != m_bplist.end();itr++)
    {
        if(i == index)
            return (*itr);
        i++;
    }
    return cb::shared_ptr<cbBreakpoint>();
}

cb::shared_ptr<const cbBreakpoint>  SquirrelDebugger::GetBreakpoint(int index) const
{
    if(index < 0 || index > m_bplist.size())
        return cb::shared_ptr<cbBreakpoint>();

    BPList::const_iterator itr = m_bplist.begin();
    for(int i = 0; itr != m_bplist.end();itr++)
    {
        if(i == index)
            return (*itr);
        i++;
    }
    return cb::shared_ptr<cbBreakpoint>();
}


cb::shared_ptr<cbBreakpoint>  SquirrelDebugger::AddBreakpoint(const wxString& file, int line)
{
    //if(!IsSquirrelFile(file))
    //    return cb::shared_ptr<cbBreakpoint>();
    for (BPList::iterator itr=m_bplist.begin(); itr!=m_bplist.end(); ++itr)
    {
        if((*itr)->GetLocation()==file && (*itr)->GetLine()==line)
            return cb::shared_ptr<cbBreakpoint>();
    }
    SqBreakpoint::Pointer p(new SqBreakpoint(file,line));
    m_bplist.push_back(p);
    if(m_DebuggerActive) // if the debugger is running already we need to send a message to the interpreter to add the new breakpoint
    {
        wxString sfile=file;
        wxString cmd=_T("ab")+wxString::Format(_T(":%x:"),line)+sfile+_T("\n");
        m_DebugLog->Append(wxT("*** Add breakpoint : ")+cmd);
        QueueCommand(cmd,SQDBG_CMD_ADD_BP);
    }
    return p;
}

void SquirrelDebugger::DeleteBreakpoint(cb::shared_ptr<cbBreakpoint> bp)
{
    //if(!IsSquirrelFile(bp->GetLocation()))
    //    return;
    wxString sfile=bp->GetLocation();
    int line = bp->GetLine();

    BPList::iterator  itr = m_bplist.begin();
    for(;itr != m_bplist.end();++itr)
    {
        if((*itr) != bp)
            continue;

        m_bplist.erase(itr);
        if(m_DebuggerActive)
        {
            wxString cmd=_T("rb")+wxString::Format(_T(":%x:"),line)+sfile+_T("\n");
            m_DebugLog->Append(wxT("*** Remove breakpoint : ")+cmd);
            QueueCommand(cmd,SQDBG_CMD_REM_BP);
         }
         return;
    }
}


void SquirrelDebugger::DeleteAllBreakpoints()
{
    BPList::iterator itr = m_bplist.begin();
    for (;itr != m_bplist.end();)
    {
        cb::shared_ptr<cbBreakpoint> bp=(*itr);
        wxString sfile=bp->GetLocation();
        int line = bp->GetLine();
        m_bplist.erase(itr);
        if(m_DebuggerActive)
        {
            wxString cmd=_T("rb")+wxString::Format(_T(":%x:"),line)+sfile+_T("\n");
            m_DebugLog->Append(wxT("*** Remove breakpoint : ")+cmd);
            QueueCommand(cmd,SQDBG_CMD_REM_BP);
        }
        ++itr;
    }
}


cb::shared_ptr<cbWatch> SquirrelDebugger::AddWatch(const wxString& symbol)
{
    wxString sym(symbol);
    sym = sym.Trim().Trim(false);
    SquirrelWatch::Pointer pwatch(new SquirrelWatch(sym));
    m_watchlist.push_back(pwatch);

    wxString debug_output;
    debug_output << wxT("*** Add watch: ") << symbol << wxString::Format(wxT(" with id: %04x"),pwatch->GetNumID());
    m_DebugLog->Append(debug_output);

    if(m_DebuggerActive)
        QueueCommand(wxT("aw:") + wxString::Format(wxT("%x:"),pwatch->GetNumID())+sym+_T("\n"),SQDBG_CMD_ADD_WA);

    return pwatch;
}

void SquirrelDebugger::DeleteWatch(cb::shared_ptr<cbWatch> watch)
{
    unsigned int i;
    for (i=0;i<m_watchlist.size();++i)
    {
        if (m_watchlist[i]==watch)
            break;
    }
    if(i==m_watchlist.size())
        return;

    m_watchlist.erase(m_watchlist.begin()+i);

    if(m_DebuggerActive)
        QueueCommand(wxT("rw:") + wxString::Format(wxT("%x"),dynamic_cast<SquirrelWatch*>(watch.get())->GetNumID()),SQDBG_CMD_REM_WA);

    wxString debug_output;
    wxString symbol;
    watch->GetSymbol(symbol);
    debug_output << wxT("*** Remove watch: ") << symbol << wxString::Format(wxT(" with id: %04x"),dynamic_cast<SquirrelWatch*>(watch.get())->GetNumID());
    m_DebugLog->Append(debug_output);
}

bool SquirrelDebugger::HasWatch(cb::shared_ptr<cbWatch> watch)
{
    unsigned int i;
    for (i=0;i<m_watchlist.size();++i)
    {
        if (m_watchlist[i]==watch)
            return true;
    }
    return watch = m_locals_watch;
}

void SquirrelDebugger::ShowWatchProperties(cb::shared_ptr<cbWatch> watch)
{

}

bool SquirrelDebugger::SetWatchValue(cb::shared_ptr<cbWatch> watch, const wxString &value)
{
    return false;
}

void SquirrelDebugger::ExpandWatch(cb::shared_ptr<cbWatch> watch)
{
    if(IsRunning())
    {

    }
}

void SquirrelDebugger::CollapseWatch(cb::shared_ptr<cbWatch> watch)
{
    if(IsRunning())
    {

    }
}

void SquirrelDebugger::UpdateWatch(cb::shared_ptr<cbWatch> watch)
{
    if(IsRunning())
    {
//        watch->RemoveChildren(); //TODO: Update instead of removing children

        wxString symbol;
        watch->GetSymbol(symbol);
//        DispatchCommands(_T("ps ")+symbol+_T("\n"),DBGCMDTYPE_WATCHEXPRESSION);
    }
}

void SquirrelDebugger::OnWatchesContextMenu(wxMenu &menu, const cbWatch &watch, wxObject *property)
{
}


int SquirrelDebugger::GetStackFrameCount() const
{
    return m_stackinfo.frames.size();
}

cb::shared_ptr<const cbStackFrame> SquirrelDebugger::GetStackFrame(int index) const
{
    return m_stackinfo.frames[index];
}

void SquirrelDebugger::SwitchToFrame(int number)
{
    if(number<0)
        return;
    if(number>=(int)m_stackinfo.frames.size())
    {
        wxMessageBox(_("Frame out of bounds"));
        return;
    }
    if(m_DebuggerActive)
    {
        int old_frame = m_stackinfo.activeframe;
        m_DebugLog->Append(wxString::Format(wxT("*** Switch to frame %d ")));
        DebuggerManager &dbg_manager = *Manager::Get()->GetDebuggerManager();
        dbg_manager.GetWatchesDialog()->RemoveWatch(m_stackinfo.frames[old_frame]->m_locals);
        dbg_manager.GetWatchesDialog()->AddWatch(m_stackinfo.frames[number]->m_locals);
        m_stackinfo.frames[number]->m_locals->Expand(true);
        dbg_manager.GetWatchesDialog()->UpdateWatches();
        ClearActiveMarkFromAllEditors();

        // Update the editor
        long tmp_line_nr = -1;
        m_stackinfo.frames[number]->GetLine().ToLong(&tmp_line_nr);
        SyncEditor(m_stackinfo.frames[number]->GetFilename(),tmp_line_nr);
        m_stackinfo.activeframe = number;

        SquirrelStackFrame::Pointer f = m_stackinfo.frames[number];

        // Update all watches (they are saved in call stack frames
        int child_count = f->m_watches->GetChildCount();
        SquirrelWatch::Pointer old_child;
        SquirrelWatchesContainer::iterator itr;
        for(itr = m_watchlist.begin();itr != m_watchlist.end();++itr)
        {
            for(int i = 0;i < child_count;i++)
            {
                wxString stack_sym, watch_sym;
                f->m_watches->GetChild(i)->GetSymbol(stack_sym);
                (*itr)->GetSymbol(watch_sym);
                if( stack_sym == watch_sym)
                {
                    wxString value,type;
                    f->m_watches->GetChild(i)->GetValue(value);
                    f->m_watches->GetChild(i)->GetValue(type);
                    (*itr)->SetValue(value);
                    (*itr)->SetType(type);
                }
            }
        }
        return;
    }
}

int SquirrelDebugger::GetActiveStackFrame() const
{
    return m_stackinfo.activeframe;
}


cbConfigurationPanel* SquirrelDebugger::GetConfigurationPanel(wxWindow* parent)
{
//    MyDialog* dlg = new MyDialog(this, *m_pKeyProfArr, parent,
//        wxT("Keybindings"), mode);

    return NULL;//new ConfigDialog(parent, this);
}

// destructor
SquirrelDebugger::~SquirrelDebugger()
{

}

void SquirrelDebugger::OnAttachReal()
{
	// do whatever initialization you need for your plugin
	// NOTE: after this function, the inherited member variable
	// m_IsAttached will be TRUE...
	// You should check for it in other functions, because if it
	// is FALSE, it means that the application did *not* "load"
	// (see: does not need) this plugin...

    m_DebugLog = new TextCtrlLogger(true);
    CodeBlocksLogEvent evtlog(cbEVT_ADD_LOG_WINDOW,m_DebugLog, _("SquirrelDebugger"));
    Manager::Get()->ProcessEvent(evtlog);

    DebuggerManager &dbg_manager = *Manager::Get()->GetDebuggerManager();
    dbg_manager.RegisterDebugger(this);

    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_TOOLTIP, new cbEventFunctor<SquirrelDebugger, CodeBlocksEvent>(this, &SquirrelDebugger::OnValueTooltip));


    if(!m_socket.Initialize())
        m_DebugLog->Append(_("Could not initialize socket..."),Logger::error);


    Manager::Get()->GetDebuggerManager()->GetWatchesDialog()->UpdateWatches();
}

void SquirrelDebugger::OnReleaseReal(bool appShutDown)
{
	// do de-initialization for your plugin
	// if appShutDown is false, the plugin is unloaded because Code::Blocks is being shut down,
	// which means you must not use any of the SDK Managers
	// NOTE: after this function, the inherited member variable
	// m_IsAttached will be FALSE...


    m_socket.Destroy();


    CodeBlocksLogEvent evt(cbEVT_REMOVE_LOG_WINDOW,m_DebugLog);
    Manager::Get()->ProcessEvent(evt);
    m_DebugLog = 0L;

    // Does we need to unregister?
//    DebuggerManager &dbg_manager = *Manager::Get()->GetDebuggerManager();
//    dbg_manager.UnregisterDebugger(this);
    Manager::Get()->GetDebuggerManager()->GetWatchesDialog()->RemoveWatch(m_locals_watch);
    m_locals_watch = SquirrelWatch::Pointer();


}

void SquirrelDebugger::SetWatchTooltip(const wxString &tip, int definition_length)
{
    EditorManager* edMan = Manager::Get()->GetEditorManager();
    EditorBase* base = edMan->GetActiveEditor();
    cbEditor* ed = base && base->IsBuiltinEditor() ? static_cast<cbEditor*>(base) : 0;
    if (!ed)
        return;
    ed->GetControl()->CallTipShow(m_watch_tooltip_pos, tip);
    ed->GetControl()->CallTipSetHighlight(0,definition_length);

}
int SquirrelDebugger::GetThreadsCount()  const
{
        return 1;
}

cb::shared_ptr<const cbThread> SquirrelDebugger::GetThread(int index) const
{
    return cb::shared_ptr<const cbThread>();
}

bool SquirrelDebugger::SwitchToThread(int thread_number)
{
    return false;
};

void SquirrelDebugger::OnValueTooltip(CodeBlocksEvent& event)
{
    event.Skip();
    if (!m_DebuggerActive)
        return;
    if (!IsStopped())
        return;

    EditorBase* base = event.GetEditor();
    cbEditor* ed = base && base->IsBuiltinEditor() ? static_cast<cbEditor*>(base) : 0;
    if (!ed)
        return;

    if(ed->IsContextMenuOpened())
    {
    	return;
    }

	// get rid of other calltips (if any) [for example the code completion one, at this time we
	// want the debugger value call/tool-tip to win and be shown]
    if(ed->GetControl()->CallTipActive())
    {
    	ed->GetControl()->CallTipCancel();
    }

    const int style = event.GetInt();
    if (style != wxSCI_P_DEFAULT && style != wxSCI_P_OPERATOR && style != wxSCI_P_IDENTIFIER && style != wxSCI_P_CLASSNAME)
        return;

    wxPoint pt;
    pt.x = event.GetX();
    pt.y = event.GetY();
    int pos = ed->GetControl()->PositionFromPoint(pt);
    int start = ed->GetControl()->WordStartPosition(pos, true);
    int end = ed->GetControl()->WordEndPosition(pos, true);
    while(ed->GetControl()->GetCharAt(start-1)==_T('.'))
        start=ed->GetControl()->WordStartPosition(start-2, true);
    wxString token;
    if (start >= ed->GetControl()->GetSelectionStart() &&
        end <= ed->GetControl()->GetSelectionEnd())
    {
        token = ed->GetControl()->GetSelectedText();
    }
    else
        token = ed->GetControl()->GetTextRange(start,end);
    if (token.IsEmpty())
        return;

    wxString cmd;
    cmd+=_T("pw ")+token+_T("\n");
//    DispatchCommands(cmd,DBGCMDTYPE_WATCHTOOLTIP,true);
    m_watch_tooltip_pos=pos;
}

