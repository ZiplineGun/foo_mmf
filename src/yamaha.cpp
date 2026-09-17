#include "yamaha.hpp"
#include "midi.hpp"
#include "smaf_audio.hpp"
#include "ma5_clock.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace smaf {
static int checked(const char* name,int value) {
    if(value < 0) throw std::runtime_error(std::string("Yamaha ")+name+" failed ("+std::to_string(value)+")");
    return value;
}
template<typename T> T Yamaha::api(const char* name) const {
    auto result=GetProcAddress(module_,name);
    if(!result) throw std::runtime_error(std::string("Missing Yamaha export: ")+name);
    return reinterpret_cast<T>(result);
}
template<typename T> T Yamaha::mmsApi(const char* name) const {
    auto result=GetProcAddress(mms_,name);
    if(!result) throw std::runtime_error(std::string("Missing Yamaha export: ")+name);
    return reinterpret_cast<T>(result);
}
template<typename T> T Yamaha::melodyApi(const char* name) const {
    return engine_==Engine::ma5?mmsApi<T>((std::string("SmafMms_")+name).c_str()):api<T>((std::string("Mapi_Melody_")+name).c_str());
}
Yamaha::Yamaha(const std::wstring& directory,Engine engine,Progress progress,unsigned rate):engine_(engine),rate_(rate),frames_(rate/100),progress_(std::move(progress)) {
    if(rate!=44100&&rate!=48000) throw std::runtime_error("Unsupported Yamaha sample rate");
    this->progress(LoadStage::libraries);
    static_assert(sizeof(void*)==4,"Yamaha is an x86 engine");
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_USER_DIRS);
    directoryCookie_=AddDllDirectory(directory.c_str());
    if(!directoryCookie_) throw std::runtime_error("Cannot use the configured Yamaha DLL folder");
    module_=LoadLibraryExW((directory+L"\\"+smwName(engine_)).c_str(),nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module_) throw std::runtime_error("Cannot load the selected Yamaha synthesis DLL or its dependencies (Windows error "+std::to_string(GetLastError())+")");
    mms_=LoadLibraryExW((directory+L"\\"+mmsName(engine_)).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    hardware_=LoadLibraryExW((directory+L"\\"+hardwareName(engine_)).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!mms_||!hardware_) throw std::runtime_error("Cannot load the selected Yamaha MMS/hardware DLLs or their dependencies");
    if(engine_==Engine::ma5) ma5Clock_=std::make_unique<Ma5Clock>(mms_);
    initialize();
}
void Yamaha::initialize() {
    progress(LoadStage::initialize);
    // 72-byte offline emulator configuration.
    std::array<uint32_t,18> config{};
    config[0]=2; config[1]=rate_; config[2]=0; config[3]=0;
    config[4]=15; config[5]=config[6]=1;
    const uint16_t dimensions[]={120,120,120,120,480,640,480,640,12,12,12,12,16,16,24,24};
    std::copy(std::begin(dimensions),std::end(dimensions),reinterpret_cast<uint16_t*>(&config[7]));
    config[16]=0xddd; config[17]=1;
    if(engine_==Engine::ma5) {
        config[0]=1;config[16]=config[17]=0;
        checked("MA5 SystemStart",mmsApi<int(__cdecl*)(void*,void*,void*,void*)>("winSmafMms_SystemStart")(config.data(),nullptr,nullptr,nullptr));emulator_=true;
        checked("MA5 Initialize",mmsApi<Call>("SmafMms_Initialize")());initialized_=true;
        auto device=mmsApi<int(__cdecl*)(unsigned,unsigned,unsigned,unsigned)>("SmafMms_DeviceControl");
        checked("MA5 Reset",device(13,0,0,0));
        checked("MA5 Output",device(2,31,0,0));checked("MA5 Output",device(3,0,31,31));checked("MA5 Output",device(4,31,0,0));
        checked("MA5 Route",device(5,2,0,0));checked("MA5 Gain",device(6,0,0,0));checked("MA5 Route",device(8,2,0,0));checked("MA5 Gain",device(9,0,0,0));
        return;
    }
    using Init=int(__cdecl*)(int,void*,void*,void*,void*,void*);
    checked("EmuInitialize",api<Init>("Mapi_EmuInitialize")(0,config.data(),nullptr,nullptr,nullptr,nullptr));
    emulator_=true;
    checked("Initialize",api<Call>("Mapi_Initialize")()); initialized_=true;
    resetDevice();
    checked("SetMode",api<One>("Mapi_SetMode")(1));
    control_=api<Control>("Mapi_Melody_Control");
    generate_=api<int(__cdecl*)(void*,unsigned)>("Mapi_EmuGenerate");
}
void Yamaha::resetDevice() {
    auto device=api<int(__cdecl*)(unsigned,unsigned,void*)>("Mapi_DeviceControlEx");
    // Required hardware reset: without it the digital master gain remains zero.
    checked("ResetDevice",device(0x10000,0,nullptr));
    uint32_t gain[3]={0,2,0};
    for(unsigned output:{0u,2u,1u}) checked("OutputGain",device(0x10010,output,gain));
    unsigned route=2, analogGain=0;
    for(unsigned output:{0u,1u}) {
        checked("OutputRoute",device(0x10018,output,&route));
        checked("AnalogGain",device(0x1001a,output,&analogGain));
    }
}
void Yamaha::open(std::vector<uint8_t> data,unsigned repeats,unsigned fadeMs,int volume,bool enableReverb,bool enableChorus) {
    bool reset=handle_!=0;
    progress(LoadStage::releaseSong);
    closeSong();
    if(reset) {
        progress(LoadStage::resetEngine);
        terminate();
        initialize(); // Retain the process and loaded DLLs; clear all chip/effect state.
    }
    if(data.empty()||data.size()>maxFileBytes||repeats>254||fadeMs>30000) throw std::runtime_error("Invalid song or playback settings");
    data_=enableReverb&&enableChorus?std::move(data):midiEffects(data,enableReverb,enableChorus); repeats_=repeats; repeatsLeft_=repeats; fadeMs_=fadeMs;
    if(engine_==Engine::ma5) data_=audioOnlySmaf(std::move(data_));
    tailFrames_=quietFrames_=ma5PollBlocks_=0;ma5Position_=0;renderedTenthsMs_=0;lastPosition_=-1; ending_=finished_=false;
    using Load=int(__cdecl*)(const void*,unsigned,unsigned,unsigned);
    progress(LoadStage::load);
    handle_=engine_==Engine::ma5?
        checked("MA5 Load",mmsApi<int(__cdecl*)(const void*,unsigned,unsigned)>("SmafMms_Load")(data_.data(),static_cast<unsigned>(data_.size()),1)):
        checked("Load",api<Load>("Mapi_Melody_Load")(data_.data(),static_cast<unsigned>(data_.size()),0,1));
    if(!handle_) throw std::runtime_error("Yamaha returned an invalid song handle");
    uint32_t args[6]{};
    progress(LoadStage::open);
    melodyApi<void(__cdecl*)(void*)>("OpenArgInit")(args);
    checked("Open",melodyApi<int(__cdecl*)(int,void*)>("Open")(handle_,args)); opened_=true;
    int loopCount=1; // Finite embedded loops; extra full-song repeats are handled below.
    if(engine_==Engine::ma7) checked("RepeatCount",songControl(0x11110034,&loopCount));
    progress(LoadStage::standby);
    checked("Standby",melodyApi<int(__cdecl*)(int,unsigned,int)>("Standby")(handle_,engine_==Engine::ma5?0x80000001u:0,0));
    progress(LoadStage::ready);
    checked("WaitReady",melodyApi<int(__cdecl*)(int,int)>("WaitReady")(handle_,-1));
    if(engine_==Engine::ma5) checked("MA5 RepeatCount",songControl(0x11110034,&loopCount));
    progress(LoadStage::length);
    length_=checked("Length",songControl(0x10100005,nullptr));
    if(!length_||length_>24*60*60*1000) throw std::runtime_error("Invalid Yamaha song length");
    progress(LoadStage::controls);controls(100,0,volume);
    progress(LoadStage::start);
    checked("Start",melodyApi<One>("Start")(handle_)); started_=true;
    if(ma5Clock_) ma5Clock_->freeze();
}
int Yamaha::songControl(unsigned command,void* value) {
    if(engine_!=Engine::ma5) return control_(handle_,command,value);
    unsigned mapped=command;
    switch(command) {
    case 0x11110029: mapped=0x10110001;break;
    case 0x11110030: mapped=0x10010002;break;
    case 0x1111002c: mapped=0x10010000;break;
    case 0x1111002b: mapped=0x10100004;break;
    case 0x11110034: mapped=0x1011001b;break;
    }
    return mmsApi<int(__cdecl*)(int,unsigned,int)>("SmafMms_Control")(handle_,mapped,value?*static_cast<int*>(value):0);
}
void Yamaha::controls(int tempo,int transpose,int volume) {
    if(tempo<70||tempo>130||transpose < -12||transpose>12) throw std::runtime_error("Tempo or transposition out of range");
    checked("Tempo",songControl(0x11110029,&tempo));
    checked("Transpose",songControl(0x11110030,&transpose));
    if(volume<0||volume>127) throw std::runtime_error("Yamaha volume out of range");
    checked("Volume",songControl(0x1111002c,&volume));
    tempo_=tempo; transpose_=transpose; volume_=volume;
}
void Yamaha::seek(uint64_t milliseconds) {
    if(ma5Clock_) ma5Clock_->realtime();
    heldTail_.clear();readyAudio_.clear();
    if(milliseconds>=uint64_t(length_)*(repeats_+1)) {finished_=true;return;}
    int stopped=melodyApi<One>("Stop")(handle_);
    if(stopped!=-8) checked("SeekStop",stopped);started_=false;
    repeatsLeft_=repeats_-static_cast<unsigned>(std::min(uint64_t(repeats_),milliseconds/length_));
    const unsigned position=static_cast<unsigned>(milliseconds%length_);
    checked("Seek",melodyApi<int(__cdecl*)(int,unsigned)>("Seek")(handle_,position));
    checked("SeekReady",melodyApi<int(__cdecl*)(int,int)>("WaitReady")(handle_,-1));
    controls(tempo_,transpose_,volume_);
    checked("SeekStart",melodyApi<One>("Start")(handle_)); started_=true;
    if(ma5Clock_) ma5Clock_->freeze();
    ma5Position_=position;renderedTenthsMs_=uint64_t(position)*10;ma5PollBlocks_=0;lastPosition_=-1;
    ending_=finished_=false;tailFrames_=quietFrames_=0;
}
int Yamaha::positionMs() {return checked("Position",songControl(0x1111002b,nullptr));}
unsigned Yamaha::read(float* interleaved) {
    for(;;) {
        if(!readyAudio_.empty()) {
            auto block=std::move(readyAudio_.front());readyAudio_.pop_front();
            std::copy(block.begin(),block.end(),interleaved);return static_cast<unsigned>(block.size()/2);
        }
        if(finished_) {heldTail_.clear();return 0;}
        unsigned count=render(interleaved);
        if(!count) return 0;
        if(!quietBlock_&&heldTail_.empty()) return count;
        std::vector<float> block(interleaved,interleaved+count*2);
        if(quietBlock_) {
            // Confirm a settled tail before discarding it. If sound returns,
            // replay the held silence so delayed effects keep their timing.
            heldTail_.push_back(std::move(block));
            continue;
        }
        readyAudio_.swap(heldTail_);readyAudio_.push_back(std::move(block));
    }
}
unsigned Yamaha::render(float* interleaved) {
    if(finished_) return 0;
    quietBlock_=false;
    std::array<int32_t,maxBlockFrames> left{},right{};
    int32_t* planes[8]={left.data(),right.data()};
    if(engine_==Engine::ma5) {
        std::array<int16_t,maxBlockFrames> l{},r{};
        api<void(__cdecl*)(int16_t*,int16_t*,unsigned)>("MaSound_Generate")(l.data(),r.data(),frames_);
        std::copy(l.begin(),l.end(),left.begin());std::copy(r.begin(),r.end(),right.begin());
    } else checked("Generate",generate_(planes,frames_));
    if(ma5Clock_) ma5Clock_->advance(10);
    // Every block is 10 ms at either rate. Integrate musical time independently
    // of Position, which can stop short or advance on wall time during offline
    // rendering even though Generate keeps supplying valid PCM.
    if(!ending_) renderedTenthsMs_+=tempo_;
    int position;
    if(engine_==Engine::ma5) {
        // The 2004 MMS dispatcher polls a worker thread. Querying it for every
        // 10-ms audio block is slower than real time. Interpolate between polls;
        // always confirm the actual position near EOF before ending/repeating.
        if(!ending_) {
            ma5Position_+=double(frames_)*1000/rate_*tempo_/100;
            if(++ma5PollBlocks_>=10||ma5Position_>=length_-1||renderedTenthsMs_>=uint64_t(length_)*10) {ma5Position_=positionMs();ma5PollBlocks_=0;}
        }
        position=static_cast<int>(ma5Position_);
    } else position=positionMs();
    // MA5 saturates one millisecond short. MA7 can stop up to one 10-ms block
    // short on MLD files while Generate keeps returning PCM forever. Confirm
    // that MA7's position has stopped advancing before allowing that tolerance;
    // audible release tails still pass through the normal end handling below.
    const bool stoppedNearEnd=engine_==Engine::ma7&&position==lastPosition_&&position>=static_cast<int>(length_)-10;
    lastPosition_=position;
    if(position>=static_cast<int>(length_)-(engine_==Engine::ma5?1:0)||stoppedNearEnd) ending_=true;
    // This only starts normal tail handling after the declared musical length;
    // it neither stops the engine nor discards notes and audible release tails.
    if(renderedTenthsMs_>=uint64_t(length_)*10) ending_=true;
    int peak=0;
    for(unsigned i=0;i<frames_;++i) {
        const int l=std::clamp(left[i],-32768,32767), r=std::clamp(right[i],-32768,32767);
        peak=std::max({peak,std::abs(l),std::abs(r)});
        float gain=1.0f;
        if(repeats_&&!repeatsLeft_&&fadeMs_) {
            const int64_t fadePosition=std::max(int64_t(position),int64_t(renderedTenthsMs_/10));
            gain=ending_?0.0f:std::clamp(float(int64_t(length_)-fadePosition)/fadeMs_,0.0f,1.0f);
        }
        interleaved[i*2]=l/32768.0f*gain; interleaved[i*2+1]=r/32768.0f*gain;
    }
    if(ending_) {
        if(repeatsLeft_) {
            if(ma5Clock_) ma5Clock_->realtime();
            int stopped=melodyApi<One>("Stop")(handle_);
            if(stopped!=-8) checked("RepeatStop",stopped);
            checked("RepeatSeek",melodyApi<int(__cdecl*)(int,int)>("Seek")(handle_,0));
            checked("RepeatReady",melodyApi<int(__cdecl*)(int,int)>("WaitReady")(handle_,-1));
            controls(tempo_,transpose_,volume_);
            checked("RepeatStart",melodyApi<One>("Start")(handle_));
            if(ma5Clock_) ma5Clock_->freeze();
            ma5Position_=0;renderedTenthsMs_=0;ma5PollBlocks_=0;lastPosition_=-1;
            --repeatsLeft_; ending_=false; return frames_;
        }
        tailFrames_+=frames_;
        // The emulator can retain a small constant DC offset after its reverb ends.
        // Detect a settled tail without altering the rendered Yamaha samples.
        const auto leftRange=std::minmax_element(left.begin(),left.begin()+frames_);
        const auto rightRange=std::minmax_element(right.begin(),right.begin()+frames_);
        const bool quiet=peak<=64&&*leftRange.second-*leftRange.first<=2&&*rightRange.second-*rightRange.first<=2;
        quietBlock_=quiet&&!(repeats_&&fadeMs_);
        quietFrames_=quiet ? quietFrames_+frames_ : 0;
        finished_=quietFrames_>=rate_/4 || tailFrames_>=rate_*10;
        if(repeats_&&fadeMs_) finished_=true;
    }
    return frames_;
}
void Yamaha::closeSong() {
    if(ma5Clock_) ma5Clock_->realtime();
    heldTail_.clear();readyAudio_.clear();
    if(started_) {int result=melodyApi<One>("Stop")(handle_);if(result!=-8) checked("Stop",result);started_=false;}
    if(opened_) {checked("Close",melodyApi<One>("Close")(handle_));opened_=false;}
    if(handle_) {checked("Unload",melodyApi<One>("Unload")(handle_));handle_=0;}
    data_.clear();
}
void Yamaha::terminate() {
    if(engine_==Engine::ma5) {
        if(emulator_) mmsApi<void(__cdecl*)()>("winSmafMms_SystemStop")();
    } else {
        if(initialized_) checked("Terminate",api<Call>("Mapi_Terminate")());
        if(emulator_) checked("EmuTerminate",api<Call>("Mapi_EmuTerminate")());
    }
    initialized_=emulator_=false;
}
void Yamaha::close() noexcept {
    if(ma5Clock_) ma5Clock_->realtime();
    try {
        if(started_) melodyApi<One>("Stop")(handle_);
        if(opened_) melodyApi<One>("Close")(handle_);
        if(handle_) melodyApi<One>("Unload")(handle_);
        terminate();
    } catch(...) {}
    ma5Clock_.reset();
    if(hardware_) FreeLibrary(hardware_);
    if(mms_) FreeLibrary(mms_);
    if(module_) FreeLibrary(module_);
    if(directoryCookie_) RemoveDllDirectory(directoryCookie_);
}
Yamaha::~Yamaha(){close();}
std::vector<uint8_t> readFile(const wchar_t* path) {
    FILE* file{};
    if(_wfopen_s(&file,path,L"rb")||!file) throw std::runtime_error("Cannot open input file");
    std::fseek(file,0,SEEK_END); auto size=std::ftell(file); std::rewind(file);
    if(size<=0||size>maxFileBytes) {std::fclose(file);throw std::runtime_error("Invalid input size");}
    std::vector<uint8_t> data(size);
    auto count=std::fread(data.data(),1,data.size(),file);std::fclose(file);
    if(count!=data.size()) throw std::runtime_error("Cannot read input file");
    return data;
}
}
