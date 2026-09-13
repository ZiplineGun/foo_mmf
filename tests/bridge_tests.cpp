#include "../src/bridge.hpp"
#include "../src/midi.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

static void require(bool value,const char* message){if(!value) throw std::runtime_error(message);}
static double milliseconds(){LARGE_INTEGER counter,frequency;QueryPerformanceCounter(&counter);QueryPerformanceFrequency(&frequency);return double(counter.QuadPart)*1000/frequency.QuadPart;}
static std::vector<uint8_t> readFile(const std::wstring& path) {
    FILE* file{};_wfopen_s(&file,path.c_str(),L"rb");require(file!=nullptr,"Cannot read fixture");
    fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);require(size>0,"Empty fixture");
    std::vector<uint8_t> bytes(size);auto count=fread(bytes.data(),1,bytes.size(),file);fclose(file);require(count==bytes.size(),"Short fixture read");return bytes;
}
template<typename F> static void fails(F fn,const char* label){bool failed=false;try{fn();}catch(const std::exception& e){failed=true;std::printf("Expected failure (%s): %s\n",label,e.what());}require(failed,label);}
static std::vector<uint8_t> midi(const std::vector<uint8_t>& track) {
    std::vector<uint8_t> result={'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k'};
    for(int shift=24;shift>=0;shift-=8) result.push_back(uint8_t(track.size()>>shift));
    result.insert(result.end(),track.begin(),track.end());return result;
}
static std::vector<float> render(smaf::Bridge& bridge,int volume=64) {
    std::vector<float> result;
    for(unsigned n=0;n<15000;++n) {auto block=bridge.read(100,0,[]{},volume);if(block.empty()) return result;result.insert(result.end(),block.begin(),block.end());}
    throw std::runtime_error("Test render did not reach EOF");
}
static void testMa5(const std::wstring& helper,const std::wstring& folder,const std::wstring& ma7,const std::wstring& fixtures) {
    auto check=[]{};auto melody=readFile(fixtures+L"\\Melody11.mmf");
    std::wprintf(L"Testing MA5 libraries: %ls\n",folder.c_str());fflush(stdout);
    smaf::Bridge bridge(helper,folder,melody,0,5000,check,64,true,smaf::Engine::ma5);
    DWORD pid=bridge.processId();double begin=milliseconds();auto pcm=render(bridge);
    require(pcm.size()>46*88200&&pcm.size()<50*88200,"MA5 Melody11 EOF is incorrect");
    require(milliseconds()-begin<pcm.size()/88.2,"MA5 rendering is slower than real time");
    smaf::Bridge seven(helper,ma7,melody,0,5000,check);
    require(pcm!=render(seven),"MA5 Melody11 unexpectedly matches MA7");
    bridge.open(folder,melody,0,5000,check);
    bool eof=false;auto remainder=bridge.discard(54419,check,64,eof);
    require(!eof&&std::equal(remainder.begin(),remainder.end(),pcm.begin()+54419*2),"MA5 exact reconstruction differs");
    bridge.seek(44000,check);require(!bridge.read(70,-12,check).empty(),"MA5 native seek/low controls failed");
    require(!bridge.read(130,12,check,32).empty(),"MA5 high controls failed");
    bridge.seek(999999,check);require(bridge.read(100,0,check).empty(),"MA5 seek past EOF failed");
    bridge.seek(0,check);require(!bridge.read(100,0,check).empty(),"MA5 seek from EOF failed");
    for(auto name:{L"MarsMine.mmf",L"Cat dance.mmf",L"GuitarMan.mmf",L"Melody11.mmf"}) {
        auto file=readFile(fixtures+L"\\"+name);bridge.open(folder,file,0,5000,check);
        smaf::Bridge fresh(helper,folder,file,0,5000,check,64,true,smaf::Engine::ma5);
        float peak=0;
        for(unsigned n=0;n<100;++n) {
            auto block=bridge.read(100,0,check);require(block==fresh.read(100,0,check),"MA5 warm/fresh PCM differs");
            for(float sample:block) {require(std::isfinite(sample)&&std::abs(sample)<=1,"Invalid MA5 PCM");peak=std::max(peak,std::abs(sample));}
        }
        require(peak>0.001f,"MA5 fixture is silent");
    }
    require(bridge.processId()==pid,"MA5 file switching restarted the helper");
    bridge.open(folder,melody,0,5000,check,0);
    for(unsigned n=0;n<100;++n) for(float sample:bridge.read(100,0,check,0)) require(std::abs(sample)<0.002f,"MA5 zero volume is not silent");
    bridge.open(folder,readFile(fixtures+L"\\MarsMine.mmf"),1,5000,check);
    auto repeated=render(bridge);require(repeated.size()>55*88200&&repeated.size()<58*88200,"MA5 repeat duration is incorrect");
    for(auto name:{L"930ca_11.mmf",L"4 Dragon Quest 8 Overture.mld"}) fails([&]{
        smaf::Bridge unsupported(helper,folder,readFile(fixtures+L"\\"+name),0,5000,check,64,true,smaf::Engine::ma5);
    },"MA5 unsupported file");
    fails([&]{smaf::Bridge unsupported(helper,folder,midi({0,0x90,69,100,96,0x80,69,0,0,0xff,0x2f,0}),0,5000,check,64,true,smaf::Engine::ma5);},"MA5 MIDI unsupported");
    fails([&]{bridge.read(100,0,[]{throw std::runtime_error("cancelled MA5");});},"MA5 cancellation");
    require(!bridge.healthy(),"Cancelled MA5 helper was retained");
    std::printf("PASS MA5 PCM, engine difference, warm reuse, controls, exact/native seek, repeats/EOF, unsupported formats and cancellation\n");fflush(stdout);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=4&&argc!=6) {std::puts("Usage: bridge_tests helper-exe MA7-folder fixture-folder [older-MA5-folder newer-MA5-folder]");return 2;}
    try {
        auto noAbort=[]{};
        const std::wstring helper=argv[1],folder=argv[2],fixtures=argv[3];
        auto song=readFile(fixtures+L"\\MarsMine.mmf");
        {
            // Running status, reverb changes, reset controllers and GM reset SysEx.
            auto file=midi({0,0xb0,91,127,0,7,100,0,121,0,0,0xf0,5,0x7e,0x7f,9,1,0xf7,0,0x90,69,100,0x87,0x40,0x80,69,0,0,0xff,0x2f,0});
            auto original=file;auto dryFile=smaf::midiEffects(file,false,true);
            require(file==original&&dryFile!=file,"Dry MIDI processing modified source or did nothing");
            smaf::Bridge bridge(helper,folder,file,0,5000,noAbort,64,true);auto wet=render(bridge);
            bridge.open(folder,file,0,5000,noAbort,64,false);auto dry=render(bridge);
            require(dry!=wet&&dry.size()<wet.size(),"MIDI reverb toggle ineffective");
            bridge.open(folder,file,0,5000,noAbort,64,true);require(render(bridge)==wet,"Dry MIDI state leaked into next wet song");
            bridge.open(folder,file,0,5000,noAbort,0,true);auto silent=render(bridge,0);
            for(float sample:silent) require(std::abs(sample)<0.002f,"Yamaha volume zero is not silent");
            std::printf("PASS MIDI resets/running status, reverb wet %.3f s / dry %.3f s, zero volume\n",wet.size()/88200.0,dry.size()/88200.0);
            fails([&]{smaf::midiEffects(midi({0,0x90,69}),false,true);},"truncated MIDI event");
            fails([&]{smaf::midiEffects(midi({0,69,100}),false,true);},"invalid MIDI running status");
            fails([&]{smaf::midiEffects(midi({0,0xff,1,0x81,0x81,0x81,0x81,0}),false,true);},"oversized MIDI VLQ");
        }
        {
            auto effectFile=[](uint8_t reverb,uint8_t chorus){return midi({
                0,0xb0,91,reverb,0,93,chorus,0,121,0,0,0xf0,5,0x7e,0x7f,9,1,0xf7,
                0,0xb0,91,reverb,0,93,chorus,0,0x90,69,100,
                96,0xb0,93,chorus,0,91,reverb,0x87,0x40,0x80,69,0,0,0xff,0x2f,0});};
            auto file=effectFile(100,127),original=file;
            require(smaf::midiEffects(file,true,true)==file,"Enabled MIDI effects rewrote the input");
            smaf::Bridge bridge(helper,folder,file,0,5000,noAbort);auto both=render(bridge);
            std::vector<std::vector<float>> combinations;
            for(bool reverb:{false,true}) for(bool chorus:{false,true}) {
                bridge.open(folder,file,0,5000,noAbort,64,reverb,chorus);auto actual=render(bridge);
                smaf::Bridge expected(helper,folder,effectFile(reverb?100:0,chorus?127:0),0,5000,noAbort);
                require(actual==render(expected),"Effect preference does not match explicit zero MIDI sends");
                combinations.push_back(std::move(actual));
            }
            for(size_t a=0;a<combinations.size();++a) for(size_t b=0;b<a;++b)
                require(combinations[a]!=combinations[b],"Reverb and chorus switches are not independent");
            bridge.open(folder,file,0,5000,noAbort);require(render(bridge)==both,"Disabled chorus leaked into the next song");
            require(file==original,"MIDI effects changed the source buffer");
            for(int volume:{64,32,32}) {
                bridge.open(folder,song,0,5000,noAbort,volume);
                smaf::Bridge fresh(helper,folder,song,0,5000,noAbort,volume);
                for(unsigned n=0;n<100;++n) require(bridge.read(100,0,noAbort,volume)==fresh.read(100,0,noAbort,volume),"MIDI chorus leaked into SMAF after engine reset");
            }
            std::puts("PASS independent MIDI reverb/chorus, explicit send references, resets, running status and source preservation");
        }
        {
            auto file=readFile(fixtures+L"\\Morning coffee.mmf");
            smaf::Bridge bridge(helper,folder,file,0,5000,noAbort,127);auto loud=render(bridge,127);
            bridge.open(folder,file,0,5000,noAbort,64);auto quiet=render(bridge,64);
            auto clipped=[](const auto& pcm){return std::count_if(pcm.begin(),pcm.end(),[](float v){return v>=32767/32768.f||v<=-1.f;});};
            auto peak=[](const auto& pcm){float p=0;for(float v:pcm) p=std::max(p,std::abs(v));return p;};
            require(clipped(loud)>0&&clipped(quiet)==0,"Default Yamaha volume did not prevent Morning coffee clipping");
            std::printf("PASS Morning coffee: clipped samples %zu -> %zu, peak %.4f -> %.4f\n",size_t(clipped(loud)),size_t(clipped(quiet)),peak(loud),peak(quiet));
        }
        {
            smaf::Bridge warm(helper,folder,song,0,5000,noAbort);
            for(auto name:{L"MarsMine.mmf",L"Cat dance.mmf",L"GuitarMan.mmf",L"930ca_11.mmf",L"4 Dragon Quest 8 Overture.mld",L"Melody11.mmf",L"Morning coffee.mmf",L"MarsMine.mmf"}) {
                std::wprintf(L"Warm file switch: %ls\n",name);fflush(stdout);
                auto file=readFile(fixtures+L"\\"+name);warm.open(folder,file,0,5000,noAbort);
                smaf::Bridge fresh(helper,folder,file,0,5000,noAbort);
                for(unsigned i=0;i<100;++i) require(warm.read(100,0,noAbort)==fresh.read(100,0,noAbort),"Warm file switch changes PCM");
                warm.open(folder,file,0,5000,noAbort,64,false,false);fresh.open(folder,file,0,5000,noAbort,64,true,true);
                for(unsigned i=0;i<100;++i) require(warm.read(100,0,noAbort)==fresh.read(100,0,noAbort),"MIDI effect preferences changed SMAF/MLD");
                auto begin=milliseconds();warm.seek(warm.lengthMs()*3/4,noAbort);
                require(!warm.read(100,0,noAbort).empty(),"Native seek produced no audio");
                std::printf("  Native seek + first PCM: %.3f ms\n",milliseconds()-begin);
            }
        }
        for(auto name:{L"MarsMine.mmf",L"Cat dance.mmf",L"GuitarMan.mmf",L"930ca_11.mmf",L"4 Dragon Quest 8 Overture.mld"}) {
            smaf::Bridge bridge(helper,folder,readFile(fixtures+L"\\"+name),0,5000,noAbort);
            float peak=0;
            for(unsigned i=0;i<500;++i) for(float sample:bridge.read(100,0,noAbort)) {require(std::isfinite(sample)&&std::abs(sample)<=1,"Invalid PCM sample");peak=std::max(peak,std::abs(sample));}
            require(peak>0.001f,"Silent decoded fixture");
            std::wprintf(L"PASS %ls: %u ms, peak %.4f\n",name,bridge.lengthMs(),peak);
        }
        {
            smaf::Bridge first(helper,folder,song,0,5000,noAbort),second(helper,folder,song,0,5000,noAbort);
            for(unsigned i=0;i<200;++i) require(first.read(100,0,noAbort)==second.read(100,0,noAbort),"Concurrent renders differ");
            require(!first.read(70,-12,noAbort).empty(),"Low tempo/pitch rejected");
            require(!first.read(130,12,noAbort).empty(),"High tempo/pitch rejected");
            std::puts("PASS concurrent isolation and live controls");
            DWORD pid=first.processId();
            auto begin=milliseconds();first.open(folder,song,0,5000,noAbort);
            std::printf("Warm reopen: %.3f ms\n",milliseconds()-begin);
            smaf::Bridge fresh(helper,folder,song,0,5000,noAbort);
            for(unsigned i=0;i<200;++i) require(first.read(100,0,noAbort)==fresh.read(100,0,noAbort),"Warm reopen differs from fresh engine");
            require(first.processId()==pid,"Reopen restarted helper");
            begin=milliseconds();first.seek(24000,noAbort);
            std::printf("Native seek to 24 s: %.3f ms\n",milliseconds()-begin);
            require(first.processId()==pid&&!first.read(100,0,noAbort).empty(),"Native seek failed");
            first.open(folder,song,0,5000,noAbort);fresh.open(folder,song,0,5000,noAbort);
            std::vector<float> reference;for(unsigned i=0;i<300;++i) {auto block=fresh.read(100,0,noAbort);reference.insert(reference.end(),block.begin(),block.end());}
            bool eof=false;auto remainder=first.discard(54419,noAbort,64,eof);require(!eof,"Early EOF while seeking");
            require(std::equal(remainder.begin(),remainder.end(),reference.begin()+54419*2),"Batched exact seek differs");
            require(first.read(100,0,noAbort)==std::vector<float>(reference.begin()+54684*2,reference.begin()+(54684+441)*2),"Exact seek continuation differs");
            first.seek(999999,noAbort);require(first.read(100,0,noAbort).empty(),"Seek beyond EOF failed");
            first.discard(100,noAbort,64,eof);require(eof,"Discard did not report EOF");
            fails([&]{first.seek(0,[]{throw std::runtime_error("cancelled seek");});},"cancel during seek");
        }
        fails([&]{smaf::Bridge bridge(helper+L".missing",folder,song,0,5000,noAbort);},"missing helper");
        fails([&]{smaf::Bridge bridge(helper,folder+L"\\missing",song,0,5000,noAbort);},"missing libraries");
        fails([&]{smaf::Bridge bridge(helper,folder,std::vector<uint8_t>(32,0x42),0,5000,noAbort);},"malformed song");
        fails([&]{smaf::Bridge bridge(helper,folder,song,0,5000,[]{throw std::runtime_error("cancelled");});},"cancel during open");
        {
            smaf::Bridge bridge(helper,folder,song,0,5000,noAbort);
            fails([&]{bridge.read(100,0,[]{throw std::runtime_error("cancelled");});},"cancel during read");
        }
        {
            smaf::Bridge bridge(helper,folder,song,0,5000,noAbort);
            HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool killed=false;
            if(Process32FirstW(snapshot,&entry)) do {
                if(entry.th32ParentProcessID==GetCurrentProcessId()&&!_wcsicmp(entry.szExeFile,L"smaf_helper.exe")) {
                    HANDLE process=OpenProcess(PROCESS_TERMINATE,FALSE,entry.th32ProcessID);
                    if(process){TerminateProcess(process,99);CloseHandle(process);killed=true;}
                }
            } while(Process32NextW(snapshot,&entry));
            CloseHandle(snapshot);require(killed,"Could not terminate test helper");
            fails([&]{bridge.read(100,0,noAbort);},"helper crash");
        }
        if(argc==6) {testMa5(helper,argv[4],folder,fixtures);testMa5(helper,argv[5],folder,fixtures);}
        std::printf("PASS all bridge tests (%zu-bit client)\n",sizeof(void*)*8);return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
