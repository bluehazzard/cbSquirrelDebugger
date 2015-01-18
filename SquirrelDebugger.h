/***************************************************************
 * Name:      Squirrel Debugger Plugin
 * Purpose:   Code::Blocks plugin
 * Author:    Damien Moore ()
 * Created:   2006-09-28
 * Copyright: Damien Moore
 * License:   GPL
 **************************************************************/

#ifndef SquirrelDEBUGGER_H_INCLUDED
#define SquirrelDEBUGGER_H_INCLUDED

// For compilers that support precompilation, includes <wx/wx.h>
#include <wx/wxprec.h>

#ifndef WX_PRECOMP
	#include <wx/wx.h>
#endif

#include <wx/socket.h>

#include <cbplugin.h> // for "class cbPlugin/cbDebuggerPlugin"
#include <loggers.h>
#include <logger.h>
#include <sdk.h>

#include <debuggermanager.h>
#include "debuggeroptionsdlg.h"


enum sqCMDType
{
    SQDBG_CMD_FLOW_CTRL,
    SQDBG_CMD_RDY,
    SQDBG_CMD_STEP_INTO,
    SQDBG_CMD_STEP_RET,
    SQDBG_CMD_STEP_OVER,
    SQDBG_CMD_RESUME,
    SQDBG_CMD_SUSPEND,
    SQDBG_CMD_TERMINATE,
    SQDBG_CMD_ADD_BP,
    SQDBG_CMD_REM_BP,
    SQDBG_CMD_ADD_WA,
    SQDBG_CMD_REM_WA,
    SQDBG_CMD_USR

};


typedef std::set<int> BPLtype;

class SquirrelStackFrame;

typedef std::vector<cb::shared_ptr<SquirrelStackFrame> > StackList;

class SqBreakpoint:public cbBreakpoint
{
    public:
        SqBreakpoint(wxString file, int line)
        {
            m_file=file;
            m_line=line;
            m_enabled = true;
        }
        virtual ~SqBreakpoint() {}
        void SetEnabled(bool flag) {m_enabled = flag;}
        wxString GetLocation() const {return m_file;}
        int GetLine() const {return m_line;}
        wxString GetLineString() const {return wxString::Format(wxT("%i"),m_line);}
        wxString GetType() const {return m_type;}
        wxString GetInfo() const {return m_info;}
        bool IsEnabled() const {return m_enabled;}
        bool IsVisibleInEditor()const  {return true;}
        bool IsTemporary() const {return false;}

        typedef cb::shared_ptr<SqBreakpoint> Pointer;

    private:
        bool m_enabled;
        wxString m_file;
        int m_line;
        wxString m_type;
        wxString m_info;
};

typedef std::list<SqBreakpoint::Pointer> BPList;

class SquirrelWatch :public cbWatch
{
    public:
        SquirrelWatch(wxString const &symbol) :
            m_symbol(symbol),
            m_has_been_expanded(false)
        {
            m_own_id = GetUnicID();
            m_ref = 0;
        }
        typedef cb::shared_ptr<SquirrelWatch> Pointer;
        virtual ~SquirrelWatch() {}

        void Reset()
        {
            m_own_id = 0;
            m_id = m_type = m_value = wxEmptyString;
            m_has_been_expanded = false;
            RemoveChildren();
            Expand(false);
        }

        wxString const & GetID() const { return m_id; }
        void SetID(wxString const &id) { m_id = id; }

        bool HasBeenExpanded() const { return m_has_been_expanded; }
        void SetHasBeenExpanded(bool expanded) { m_has_been_expanded = expanded; }
        virtual void GetSymbol(wxString &symbol) const { symbol = m_symbol; }
        virtual void GetValue(wxString &value) const { value = m_value; }
        virtual bool SetValue(const wxString &value) { m_value = value; return true; }
        virtual void GetFullWatchString(wxString &full_watch) const { full_watch = m_value; }
        virtual void GetType(wxString &type) const { type = m_type; }
        virtual void SetType(const wxString &type) { m_type = type; }

        int GetRef()    {return m_ref;};
        void SetRef(int r) {m_ref = r;};

        unsigned long GetNumID()    {return m_own_id;};

        virtual wxString const & GetDebugString() const
        {
            m_debug_string = m_id + wxT("->") + m_symbol + wxT(" = ") + m_value;
            return m_debug_string;
        }
	protected:
        virtual void DoDestroy() {}
    private:

        static unsigned long m_unic_id;

        unsigned long GetUnicID()       {m_unic_id++; return m_unic_id;};

        wxString m_id;
        wxString m_symbol;
        wxString m_value;
        wxString m_type;
        unsigned long m_own_id;
        mutable wxString m_debug_string;
        bool m_has_been_expanded;
        int m_ref;
};

