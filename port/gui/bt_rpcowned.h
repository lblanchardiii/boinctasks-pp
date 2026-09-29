// =============================================================================
// bt_rpcowned.h - give BOINC's RPC reply containers the destructor they lack
// =============================================================================
#pragma once

// RESULTS, PROJECTS, FILE_TRANSFERS, MESSAGES, NOTICES, CC_STATE and
// ALL_PROJECTS_LIST hold their rows through raw pointers that parse() allocates
// with new. None of them has a destructor: the rows are only ever freed by an
// explicit clear(). BOINC Manager keeps one long-lived instance of each and
// every get_*() call clears it before parsing, so it never notices.
//
// A local declared once per poll is different - it goes out of scope with every
// row still allocated. 0.9.5a did exactly that on every host, every poll,
// after the move from eFMer's copy of the library (whose RESULTS had a real
// destructor) to the BOINC submodule, and grew by the size of the farm's task
// list each cycle until Windows ran out of memory overnight.
//
// Declare the reply as BtOwned<RESULTS> instead of RESULTS and it frees itself.
template <class T>
struct BtOwned : T
{
    BtOwned() = default;
    ~BtOwned() { T::clear(); }
    BtOwned(const BtOwned&) = delete;
    BtOwned& operator=(const BtOwned&) = delete;
};
