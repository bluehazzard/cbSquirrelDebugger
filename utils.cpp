
#include "utils.h"

namespace SquirrelDebugger_util
{

wxString GetSocketErrorMsg(int pSockError)
{
 	switch(pSockError)
 	{
 	case wxSOCKET_NOERROR:
 	return wxString(_("wxSOCKET_NOERROR"));

 	case wxSOCKET_INVOP:
 	return wxString(_("wxSOCKET_INVOP"));

 	case wxSOCKET_IOERR:
 	return wxString(_("wxSOCKET_IOERR"));

 	case wxSOCKET_INVADDR:
 	return wxString(_("wxSOCKET_INVADDR"));

	case wxSOCKET_NOHOST:
 	return wxString(_("wxSOCKET_NOHOST"));

 	case wxSOCKET_INVPORT:
 	return wxString(_("wxSOCKET_INVPORT"));

 	case wxSOCKET_WOULDBLOCK:
 	return wxString(_("wxSOCKET_WOULDBLOCK"));

 	case wxSOCKET_TIMEDOUT:
 	return wxString(_("wxSOCKET_TIMEDOUT"));

 	case wxSOCKET_MEMERR:
 	return wxString(_("wxSOCKET_MEMERR"));

 	default:
 	return wxString(_("Unknown"));
 	}
}

SquirrelStackFrame::Pointer FindStackFrame(SquirrelStackFrame::Pointer frame,StackList& lst)
{
    if(!frame)
        return SquirrelStackFrame::Pointer();

    StackList::iterator itr;
    for(itr = lst.begin();itr != lst.end();++itr)
    {
        if(frame->GetSymbol() == (*itr)->GetSymbol() && frame->GetLine() == (*itr)->GetLine() &&
           frame->GetFilename() == (*itr)->GetFilename())
        {
            return (*itr);
        }
    }
    return SquirrelStackFrame::Pointer();
}

void RemoveMissingChildren(cb::shared_ptr<cbWatch> parent, const std::set<wxString> &knownsyms)
{
    if(!parent)
        return;

    for (int i=0; i<parent->GetChildCount(); ++i)
    {
        cb::shared_ptr<cbWatch> p(parent->GetChild(i));
        wxString s;
        p->GetSymbol(s);
        if (knownsyms.find(s)!=knownsyms.end())
        {
            parent->RemoveChild(i);
            --i;
            continue;
        }
    }
}

bool WildCardListMatch(wxString list, wxString name)
{
    if(list==_T("")) //any empty list matches everything by default
        return true;
    wxString wildlist=list;
    wxString wild=list.BeforeFirst(';');
    while(wildlist!=_T(""))
    {
        if(wild!=_T("") && ::wxMatchWild(wild,name))
            return true;
        wildlist=wildlist.AfterFirst(';');
        wild=wildlist.BeforeFirst(';');
    }
    return false;
}

wxString DecodeType(wxString key)
{
    if(key == wxT("n"))
        return wxT("null");
    else if(key == wxT("s"))
        return wxT("string");
    else if(key == wxT("i"))
        return wxT("integer");
    else if(key == wxT("f"))
        return wxT("float");
    else if(key == wxT("fn"))
        return wxT("function");
    else if(key == wxT("t"))
        return wxT("table");
    else if(key == wxT("a"))
        return wxT("array");
    else if(key == wxT("g"))
        return wxT("generator");
    else if(key == wxT("h"))
        return wxT("thread");
    else if(key == wxT("x"))
        return wxT("instance");
    else if(key == wxT("y"))
        return wxT("class");
    else if(key == wxT("b"))
        return wxT("bool");
    else if(key == wxT("w"))
        return wxT("weakref");
    else
        return wxT("unknown");
}

wxString PrintNiceValueFromSquirrelObject(SquirrelObjectsSet obj_set, int reference,int lvl,bool value)
{
    if(reference == -1)
        return wxEmptyString;

    wxString lvl_space;
    for(int i = 0; i < lvl*SpacePerLvl; i++)
        lvl_space << wxT(" ");

    SquirrelObjectsSet::iterator obj = obj_set.find(SquirrelObjects::Pointer(new SquirrelObjects(reference)));
    if(obj == obj_set.end())
    {
        return wxString::Format(wxT("Could not find object with ref=%04x"),reference);
    }
    else
    {
        wxString ret;
        ret << (value ? wxT("") :lvl_space);
        ret << wxString::Format(wxT("(ref = 0x%04x)"),reference) << wxT(":") << (*obj)->GetType();
        int child_count = (*obj)->GetChildCount();

        if(child_count > 0)
            ret << wxT("= {\n");

        for(int i = 0; i < child_count;i++)
        {
            // First we print the key
            // format: "value":"type"
            SquirrelObjectChilds::Pointer child = (*obj)->GetChild(i);
            wxString key_type = child->GetKeyType();
            if( key_type == wxT("table") ||
                key_type == wxT("array") ||
                key_type == wxT("class") ||
                key_type == wxT("instance"))
            {
                long key_ref = -1;
                child->GetKeyValue().ToLong(&key_ref);
                ret << PrintNiceValueFromSquirrelObject(obj_set,key_ref,lvl+1);
            } else {
                ret << lvl_space << child->GetKeyValue() << wxT(":") << key_type;
            }

            ret << wxT("=");

            // now we print the value
            // format: "value":"type"
            wxString value_type = child->GetType();
            if( value_type == wxT("table") ||
                value_type == wxT("array") ||
                value_type == wxT("class") ||
                value_type == wxT("instance"))
            {
                    long value_ref = -1;
                    child->GetValue().ToLong(&value_ref);
                    ret << PrintNiceValueFromSquirrelObject(obj_set,value_ref,lvl+1,true);
            } else {
                ret << child->GetValue() << wxT(":") << value_type;
            }
            ret << wxT("\n");
        }

        if(child_count > 0)
            ret<< lvl_space <<wxT("}");

        return ret;
    }
}

SquirrelWatch::Pointer FindWatch(long id, SquirrelWatchesContainer &container)
{
    SquirrelWatchesContainer::iterator itr = container.begin();
    for(;itr!=container.end();++itr)
    {
        if((*itr)->GetNumID() == id)
            return (*itr);
        // TODO (bluehazzard#1#): Search also in children
    }
    return SquirrelWatch::Pointer();
}

SquirrelWatch::Pointer FindWatch(const wxString& symbol, SquirrelWatchesContainer &container)
{
    SquirrelWatchesContainer::iterator itr = container.begin();
    for(;itr!=container.end();++itr)
    {
        wxString s;
        (*itr)->GetSymbol(s);
        if(s == symbol)
            return (*itr);
        // TODO (bluehazzard#1#): Search also in children
    }
    return SquirrelWatch::Pointer();
}

BPList::iterator FindBp(const SqBreakpoint& bp, BPList& lst)
{
    BPList::iterator itr = lst.begin();
    for(; itr != lst.end();++itr)
    {
        if((*itr)->GetLocation() == bp.GetLocation() &&
           (*itr)->GetLine() == bp.GetLine())
            return itr;
    }
    return lst.end();
}

bool WaitForResponse(sqCMDType type)
{
    switch(type)
    {
    case SQDBG_CMD_RDY:
    case SQDBG_CMD_TERMINATE:
    case SQDBG_CMD_SUSPEND:
    case SQDBG_CMD_USR:
    case SQDBG_CMD_ADD_WA:
    case SQDBG_CMD_REM_WA:
    case SQDBG_CMD_FLOW_CTRL:
        return false;
    case SQDBG_CMD_ADD_BP:
    case SQDBG_CMD_REM_BP:
    case SQDBG_CMD_STEP_INTO:
    case SQDBG_CMD_STEP_RET:
    case SQDBG_CMD_STEP_OVER:
    case SQDBG_CMD_RESUME:
        return true;
    }
    return false;
}

}

