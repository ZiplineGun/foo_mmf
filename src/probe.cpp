#include "yamaha.hpp"
#include <array>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

static void write16(FILE* file,uint16_t value) {unsigned char b[]={static_cast<unsigned char>(value),static_cast<unsigned char>(value>>8)};std::fwrite(b,1,2,file);}
static void write32(FILE* file,uint32_t value) {write16(file,uint16_t(value));write16(file,uint16_t(value>>16));}
static double milliseconds(){LARGE_INTEGER counter,frequency;QueryPerformanceCounter(&counter);QueryPerformanceFrequency(&frequency);return double(counter.QuadPart)*1000/frequency.QuadPart;}
static void header(FILE* file,uint32_t bytes) {
    std::rewind(file);std::fwrite("RIFF",1,4,file);write32(file,36+bytes);std::fwrite("WAVEfmt ",1,8,file);
    write32(file,16);write16(file,1);write16(file,2);write32(file,smaf::sampleRate);write32(file,smaf::sampleRate*4);
    write16(file,4);write16(file,16);std::fwrite("data",1,4,file);write32(file,bytes);
}
int wmain(int argc,wchar_t** argv) {
    if(argc<2||argc>12) {std::fprintf(stderr,"Usage: smaf_probe DLL-folder [input output.wav [tempo [transpose [repeats [volume [seek-ms [reverb [engine=5|7 [chorus]]]]]]]]]\nOr: smaf_probe DLL-folder --benchmark input engine=5|7\n");return 2;}
    try {
        wchar_t folder[32768];if(!GetFullPathNameW(argv[1],32768,folder,nullptr)) throw std::runtime_error("Invalid DLL folder");
        if(argc==5&&!wcscmp(argv[2],L"--benchmark")) {
            auto data=smaf::readFile(argv[3]);double begin=milliseconds();
            smaf::Yamaha engine(folder,_wtoi(argv[4])==5?smaf::Engine::ma5:smaf::Engine::ma7);
            std::printf("DLL load + initialize: %.3f ms\n",milliseconds()-begin);
            std::array<float,smaf::blockFrames*2> pcm{};
            for(unsigned n=0;n<6;++n) {
                begin=milliseconds();engine.open(data);double opened=milliseconds();engine.read(pcm.data());
                std::printf("%s %u: open %.3f ms, first PCM %.3f ms\n",n?"Warm":"Initial",n,opened-begin,milliseconds()-opened);
            }
            return 0;
        }
        smaf::Yamaha engine(folder,argc>10&&_wtoi(argv[10])==5?smaf::Engine::ma5:smaf::Engine::ma7);
        if(argc==2) {std::puts("Yamaha initialized and hardware reset successfully");return 0;}
        if(argc<4) throw std::runtime_error("Input and output paths are required");
        engine.open(smaf::readFile(argv[2]),argc>6?_wtoi(argv[6]):0,5000,argc>7?_wtoi(argv[7]):64,argc>9?_wtoi(argv[9])!=0:true,argc>11?_wtoi(argv[11])!=0:true);
        engine.controls(argc>4?_wtoi(argv[4]):100,argc>5?_wtoi(argv[5]):0,argc>7?_wtoi(argv[7]):64);
        if(argc>8&&_wtoi(argv[8])) engine.seek(_wtoi(argv[8]));
        FILE* output{};if(_wfopen_s(&output,argv[3],L"wb")||!output) throw std::runtime_error("Cannot create WAV");
        header(output,0);
        std::array<float,smaf::blockFrames*2> pcm{};
        uint32_t frames=0;int peak=0;
        while(auto count=engine.read(pcm.data())) {
            for(unsigned i=0;i<count*2;++i) {
                int value=std::clamp(static_cast<int>(pcm[i]*32768.0f),-32768,32767);
                peak=std::max(peak,std::abs(value));write16(output,static_cast<uint16_t>(value));
            }
            frames+=count;
            if(frames>smaf::sampleRate*60*30) throw std::runtime_error("Probe duration limit exceeded");
        }
        header(output,frames*4);if(std::fclose(output)) throw std::runtime_error("WAV write failed");
        std::printf("Length=%u ms, rendered=%.3f s, peak=%d, position=%d ms\n",engine.lengthMs(),double(frames)/smaf::sampleRate,peak,engine.positionMs());
        return peak?0:5;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"Probe failed: %s\n",e.what());
        TerminateProcess(GetCurrentProcess(),6);return 6;
    }
}
