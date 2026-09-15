#define NOMINMAX
#include "../src/bridge.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <memory>
#include <random>
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>
#include <cmath>

int wmain(int argc,wchar_t** argv) {
    if(argc!=8) {std::puts("Usage: render_stress helper DLL-folder song-folder engine=5|7 rate=44100|48000 passes workers");return 2;}
    try {
        const auto engine=_wtoi(argv[4])==5?smaf::Engine::ma5:smaf::Engine::ma7;
        const unsigned rate=_wtoi(argv[5]),passes=std::clamp(_wtoi(argv[6]),1,100),workers=std::clamp(_wtoi(argv[7]),1,16);
        if(rate!=44100&&rate!=48000) throw std::runtime_error("Unsupported rate");
        std::vector<std::filesystem::path> files;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(argv[3])) {
            auto ext=entry.path().extension().wstring();
            if(entry.is_regular_file()&&(!_wcsicmp(ext.c_str(),L".mmf")||!_wcsicmp(ext.c_str(),L".mld")||!_wcsicmp(ext.c_str(),L".mid"))) files.push_back(entry.path());
        }
        if(files.empty()) throw std::runtime_error("No song fixtures");
        std::mt19937 random(25);std::vector<std::filesystem::path> jobs;
        for(unsigned pass=0;pass<passes;++pass) {std::shuffle(files.begin(),files.end(),random);jobs.insert(jobs.end(),files.begin(),files.end());}
        std::atomic<size_t> next{0};std::atomic<unsigned> failures{0};std::mutex output;
        std::vector<std::thread> threads;
        for(unsigned worker=0;worker<workers;++worker) threads.emplace_back([&,worker] {
            std::unique_ptr<smaf::Bridge> bridge;
            for(;;) {
                auto index=next.fetch_add(1);if(index>=jobs.size()) break;const auto& path=jobs[index];
                auto began=GetTickCount64();uint64_t frames=0;unsigned length=0;
                {std::lock_guard<std::mutex> lock(output);std::wprintf(L"OPEN %zu worker %u %ls\n",index+1,worker,path.c_str());fflush(stdout);}
                try {
                    std::ifstream input(path,std::ios::binary);std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});
                    auto check=[&]{if(GetTickCount64()-began>120000) throw std::runtime_error("Song exceeded 120-second test deadline");};
                    if(bridge) bridge->open(argv[2],data,0,5000,check,64,true,true,rate);
                    else bridge=std::make_unique<smaf::Bridge>(argv[1],argv[2],data,0,5000,check,64,true,engine,true,rate);
                    length=bridge->lengthMs();
                    for(;;) {
                        auto block=bridge->read(100,0,check);if(block.empty()) break;frames+=block.size()/2;
                        for(float value:block) if(!std::isfinite(value)||std::abs(value)>1) throw std::runtime_error("Invalid PCM");
                        if(frames>uint64_t(length+11000)*rate/1000) throw std::runtime_error("No EOF beyond nominal duration plus release tail");
                    }
                    std::lock_guard<std::mutex> lock(output);
                    std::wprintf(L"OK %zu worker %u PID %lu nominal %u rendered %.3f elapsed %llu ms %ls\n",index+1,worker,bridge->processId(),length,double(frames)/rate,GetTickCount64()-began,path.c_str());fflush(stdout);
                } catch(const std::exception& e) {
                    ++failures;std::lock_guard<std::mutex> lock(output);
                    std::wprintf(L"FAIL %zu worker %u nominal %u rendered %.3f %ls: %hs\n",index+1,worker,length,double(frames)/rate,path.c_str(),e.what());fflush(stdout);bridge.reset();
                }
            }
        });
        for(auto& thread:threads) thread.join();
        std::printf("TOTAL %zu renders, %u workers, %u failures\n",jobs.size(),workers,failures.load());return failures?1:0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 2;}
}
