#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace smaf {
// MA5's multimedia scheduler falls back to timeGetTime near the end of a
// song. Freeze that clock between PCM requests so decoder/output buffering
// cannot make its worker stop sound that has not been generated yet.
// Only the loaded MMS module's named import is redirected, never a DLL file,
// another process, or the helper's IPC timeout clock.
class Ma5Clock {
    using Time=DWORD(WINAPI*)();
    inline static Ma5Clock* instance_{}; // One Yamaha instance per helper.
    Time original_{};
    void** slot_{};
    std::mutex mutex_;
    bool frozen_{};
    DWORD tick_{},offset_{};
    static DWORD WINAPI now() {
        auto& clock=*instance_;
        std::lock_guard<std::mutex> lock(clock.mutex_);
        return clock.frozen_?clock.tick_:clock.original_()+clock.offset_;
    }
    static void replace(void** slot,void* value) {
        DWORD protection=0;
        if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&protection)) throw std::runtime_error("Cannot configure the MA5 scheduler clock");
        InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot),value);
        DWORD ignored=0;VirtualProtect(slot,sizeof(*slot),protection,&ignored);
    }
public:
    explicit Ma5Clock(HMODULE module) {
        auto base=reinterpret_cast<uint8_t*>(module);
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
        const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if(directory.VirtualAddress) {
            auto entry=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);
            for(;entry->Name&&!slot_;++entry) {
                if(_stricmp(reinterpret_cast<char*>(base+entry->Name),"winmm.dll")||!entry->OriginalFirstThunk) continue;
                auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+entry->OriginalFirstThunk);
                auto addresses=reinterpret_cast<IMAGE_THUNK_DATA*>(base+entry->FirstThunk);
                for(;names->u1.AddressOfData;++names,++addresses) {
                    if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                    auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
                    if(!std::strcmp(reinterpret_cast<char*>(name->Name),"timeGetTime")) {slot_=reinterpret_cast<void**>(&addresses->u1.Function);break;}
                }
            }
        }
        if(!slot_||instance_) throw std::runtime_error("Unsupported MA5 scheduler clock import");
        original_=reinterpret_cast<Time>(*slot_);instance_=this;
        try {replace(slot_,reinterpret_cast<void*>(&now));} catch(...) {instance_=nullptr;throw;}
    }
    ~Ma5Clock() {
        try {replace(slot_,reinterpret_cast<void*>(original_));} catch(...) {}
        instance_=nullptr;
    }
    Ma5Clock(const Ma5Clock&)=delete;
    Ma5Clock& operator=(const Ma5Clock&)=delete;
    void freeze() {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!frozen_) {tick_=original_()+offset_;frozen_=true;}
    }
    void realtime() {
        // Preserve a monotonic time domain even when fast rendering has moved
        // the virtual clock ahead of wall time. Loading and seeking still let
        // the library's ordinary asynchronous operations advance normally.
        std::lock_guard<std::mutex> lock(mutex_);
        if(frozen_) {offset_=tick_-original_();frozen_=false;}
    }
    void advance(DWORD milliseconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(frozen_) tick_+=milliseconds;
    }
};
}
