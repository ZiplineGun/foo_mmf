#include "../src/bridge.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <memory>
#include <random>
#include <algorithm>
int wmain(int argc,wchar_t** argv) {
    if(argc!=5) {std::puts("Usage: load_stress helper DLL-folder fixture-folder passes");return 2;}
    try {
        std::vector<std::filesystem::path> files;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(argv[3]))
            if(entry.is_regular_file()&&!_wcsicmp(entry.path().extension().c_str(),L".mmf")) files.push_back(entry.path());
        if(files.empty()) throw std::runtime_error("No MMF fixtures");
        unsigned passes=std::clamp(_wtoi(argv[4]),1,100),failures=0,timeouts=0,total=0;ULONGLONG longest=0;
        std::unique_ptr<smaf::Bridge> bridge;std::mt19937 random(25);
        for(unsigned pass=0;pass<passes;++pass) {
            std::shuffle(files.begin(),files.end(),random);
            for(const auto& path:files) {
                std::ifstream input(path,std::ios::binary);std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});
                std::wprintf(L"OPEN %u pass %u PID %lu %ls\n",++total,pass+1,bridge?bridge->processId():0,path.filename().c_str());fflush(stdout);
                auto began=GetTickCount64();
                auto check=[]{};
                try {
                    if(bridge) bridge->open(argv[2],data,0,5000,check);
                    else bridge=std::make_unique<smaf::Bridge>(argv[1],argv[2],data,0,5000,check,64,true,smaf::Engine::ma5);
                    longest=std::max(longest,GetTickCount64()-began);
                    std::printf("OK %llu ms, length %u\n",GetTickCount64()-began,bridge->lengthMs());
                } catch(const smaf::HelperTimeout& e) {++timeouts;++failures;std::printf("TIMEOUT: %s\n",e.what());bridge.reset();}
                catch(const std::exception& e) {++failures;std::printf("ERROR: %s\n",e.what());bridge.reset();}
                fflush(stdout);
            }
        }
        std::printf("TOTAL %u, failures %u, timeouts %u, longest successful open %llu ms\n",total,failures,timeouts,longest);
        return failures?1:0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 2;}
}
