#include "../src/bridge.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <stdexcept>

static void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
static std::vector<float> render(smaf::Bridge& bridge,int tempo=100) {
    std::vector<float> pcm;auto began=GetTickCount64();
    auto check=[&]{require(GetTickCount64()-began<120000,"Endpoint test deadline exceeded");};
    const uint64_t limit=uint64_t(bridge.lengthMs())*100*bridge.rate()/tempo/1000+11*bridge.rate();
    for(;;) {
        auto block=bridge.read(tempo,0,check);
        if(block.empty()) return pcm;
        for(float value:block) require(std::isfinite(value)&&std::abs(value)<=1,"Invalid endpoint PCM");
        pcm.insert(pcm.end(),block.begin(),block.end());
        require(pcm.size()/2<=limit,"Endpoint render did not reach EOF");
    }
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=5) {std::puts("Usage: end_tests helper DLL-folder fixture-folder engine=5|7");return 2;}
    try {
        require(_wtoi(argv[4])==5||_wtoi(argv[4])==7,"Invalid engine");
        const auto engine=static_cast<smaf::Engine>(_wtoi(argv[4]));auto check=[]{};
        for(auto name:{L"BoyMeetsGirl-Q-UTF8EN.mmf",L"BoyMeetsGirl-QQ-UTF8EN.mmf",L"ChuteDePluie-Q-UTF8EN.mmf",L"EatMyHeart-QQ-UTF8EN.mmf"}) {
            std::ifstream file(std::filesystem::path(argv[3])/name,std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),{});require(!data.empty(),"Missing endpoint fixture");
            for(unsigned rate:{48000u,44100u}) {
                smaf::Bridge bridge(argv[1],argv[2],data,0,0,check,64,true,engine,true,rate);
                const double length=bridge.lengthMs()/1000.0;auto reference=render(bridge);
                const double duration=double(reference.size()/2)/rate;
                require(std::abs(duration-length)<0.15,"Endpoint lost audio or retained excessive silence");
                if(engine==smaf::Engine::ma5&&!wcscmp(name,L"BoyMeetsGirl-Q-UTF8EN.mmf")) {
                    bridge.open(argv[2],data,0,0,check,64,true,true,rate);
                    const auto& fastTail=reference;
                    std::vector<float> pacedTail;auto began=GetTickCount64();bool paused=false;
                    auto deadline=[&]{require(GetTickCount64()-began<120000,"Paced endpoint deadline exceeded");};
                    for(;;) {
                        auto block=bridge.read(100,0,deadline);if(block.empty()) break;
                        pacedTail.insert(pacedTail.end(),block.begin(),block.end());Sleep(10);
                        if(!paused&&pacedTail.size()/2>=uint64_t(bridge.lengthMs()-500)*rate/1000) {Sleep(750);paused=true;}
                    }
                    auto energy=[&](const auto& pcm,unsigned beginMs,unsigned endMs) {
                        double sum=0;size_t begin=uint64_t(beginMs)*rate/1000*2,end=std::min(pcm.size(),size_t(uint64_t(endMs)*rate/1000*2));
                        for(size_t i=begin;i<end;++i) sum+=double(pcm[i])*pcm[i];return sum;
                    };
                    for(unsigned begin:{bridge.lengthMs()-200,bridge.lengthMs()-100}) {
                        const auto expected=energy(fastTail,begin,begin+50);
                        std::printf("Paced %u Hz %u ms: energy %.6f / %.6f, frames %zu / %zu\n",rate,begin,energy(pacedTail,begin,begin+50),expected,pacedTail.size()/2,fastTail.size()/2);fflush(stdout);
                        require(expected>0.001&&energy(pacedTail,begin,begin+50)>expected*0.5,"Paced MA5 playback lost final notes");
                    }
                }
                bridge.open(argv[2],data,0,0,check,64,true,true,rate);
                bool eof=false;unsigned offset=rate*1234/1000;auto rest=bridge.discard(offset,check,64,eof);
                require(!eof&&!rest.empty()&&reference.size()>=offset*2+rest.size(),"Exact seek returned invalid remainder");
                require(std::equal(rest.begin(),rest.end(),reference.begin()+offset*2),"Endpoint exact seek changed PCM");
                // Seek within the final seconds, run to EOF, then seek back from EOF.
                bridge.seek(bridge.lengthMs()-3000,check);auto tail=render(bridge);
                require(double(tail.size()/2)/rate>2.5&&double(tail.size()/2)/rate<3.2,"Native seek endpoint is incorrect");
                bridge.seek(0,check);require(!bridge.read(100,0,check).empty(),"Seek back from endpoint failed");
                bridge.seek(bridge.lengthMs()+1000,check);require(bridge.read(100,0,check).empty(),"Seek beyond endpoint returned PCM");
                for(int tempo:{70,130}) {
                    bridge.open(argv[2],data,0,0,check,64,true,true,rate);auto changed=render(bridge,tempo);
                    require(std::abs(double(changed.size()/2)/rate-length*100/tempo)<0.25,"Tempo-adjusted endpoint is incorrect");
                }
                // One finite repeat: drain with a separate bound allowing two songs.
                bridge.open(argv[2],data,1,0,check,64,true,true,rate);
                uint64_t frames=0;auto began=GetTickCount64();
                auto deadline=[&]{require(GetTickCount64()-began<120000,"Repeat deadline exceeded");};
                for(;;) {auto block=bridge.read(100,0,deadline);if(block.empty()) break;frames+=block.size()/2;require(double(frames)/rate<length*2+11,"Finite repeat never reached EOF");}
                require(std::abs(double(frames)/rate-length*2)<0.25,"Finite repeat ended at wrong duration");
                // A short reported position must not leave the final repeat's
                // fade above zero when the generated-time guard reaches EOF.
                bridge.open(argv[2],data,1,5000,check,64,true,true,rate);
                frames=0;began=GetTickCount64();std::vector<float> last;
                for(;;) {auto block=bridge.read(100,0,deadline);if(block.empty()) break;frames+=block.size()/2;require(double(frames)/rate<length*2+11,"Faded repeat never reached EOF");last=std::move(block);}
                require(std::abs(double(frames)/rate-length*2)<0.25&&!last.empty(),"Faded repeat ended at wrong duration");
                for(float value:last) require(value==0,"Faded repeat ended above zero");
                std::wprintf(L"PASS %ls engine %u rate %u duration %.3f: EOF, tempo, exact/native seeks, finite repeat\n",name,unsigned(engine),rate,duration);fflush(stdout);
            }
        }
        std::puts("PASS ALL ENDPOINT TESTS");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