typedef std::vector<SquirrelWatch::Pointer> SquirrelWatchesContainer;

class SquirrelObjectChilds
{
    public:
    SquirrelObjectChilds()
    {

    }

    SquirrelObjectChilds(wxString key_val,wxString key_type,wxString val, wxString type)
    {
        m_key_value = key_val;
        m_key_type = key_type;
        m_value = val;
        m_type = type;
    }

    wxString GetKeyValue() {return m_key_value;};
    void SetKeyValue(wxString val) {m_key_value = val;};
    wxString GetKeyType()   {return m_key_type;};
    void SetKeyType(wxString type) {m_key_type = type;};
    wxString GetValue() {return m_value;};
    void SetValue(wxString val) {m_value = val;};
    wxString GetType()  {return m_type;};
    void SetType(wxString type) {m_type = type;};

    wxString GetString()      {return m_key_value + wxT(":") + m_value;};

    typedef cb::shared_ptr<SquirrelObjectChilds> Pointer;

    private:
    wxString m_key_value;
    wxString m_value;
    wxString m_key_type;
    wxString m_type;
};


class SquirrelObjects
{
    public:
    SquirrelObjects(long ref) : m_ref(ref)
    {

    }

    bool operator<(const SquirrelObjects& other) const
    {
        return(m_ref < other.m_ref);
    }

    long GetRef()   const     {return m_ref;};
    wxString GetValue() {return m_value;};
    void SetValue(wxString val) {m_value = val;};
    wxString GetType()  {return m_type;};
    void SetType(wxString type) {m_type = type;};

    wxString GetNiceValueString()
    {
        wxString ret;
        bool children = false;
        ret << wxT("Obj:ref=") << wxString::Format(wxT("0x04%x"),m_ref) << wxT(":") << m_type ;
        std::vector<SquirrelObjectChilds::Pointer>::iterator itr = m_childs.begin();
        if(itr != m_childs.end())
        {
            ret << wxT("= {\n");
            children = true;
        }
        for(; itr != m_childs.end();++itr)
        {
            ret << (*itr)->GetKeyValue() << wxT("\n");
        }
        if(children)
            ret << wxT("}");

        return ret;
    }

    void AddChild(SquirrelObjectChilds::Pointer child)
    {
        m_childs.push_back(child);
    }

    SquirrelObjectChilds::Pointer GetChild(int index)
    {
        if(index < 0 || index > m_childs.size())
            return SquirrelObjectChilds::Pointer();
        return m_childs[index];
    }

    int GetChildCount() {return m_childs.size();};

    void DeleteAllChilds()  {m_childs.clear();};

    typedef cb::shared_ptr<SquirrelObjects> Pointer;

    private:
    long m_ref;
    wxString m_value;
    wxString m_type;
    std::vector<SquirrelObjectChilds::Pointer> m_childs;
};

class CompareSquirrelObjectsPointer { // simple comparison function
   public:
      bool operator()(const SquirrelObjects::Pointer a,const SquirrelObjects::Pointer b)
      { return a->GetRef() < b->GetRef(); }
};


typedef std::set<SquirrelObjects::Pointer,CompareSquirrelObjectsPointer> SquirrelObjectsSet;


class SquirrelStackFrame : public cbStackFrame
{
    public:
    SquirrelStackFrame()  {
    m_locals = SquirrelWatch::Pointer(new SquirrelWatch(wxT("local")));
    m_watches = SquirrelWatch::Pointer(new SquirrelWatch(wxT("watches")));
    };
    ~SquirrelStackFrame() {};

    typedef cb::shared_ptr<SquirrelStackFrame> Pointer;

    bool operator<(const SquirrelStackFrame &other) const
    {
        return (this->GetNumber() < other.GetNumber());
    }


    SquirrelWatch::Pointer  m_locals;
    SquirrelWatch::Pointer  m_watches;
};

struct CompareSuirrelStackFramePointer {
    bool operator()(const SquirrelStackFrame::Pointer a, const SquirrelStackFrame::Pointer b)
    {
        return (a->GetNumber() < b->GetNumber());
    }

};

typedef std::list<SquirrelStackFrame::Pointer,CompareSuirrelStackFramePointer> SquirrelStackFrameList;




struct StackInfo
{
    int activeframe;
    StackList frames;
};

struct SquirrelCmdDispatchData
{
    SquirrelCmdDispatchData()
    {
        poll = false;
        type = SQDBG_CMD_FLOW_CTRL;
    };
    sqCMDType type;
    wxString cmdtext;
    bool poll;
};

