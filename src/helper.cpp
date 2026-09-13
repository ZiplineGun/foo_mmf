#include "yamaha.hpp"
#include "protocol.hpp"
#include <array>
#include <memory>
#include <stdexcept>
#include <cstring>

static void transfer(HANDLE pipe,void* data,size_t size,bool writing) {
    auto cursor=static_cast<uint8_t*>(data);
    while(size) {
        DWORD done=0,chunk=static_cast<DWORD>((std::min)(size,size_t(65536)));
        BOOL ok=writing?WriteFile(pipe,cursor,chunk,&done,nullptr):ReadFile(pipe,cursor,chunk,&done,nullptr);
        if(!ok||!done) throw std::runtime_error("Bridge connection closed");
        cursor+=done;size-=done;
    }
}
static void send(HANDLE pipe,uint32_t command,const void* data,uint32_t bytes) {
    smaf::ipc::Header header;header.command=command;header.bytes=bytes;
    transfer(pipe,&header,sizeof(header),true);
    if(bytes) transfer(pipe,const_cast<void*>(data),bytes,true);
}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc!=3||wcscmp(argv[1],L"--pipe")) return 2;
    HANDLE pipe=CreateFileW(argv[2],GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    if(pipe==INVALID_HANDLE_VALUE) return 1000+GetLastError();
    try {
        std::unique_ptr<smaf::Yamaha> engine;
        std::wstring engineFolder;
        smaf::Engine engineKind=smaf::Engine::ma7;
        smaf::ipc::Read current;
        for(;;) {
            smaf::ipc::Header header;transfer(pipe,&header,sizeof(header),false);
            if(header.magicValue!=smaf::ipc::magic||header.protocolVersion!=smaf::ipc::version||header.bytes>smaf::ipc::maxPayload) throw std::runtime_error("Invalid bridge request");
            std::vector<uint8_t> payload(header.bytes);
            if(header.bytes) transfer(pipe,payload.data(),payload.size(),false);
            if(header.command==smaf::ipc::quit) break;
            if(header.command==smaf::ipc::open) {
                if(payload.size()<sizeof(smaf::ipc::Open)) throw std::runtime_error("Invalid open request");
                smaf::ipc::Open request;memcpy(&request,payload.data(),sizeof(request));
                if(!request.folderCharacters||request.folderCharacters>32767||!request.fileBytes||request.fileBytes>smaf::maxFileBytes||
                    uint64_t(sizeof(request))+uint64_t(request.folderCharacters)*2+request.fileBytes!=payload.size()) throw std::runtime_error("Invalid file or folder size");
                std::wstring folder(request.folderCharacters,L'\0');
                memcpy(folder.data(),payload.data()+sizeof(request),folder.size()*2);
                if(folder.find(L'\0')!=std::wstring::npos) throw std::runtime_error("Invalid folder");
                std::vector<uint8_t> file(payload.end()-request.fileBytes,payload.end());
                if(request.reverb>1||request.chorus>1||request.volume<0||request.volume>127) throw std::runtime_error("Invalid synthesis settings");
                if(request.engine!=5&&request.engine!=7) throw std::runtime_error("Invalid Yamaha engine");
                auto kind=static_cast<smaf::Engine>(request.engine);
                if(!engine||folder!=engineFolder||kind!=engineKind) {
                    auto stage=smaf::LoadStage::releaseSong;send(pipe,smaf::ipc::progress,&stage,sizeof(stage));
                    engine.reset();engine=std::make_unique<smaf::Yamaha>(folder,kind,[&](smaf::LoadStage stage){send(pipe,smaf::ipc::progress,&stage,sizeof(stage));});engineFolder=folder;engineKind=kind;
                }
                engine->open(std::move(file),request.repeats,request.fadeMs,request.volume,request.reverb!=0,request.chorus!=0);
                current={100,0,request.volume};
                smaf::ipc::OpenReply reply{engine->lengthMs(),smaf::sampleRate,2};
                send(pipe,header.command,&reply,sizeof(reply));
            } else if(header.command==smaf::ipc::read) {
                if(!engine||payload.size()!=sizeof(current)) throw std::runtime_error("Invalid read request");
                smaf::ipc::Read request;memcpy(&request,payload.data(),sizeof(request));
                if(request.tempo!=current.tempo||request.transpose!=current.transpose||request.volume!=current.volume) {
                    engine->controls(request.tempo,request.transpose,request.volume);current=request;
                }
                std::array<float,smaf::blockFrames*2> pcm;
                unsigned count=engine->read(pcm.data());
                send(pipe,header.command,pcm.data(),count*2*sizeof(float));
            } else if(header.command==smaf::ipc::seek) {
                if(!engine||payload.size()!=sizeof(smaf::ipc::Seek)) throw std::runtime_error("Invalid seek request");
                smaf::ipc::Seek request;memcpy(&request,payload.data(),sizeof(request));
                engine->seek(request.milliseconds);send(pipe,header.command,nullptr,0);
            } else if(header.command==smaf::ipc::discard) {
                if(!engine||payload.size()!=sizeof(smaf::ipc::Discard)) throw std::runtime_error("Invalid discard request");
                smaf::ipc::Discard request;memcpy(&request,payload.data(),sizeof(request));
                if(request.frames>5*smaf::sampleRate) throw std::runtime_error("Oversized discard request");
                current=request.controls;engine->controls(current.tempo,current.transpose,current.volume);
                std::array<float,smaf::blockFrames*2> pcm{};
                unsigned remaining=request.frames,offset=0,count=0;
                while(remaining) {
                    count=engine->read(pcm.data());if(!count) {offset=0;break;}
                    offset=(std::min)(remaining,count);remaining-=offset;
                }
                smaf::ipc::DiscardReply reply{count==0?1u:0u};
                std::vector<uint8_t> result(sizeof(reply)+(count-offset)*2*sizeof(float));
                memcpy(result.data(),&reply,sizeof(reply));
                if(count>offset) memcpy(result.data()+sizeof(reply),pcm.data()+offset*2,(count-offset)*2*sizeof(float));
                send(pipe,header.command,result.data(),static_cast<uint32_t>(result.size()));
            } else throw std::runtime_error("Unknown bridge command");
        }
        engine.reset();CloseHandle(pipe);return 0;
    } catch(const std::exception& e) {
        try {send(pipe,smaf::ipc::error,e.what(),static_cast<uint32_t>(strlen(e.what())));} catch(...) {}
        TerminateProcess(GetCurrentProcess(),4);return 4;
    }
}
