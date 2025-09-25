#include "commutil_win32_dll_event.h"

#ifdef COMMUTIL_WINDOWS

// on MinGW we need to include windows header
#ifdef COMMUTIL_MINGW
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <algorithm>
#include <vector>

namespace commutil {

typedef std::vector<DllListener*> ListenerList;
static ListenerList sListeners;
typedef std::vector<std::pair<ThreadDllEventCB, void*>> CallbackList;
static CallbackList sCallbacks;

void registerDllListener(DllListener* listener) { sListeners.push_back(listener); }

void deregisterDllListener(DllListener* listener) {
    ListenerList::iterator itr = std::find(sListeners.begin(), sListeners.end(), listener);
    if (itr != sListeners.end()) {
        sListeners.erase(itr);
    }
}

void registerDllCallback(ThreadDllEventCB callback, void* userData) {
    sCallbacks.emplace_back(callback, userData);
}

void deregisterDllCallback(ThreadDllEventCB callback) {
    CallbackList::iterator itr =
        std::find_if(sCallbacks.begin(), sCallbacks.end(),
                     [callback](std::pair<ThreadDllEventCB, void*>& cbPair) {
                         return cbPair.first == callback;
                     });
    if (itr != sCallbacks.end()) {
        sCallbacks.erase(itr);
    }
}

void* getDllCallbackUserData(ThreadDllEventCB callback) {
    CallbackList::iterator itr =
        std::find_if(sCallbacks.begin(), sCallbacks.end(),
                     [callback](std::pair<ThreadDllEventCB, void*>& cbPair) {
                         return cbPair.first == callback;
                     });
    if (itr != sCallbacks.end()) {
        return itr->second;
    }
    return nullptr;
}

void purgeDllCallback(DllPurgeFilter* filter) {
    CallbackList::iterator itr = sCallbacks.begin();
    while (itr != sCallbacks.end()) {
        if (filter->purge(itr->first, itr->second)) {
            itr = sCallbacks.erase(itr);
        } else {
            ++itr;
        }
    }
}

static void notifyThreadAttach() {
    for (DllListener* listener : sListeners) {
        listener->onThreadDllAttach();
    }
    for (auto& cbPair : sCallbacks) {
        (*cbPair.first)(COMMUTIL_DLL_THREAD_ATTACH, cbPair.second);
    }
}

static void notifyThreadDetach() {
    for (DllListener* listener : sListeners) {
        listener->onThreadDllDetach();
    }
    for (auto& cbPair : sCallbacks) {
        (*cbPair.first)(COMMUTIL_DLL_THREAD_DETACH, cbPair.second);
    }
}

static void notifyProcessDetach() {
    for (DllListener* listener : sListeners) {
        listener->onProcessDllDetach();
    }
    for (auto& cbPair : sCallbacks) {
        (*cbPair.first)(COMMUTIL_DLL_PROCESS_DETACH, cbPair.second);
    }
}

}  // namespace commutil

BOOL WINAPI DllMain(HINSTANCE /* hinstDLL */,  // handle to DLL module
                    DWORD fdwReason,           // reason for calling function
                    LPVOID lpvReserved)        // reserved
{
    // Perform actions based on the reason for calling.
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            // Initialize once for each new process.
            // Return FALSE to fail DLL load.
            break;

        case DLL_THREAD_ATTACH:
            commutil::notifyThreadAttach();
            break;

        case DLL_THREAD_DETACH:
            commutil::notifyThreadDetach();
            break;

        case DLL_PROCESS_DETACH:
            if (lpvReserved != nullptr) {
                break;  // do not do cleanup if process termination scenario
            }

            // Perform any necessary cleanup.
            commutil::notifyProcessDetach();
            break;

        default:
            break;
    }
    return TRUE;  // Successful DLL_PROCESS_ATTACH.
}

#endif