class SquirrelDebugger : public cbDebuggerPlugin
{
    public:
		/** Constructor. */
        SquirrelDebugger();
		/** Destructor. */
        virtual ~SquirrelDebugger();

        virtual void OnAttachReal();
        virtual void OnReleaseReal(bool appShutDown);

        virtual void SetupToolsMenu(wxMenu &) {}
        virtual bool SupportsFeature(cbDebuggerFeature::Flags f);

        /** @brief Is the plugin stopped on breakpoint? */
        virtual bool IsStopped() const  {return m_stopped;}

        /** @brief Get the exit code of the last debug process. */
//        virtual int GetExitCode() const  {}

        // stack frame calls;
        virtual int GetStackFrameCount() const;
        virtual cb::shared_ptr<const cbStackFrame> GetStackFrame(int index) const;
        virtual void SwitchToFrame(int number);
        virtual int GetActiveStackFrame() const;

        // breakpoints calls
        /** @brief Request to add a breakpoint.
          * @param file The file to add the breakpoint based on a file/line pair.
          * @param line The line number to put the breakpoint in @c file.
          * @return True if succeeded, false if not.
          */
        virtual cb::shared_ptr<cbBreakpoint> AddBreakpoint(const wxString& filename, int line);

        /** @brief Request to add a breakpoint based on a data expression.
          * @param dataExpression The data expression to add the breakpoint.
          * @return True if succeeded, false if not.
          */
        virtual cb::shared_ptr<cbBreakpoint> AddDataBreakpoint(const wxString& dataExpression) {return cb::shared_ptr<cbBreakpoint>();}
        virtual int GetBreakpointsCount() const  {return m_bplist.size();}
        virtual cb::shared_ptr<cbBreakpoint> GetBreakpoint(int index);
        virtual cb::shared_ptr<const cbBreakpoint> GetBreakpoint(int index) const;
        virtual void UpdateBreakpoint(cb::shared_ptr<cbBreakpoint> breakpoint)  {}
        virtual void DeleteBreakpoint(cb::shared_ptr<cbBreakpoint> breakpoint);
        virtual void DeleteAllBreakpoints();
        virtual void ShiftBreakpoint(int index, int lines_to_shift)  {}
        // threads
        //virtual int GetThreadsCount() const  {return 0;}
        //virtual cb::shared_ptr<const cbThread> GetThread(int index) const  { cb::shared_ptr<const cbThread> p(new cbThread()); return p;}
        //virtual bool SwitchToThread(int thread_number)  {return false;}

        // watches
        virtual cb::shared_ptr<cbWatch> AddWatch(const wxString& symbol);
        virtual void DeleteWatch(cb::shared_ptr<cbWatch> watch);
        virtual bool HasWatch(cb::shared_ptr<cbWatch> watch);
        virtual void ShowWatchProperties(cb::shared_ptr<cbWatch> watch);
        virtual bool SetWatchValue(cb::shared_ptr<cbWatch> watch, const wxString &value);
        virtual void ExpandWatch(cb::shared_ptr<cbWatch> watch);
        virtual void CollapseWatch(cb::shared_ptr<cbWatch> watch);
        virtual void UpdateWatch(cb::shared_ptr<cbWatch> watch);
        virtual void OnWatchesContextMenu(wxMenu &menu, const cbWatch &watch, wxObject *property);

        virtual void SendCommand(const wxString& cmd, bool debugLog);

        virtual void AttachToProcess(const wxString& pid)  {}
        virtual void DetachFromProcess()  {}
        virtual bool IsAttachedToProcess() const;

        virtual void GetCurrentPosition(wxString &filename, int &line)  {filename=m_curfile;line=m_curline;}

        virtual void ConvertDirectory(wxString& str, wxString base = _T(""), bool relative = true) {}
        virtual cbProject* GetProject() {return NULL;}
        virtual void ResetProject() {}
        virtual void CleanupWhenProjectClosed(cbProject *project) {}

        virtual void RequestUpdate(DebugWindows window);

// Debugger Plugin Specific Virtuals


        virtual int GetThreadsCount()  const;
        virtual cb::shared_ptr<const cbThread> GetThread(int index) const;
        virtual bool SwitchToThread(int thread_number);

