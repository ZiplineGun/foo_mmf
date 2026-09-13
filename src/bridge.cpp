#include "bridge.hpp"
#include <objbase.h>
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace smaf {
static std::runtime_error failure(const char* what) {return std::runtime_error(std::string(what)+" (Windows error "+std::to_string(GetLastError())+")");}
void Bridge::close() noexcept {
    if(pipe_!=INVALID_HANDLE_VALUE) {CancelIoEx(pipe_,nullptr);CloseHandle(pipe_);pipe_=INVALID_HANDLE_VALUE;}
    if(job_) {CloseHandle(job_);job_=nullptr;}
    if(process_) {WaitForSingleObject(process_,100);CloseHandle(process_);process_=nullptr;}
}
Bridge::~Bridge(){close();}
void Bridge::beginRequest(bool opening,LoadStage stage) {
    opening_=opening;stage_=stage;requestStarted_=progressAt_=GetTickCount64();
}
void Bridge::checkDeadline(const Check& check) {
    check(); // Cancellation always takes precedence over timeout/recovery.
    auto now=GetTickCount64();
    if(now-requestStarted_>=requestLimitMs||(opening_&&engine_==Engine::ma5&&now-progressAt_>=ma5LoadStallMs)) {
        std::string detail=opening_?stageName(stage_):"processing audio request";
        throw HelperTimeout("Yamaha helper timed out while "+detail+" (PID "+std::to_string(processId())+", elapsed "+std::to_string(now-requestStarted_)+" ms)");
    }
}
void Bridge::wait(OVERLAPPED& operation,const Check& check) {
    try {
        for(;;) {
            checkDeadline(check);
            if(WaitForSingleObject(operation.hEvent,20)==WAIT_OBJECT_0) return;
            if(process_&&WaitForSingleObject(process_,0)==WAIT_OBJECT_0) {DWORD exitCode=0;GetExitCodeProcess(process_,&exitCode);throw std::runtime_error("The Yamaha helper exited unexpectedly (exit code "+std::to_string(exitCode)+")");}
        }
    } catch(...) {CancelIoEx(pipe_,&operation);WaitForSingleObject(operation.hEvent,INFINITE);throw;}
}
void Bridge::transfer(void* data,size_t bytes,bool writing,const Check& check) {
    auto cursor=static_cast<uint8_t*>(data);
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!event) throw failure("Cannot create bridge event");
    try {
        while(bytes) {
            checkDeadline(check);ResetEvent(event);OVERLAPPED operation{};operation.hEvent=event;
            DWORD done=0,chunk=static_cast<DWORD>(std::min(bytes,size_t(65536)));
            BOOL ok=writing?WriteFile(pipe_,cursor,chunk,&done,&operation):ReadFile(pipe_,cursor,chunk,&done,&operation);
            if(!ok) {
                if(GetLastError()!=ERROR_IO_PENDING) throw failure("Yamaha bridge I/O failed");
                wait(operation,check);
                if(!GetOverlappedResult(pipe_,&operation,&done,FALSE)) throw failure("Yamaha bridge I/O failed");
            }
            if(!done) throw std::runtime_error("The Yamaha helper closed its connection");
            cursor+=done;bytes-=done;
        }
    } catch(...) {CloseHandle(event);throw;}
    CloseHandle(event);
}
std::vector<uint8_t> Bridge::transact(uint32_t command,const void* data,uint32_t bytes,const Check& check) {
    beginRequest(command==ipc::open,LoadStage::sendingFile);
    ipc::Header request;request.command=command;request.bytes=bytes;
    transfer(&request,sizeof(request),true,check);
    if(bytes) transfer(const_cast<void*>(data),bytes,true,check);
    unsigned reports=0;
    for(;;) {
        ipc::Header reply;transfer(&reply,sizeof(reply),false,check);
        if(reply.magicValue!=ipc::magic||reply.protocolVersion!=ipc::version||reply.bytes>65536) throw std::runtime_error("Invalid Yamaha bridge response");
        std::vector<uint8_t> result(reply.bytes);
        if(reply.bytes) transfer(result.data(),result.size(),false,check);
        if(reply.command==ipc::progress) {
            if(!opening_||result.size()!=sizeof(LoadStage)||++reports>32) throw std::runtime_error("Invalid Yamaha loading progress");
            LoadStage stage;memcpy(&stage,result.data(),sizeof(stage));
            if(stage<LoadStage::libraries||stage>=LoadStage::count) throw std::runtime_error("Unknown Yamaha loading stage");
            if(stage!=stage_) {stage_=stage;progressAt_=GetTickCount64();}
            continue;
        }
        if(reply.command==ipc::error) throw std::runtime_error(std::string(result.begin(),result.end()));
        if(reply.command!=command) throw std::runtime_error("Unexpected Yamaha bridge response");
        return result;
    }
}
// The 2004 MA5 MMS DLL rejects Windows major versions above 5. Apply only
// Microsoft's version-reporting fix to this child; never change foobar's environment.
static std::vector<wchar_t> ma5Environment() {
    wchar_t* environment=GetEnvironmentStringsW();
    if(!environment) throw failure("Cannot read helper environment");
    std::vector<std::wstring> entries;
    try {
        for(auto p=environment;*p;p+=wcslen(p)+1)
            if(_wcsnicmp(p,L"__COMPAT_LAYER=",15)) entries.emplace_back(p);
    } catch(...) {FreeEnvironmentStringsW(environment);throw;}
    FreeEnvironmentStringsW(environment);
    entries.emplace_back(L"__COMPAT_LAYER=WinXPSP2VersionLie");
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    std::vector<wchar_t> result;
    for(const auto& entry:entries) {result.insert(result.end(),entry.begin(),entry.end());result.push_back(0);}
    result.push_back(0);return result;
}
Bridge::Bridge(const std::wstring& executable,const std::wstring& folder,const std::vector<uint8_t>& data,unsigned repeats,unsigned fadeMs,const Check& check,int volume,bool reverb,Engine engine,bool chorus):engine_(engine) {
    try {
        beginRequest(true,LoadStage::connecting);
        if(folder.empty()||folder.size()>32767||data.empty()||data.size()>64*1024*1024) throw std::runtime_error("Invalid Yamaha input or folder");
        GUID guid;if(FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Cannot create bridge identifier");
        wchar_t text[40];StringFromGUID2(guid,text,40);
        const std::wstring name=std::wstring(L"\\\\.\\pipe\\foo_mmf-")+text;
        pipe_=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,nullptr);
        if(pipe_==INVALID_HANDLE_VALUE) throw failure("Cannot create Yamaha bridge pipe");
        job_=CreateJobObjectW(nullptr,nullptr);if(!job_) throw failure("Cannot create Yamaha helper job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limit,sizeof(limit))) throw failure("Cannot configure Yamaha helper job");
        std::wstring command=L"\""+executable+L"\" --pipe \""+name+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        auto environment=engine==Engine::ma5?ma5Environment():std::vector<wchar_t>();
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,environment.empty()?nullptr:environment.data(),nullptr,&startup,&process)) throw failure("Cannot start smaf_helper.exe");
        process_=process.hProcess;
        if(!AssignProcessToJobObject(job_,process_)) {TerminateProcess(process_,1);CloseHandle(process.hThread);throw failure("Cannot isolate Yamaha helper");}
        ResumeThread(process.hThread);CloseHandle(process.hThread);
        OVERLAPPED connection{};connection.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!connection.hEvent) throw failure("Cannot create connection event");
        try {
            if(!ConnectNamedPipe(pipe_,&connection)) {
                DWORD error=GetLastError();
                if(error==ERROR_IO_PENDING) {wait(connection,check);DWORD ignored;if(!GetOverlappedResult(pipe_,&connection,&ignored,FALSE)) throw failure("Cannot connect to Yamaha helper");}
                else if(error!=ERROR_PIPE_CONNECTED) throw failure("Cannot connect to Yamaha helper");
            }
        } catch(...) {CloseHandle(connection.hEvent);throw;}
        CloseHandle(connection.hEvent);
        open(folder,data,repeats,fadeMs,check,volume,reverb,chorus);
    } catch(...) {close();throw;}
}
void Bridge::open(const std::wstring& folder,const std::vector<uint8_t>& data,unsigned repeats,unsigned fadeMs,const Check& check,int volume,bool reverb,bool chorus) {
    try {
        if(folder.empty()||folder.size()>32767||data.empty()||data.size()>64*1024*1024) throw std::runtime_error("Invalid Yamaha input or folder");
        ipc::Open request{static_cast<uint32_t>(data.size()),static_cast<uint32_t>(folder.size()),repeats,fadeMs,volume,reverb?1u:0u,static_cast<uint32_t>(engine_),chorus?1u:0u};
        std::vector<uint8_t> payload(sizeof(request)+folder.size()*2+data.size());
        memcpy(payload.data(),&request,sizeof(request));memcpy(payload.data()+sizeof(request),folder.data(),folder.size()*2);
        memcpy(payload.data()+sizeof(request)+folder.size()*2,data.data(),data.size());
        auto result=transact(ipc::open,payload.data(),static_cast<uint32_t>(payload.size()),check);
        if(result.size()!=sizeof(ipc::OpenReply)) throw std::runtime_error("Invalid Yamaha open response");
        ipc::OpenReply info;memcpy(&info,result.data(),sizeof(info));
        if(info.sampleRate!=44100||info.channels!=2||!info.lengthMs) throw std::runtime_error("Unsupported Yamaha audio format");
        length_=info.lengthMs;
        folder_=folder;
    } catch(...) {close();throw;}
}
std::vector<float> Bridge::read(int tempo,int transpose,const Check& check,int volume) {
    ipc::Read request{tempo,transpose,volume};
    try {
    auto bytes=transact(ipc::read,&request,sizeof(request),check);
    if(bytes.size()>441*2*sizeof(float)||bytes.size()%(2*sizeof(float))) throw std::runtime_error("Invalid Yamaha audio block");
    std::vector<float> result(bytes.size()/sizeof(float));
    if(!bytes.empty()) memcpy(result.data(),bytes.data(),bytes.size());
    return result;
    } catch(...) {close();throw;}
}
void Bridge::seek(uint64_t milliseconds,const Check& check) {
    ipc::Seek request{milliseconds};
    try {if(!transact(ipc::seek,&request,sizeof(request),check).empty()) throw std::runtime_error("Invalid seek reply");}
    catch(...) {close();throw;}
}
std::vector<float> Bridge::discard(unsigned frames,const Check& check,int volume,bool& eof) {
    ipc::Discard request{frames,{100,0,volume}};
    try {
        auto bytes=transact(ipc::discard,&request,sizeof(request),check);
        if(bytes.size()<sizeof(ipc::DiscardReply)||bytes.size()>sizeof(ipc::DiscardReply)+441*2*sizeof(float)||(bytes.size()-sizeof(ipc::DiscardReply))%(2*sizeof(float))) throw std::runtime_error("Invalid discard reply");
        ipc::DiscardReply reply;memcpy(&reply,bytes.data(),sizeof(reply));
        if(reply.eof>1) throw std::runtime_error("Invalid discard status");eof=reply.eof!=0;
        std::vector<float> result((bytes.size()-sizeof(reply))/sizeof(float));
        if(!result.empty()) memcpy(result.data(),bytes.data()+sizeof(reply),result.size()*sizeof(float));
        return result;
    } catch(...) {close();throw;}
}
}
