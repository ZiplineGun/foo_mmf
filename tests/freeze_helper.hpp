#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <thread>
#include <atomic>
#include <set>
#include <vector>

// Fault injection from the test parent only. Release packages have no hooks.
class FreezeMa5 {
    std::atomic<bool> stop_{false};
    std::vector<HANDLE> threads_;
    std::set<DWORD> frozen_;
    std::thread thread_;
    static bool hasMa5(DWORD pid) {
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
        if(snapshot==INVALID_HANDLE_VALUE) return false;
        MODULEENTRY32W module{};module.dwSize=sizeof(module);bool found=false;
        if(Module32FirstW(snapshot,&module)) do {if(!_wcsicmp(module.szModule,L"M5_EmuSmw5.dll")) {found=true;break;}} while(Module32NextW(snapshot,&module));
        CloseHandle(snapshot);return found;
    }
    bool freeze(DWORD pid) {
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 entry{};entry.dwSize=sizeof(entry);bool any=false;
        if(Thread32First(snapshot,&entry)) do {
            if(entry.th32OwnerProcessID!=pid) continue;
            HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME,FALSE,entry.th32ThreadID);
            if(thread) {if(SuspendThread(thread)!=DWORD(-1)) {threads_.push_back(thread);any=true;} else CloseHandle(thread);}
        } while(Thread32Next(snapshot,&entry));
        CloseHandle(snapshot);return any;
    }
public:
    explicit FreezeMa5(unsigned count):thread_([this,count]{
        auto began=GetTickCount64();
        while(!stop_&&frozen_.size()<count&&GetTickCount64()-began<35000) {
            HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
            if(Process32FirstW(snapshot,&entry)) do {
                if(entry.th32ParentProcessID==GetCurrentProcessId()&&!_wcsicmp(entry.szExeFile,L"smaf_helper.exe")&&!frozen_.count(entry.th32ProcessID)&&hasMa5(entry.th32ProcessID)&&freeze(entry.th32ProcessID)) frozen_.insert(entry.th32ProcessID);
                if(frozen_.size()>=count) break;
            } while(Process32NextW(snapshot,&entry));
            CloseHandle(snapshot);Sleep(2);
        }
    }) {}
    void finish(){stop_=true;if(thread_.joinable()) thread_.join();}
    const std::set<DWORD>& pids(){finish();return frozen_;}
    ~FreezeMa5(){finish();for(HANDLE thread:threads_){ResumeThread(thread);CloseHandle(thread);}}
};
inline bool processExited(DWORD pid) {
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!process) return GetLastError()==ERROR_INVALID_PARAMETER;
    bool ended=WaitForSingleObject(process,1000)==WAIT_OBJECT_0;CloseHandle(process);return ended;
}
