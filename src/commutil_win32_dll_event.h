#ifndef __COMMUTIL_WIN32_DLL_EVENT_H__
#define __COMMUTIL_WIN32_DLL_EVENT_H__

#include "comm_util_def.h"

#ifdef COMMUTIL_WINDOWS

#include <cstdint>

namespace commutil {

#define COMMUTIL_DLL_PROCESS_ATTACH 1
#define COMMUTIL_DLL_PROCESS_DETACH 2
#define COMMUTIL_DLL_THREAD_ATTACH 3
#define COMMUTIL_DLL_THREAD_DETACH 4

typedef void (*ThreadDllEventCB)(int, void*);

class DllListener {
public:
    virtual ~DllListener() {}

    virtual void onThreadDllAttach() = 0;
    virtual void onThreadDllDetach() = 0;
    virtual void onProcessDllDetach() = 0;

protected:
    DllListener() {}
    DllListener(const DllListener&) = delete;
    DllListener(DllListener&&) = delete;
    DllListener& operator=(const DllListener&) = delete;
};

class DllPurgeFilter {
public:
    virtual ~DllPurgeFilter() {}

    virtual bool purge(ThreadDllEventCB callback, void* userData) = 0;

protected:
    DllPurgeFilter() {}
    DllPurgeFilter(const DllPurgeFilter&) = delete;
    DllPurgeFilter(DllPurgeFilter&&) = delete;
    DllPurgeFilter& operator=(const DllPurgeFilter&) = delete;
};

// listener API
extern void registerDllListener(DllListener* listener);
extern void deregisterDllListener(DllListener* listener);

// callback API
extern void registerDllCallback(ThreadDllEventCB callback, void* userData);
extern void deregisterDllCallback(ThreadDllEventCB callback);
extern void* getDllCallbackUserData(ThreadDllEventCB callback);
extern void purgeDllCallback(DllPurgeFilter* filter);

}  // namespace commutil

#endif  // COMMUTIL_WINDOWS

#endif  // __COMMUTIL_WIN32_DLL_EVENT_H__