        virtual bool IsBusy() const {return false;}
        virtual void EnableBreakpoint(cb::shared_ptr<cbBreakpoint> breakpoint, bool enable) {}
        virtual bool Debug(bool breakOnEntry);
		virtual void Continue();
		virtual void Next();
        virtual void NextInstruction();
		virtual void Step();
        virtual void StepIntoInstruction();
        virtual void StepOut();
		virtual void Stop();
		virtual void Break();
        virtual bool RunToCursor(const wxString& filename, int line, const wxString& line_text);
        virtual void SetNextStatement(const wxString& filename, int line);
        bool IsRunning() const          { return m_DebuggerActive; } /** Is the plugin currently debugging? */
        int GetExitCode() const { return 0; }

// Misc Plugin Virtuals
        virtual cbDebuggerConfiguration* LoadConfig(const ConfigManagerWrapper &config) {return new SqDebuggerConfiguration(config);}
        virtual int GetConfigurationPriority() const { return 50; }
        virtual int GetConfigurationGroup() const { return cgUnknown; }
        virtual cbConfigurationPanel* GetConfigurationPanel(wxWindow* parent); /** Return plugin's configuration panel.*/
        virtual cbConfigurationPanel* GetProjectConfigurationPanel(wxWindow* parent, cbProject* project){ return 0; } /** Return plugin's configuration panel for projects.*/
        virtual void BuildMenu(wxMenuBar* menuBar) {} /** add plugin items to the main menu bar*/

        void OnValueTooltip(CodeBlocksEvent& event);
        void SetWatchTooltip(const wxString &tip, int definition_length);

        SqDebuggerConfiguration& GetActiveConfigEx()
        {
            return static_cast<SqDebuggerConfiguration&>(GetActiveConfig());
        }

/// Non-boiler plate methods
    public:
        void UpdateMenu();
        void UpdateConfig() {;}
        void CreateMenu();

    private:
        // Not really used, because we can not distinguish squirrel files from the file extension, because the debugger
        // can send us temporary files without extension
        bool IsSquirrelFile(const wxString &file) const;

        // Called on every socket event (lost connection, data received etc.)
        void OnSocketEvt(wxSocketEvent &event);
        // Process the response from the debug server
        int ProcessResponse(wxString &resp);

        // Remove all debugger Marks from all editors
        void ClearActiveMarkFromAllEditors();

        // Send next command if the output queue is empty
        bool DispatchCommand();

        // Add a cmd to the command queue. This command gets send as soon all inputs are processed
        bool QueueCommand(wxString cmd, sqCMDType cmdtype, bool poll = false);


        // Queue all oldBreakpoints
        void DispatchBreakpointCommands();
        // Queue all old Watches
        void DispatchWatchCommands();


        wxMenu *LangMenu;  // pointer to the interpreters menu
        wxString m_SquirrelFileExtensions;

        // Clear up, after the debugger has finished
        void StopDebugging();

        // Information about current debug location
        unsigned long m_curline;
        wxString m_curfile;
        bool m_changeposition;

        long m_watch_tooltip_pos;

        // Output from the debugger and program held in buffers
        int m_DebugCommandCount; //number of commands dispatched to the debugger that have yet to be handled
        // All commands that are pending for response are stored in this list (not very useful, because only one cmd can be processed ad time)
        std::list<SquirrelCmdDispatchData> m_DispatchedCommands;
        // A cmd queue, where all outgoing cmds are waiting
        std::list<SquirrelCmdDispatchData> m_CommandQueue;

        // true if the debugger is halted on a breakpoint or error
        bool m_stopped;
        // true if the debugger has a connection to a server and is active
        bool m_DebuggerActive;
        wxString m_debugfile; // file and line of current debug code position
        wxString m_debugline;

        // breakpoint list
        BPList m_bplist;
        // List with all temporary breakpoints (e.g. from "run to cursor")
        BPList m_tmp_bp;
        StackInfo m_stackinfo; //call stack
        SquirrelWatchesContainer m_watchlist;   // All non automatic watches
        SquirrelWatch::Pointer m_locals_watch;  // Not used, should be removed
        SquirrelWatch::Pointer m_functions_watch; // Not used, should be removed
        SquirrelWatch::Pointer m_classes_watch; // Not used, should be removed
        SquirrelWatch::Pointer m_modules_watch; // Not used, should be removed
        SquirrelWatchesContainer m_local_watch_list;



        TextCtrlLogger *m_DebugLog; // pointer to the text log (initialized with OnAttach)
        int m_DebugLogPageIndex; //page index of the debug log

        wxString m_RunTarget;
        bool m_RunTargetSelected;
        wxToolBar *m_pTbar;

        // Buffer used to store incoming incomplete socket data
        wxString m_rec_buffer;
        // Communication socket
        wxSocketClient m_socket;
        // true if a connection to a server is established (the socket is ready)
        bool m_connected;

        // Storage for all squirrel Objects (references)
        SquirrelObjectsSet m_sq_objects;

        DECLARE_EVENT_TABLE();
};

#endif // SquirrelDEBUGGER_H_INCLUDED
