
#ifndef UTILS_H
#define UTILS_H

#include "SquirrelDebugger.h"
#ifndef WX_PRECOMP
	#include <wx/wx.h>
#endif

#include <wx/socket.h>

#include <cbplugin.h> // for "class cbPlugin/cbDebuggerPlugin"
#include <loggers.h>
#include <logger.h>
#include <sdk.h>

namespace SquirrelDebugger_util
{


/** \brief Return a human readable name of the error with the number pSockError
 *
 * \param pSockError int    Error number returned by the socket
 * \return wxString Human readable error name
 *
 */
wxString GetSocketErrorMsg(int pSockError);

/** \brief Find and return the pointer to a stack frame from the list
 *
 * \param frame SquirrelStackFrame::Pointer The stack frame to search (src and line are checked)
 * \param lst StackList&    A list with all stack frames to search in
 * \return SquirrelStackFrame::Pointer  Return an empty pointer if not found or a pointer to the corresponding frame in the list
 *
 */
SquirrelStackFrame::Pointer FindStackFrame(SquirrelStackFrame::Pointer frame,StackList& lst);

/** \brief Remove all children from the parent who are not known (provided by knownsyms)
 *
 * \param parent cb::shared_ptr<cbWatch>    The to clean up list
 * \param knownsyms const std::set<wxString>&   A set with all known symbols, that can remain in the parent
 * \return void
 *
 */
void RemoveMissingChildren(cb::shared_ptr<cbWatch> parent, const std::set<wxString> &knownsyms);


bool WildCardListMatch(wxString list, wxString name);


/** \brief Decode the type of a squirrel object returned by the debugger
 *
 * \param key wxString Encoded string
 * \return wxString A human readable form of the type (ex. string or integer)
 *
 */
wxString DecodeType(wxString key);


const int SpacePerLvl = 4;  /**<  Use 4 spaces per lvl for printing tables*/

/** \brief try to print a squirrel object in a human readable string
 *
 * \param obj_set SquirrelObjectsSet    A set what stores all squirrel references
 * \param reference int  We want a string for this object
 * \param lvl int        For internal use (how deep are we in the table)
 * \param value bool     For internal use (are we a value, or a key)
 * \return wxString      Human readable for of the string
 *
 */
wxString PrintNiceValueFromSquirrelObject(SquirrelObjectsSet obj_set, int reference, int lvl = 0,bool value = false);

/** \brief Find a watch with the corresponding unique id in the container
 *
 * \param id long   unique id of the watch
 * \param container SquirrelWatchesContainer&   Container that stores all watches
 * \return SquirrelWatch::Pointer   Empty Pointer or the found value
 *
 */
SquirrelWatch::Pointer FindWatch(long id, SquirrelWatchesContainer &container);

/** \brief return the iterator of the breakpoint in the list (if not found lst.end())
 *
 * \param bp const SqBreakpoint&    Breakpoint to find
 * \param lst BPList&               List of breakpoints to search in
 * \return BPList::iterator         iterator to the breakpoint in the list, or lst.end() if not found
 *
 */
BPList::iterator FindBp(const SqBreakpoint &bp, BPList& lst);

/** \brief Check if the command gets a response from the debugger server
 *
 * \param type sqCMDType Command to check
 * \return bool   True if the debugger sends a reply on this command
 *
 */
bool WaitForResponse(sqCMDType type);

}
#endif // UTILS_H

