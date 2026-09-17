#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "engine.hpp"
#include "loading.hpp"
#include <functional>
#include <deque>
#include <memory>

namespace smaf {
class Ma5Clock;
constexpr uint32_t sampleRate = 44100;
constexpr uint32_t blockFrames = 441;
constexpr uint32_t maxBlockFrames = 480;
constexpr uint32_t maxFileBytes = 64 * 1024 * 1024;

// This class belongs exclusively to an x86 helper process. Yamaha keeps global state.
class Yamaha {
public:
    using Progress=std::function<void(LoadStage)>;
    explicit Yamaha(const std::wstring& directory,Engine engine=Engine::ma7,Progress progress={},unsigned rate=sampleRate);
    ~Yamaha();
    Yamaha(const Yamaha&) = delete;
    Yamaha& operator=(const Yamaha&) = delete;
    void open(std::vector<uint8_t> data, unsigned repeats = 0, unsigned fadeMs = 5000, int volume = 64, bool reverb = true, bool chorus = true);
    void controls(int tempo, int transpose, int volume = 127);
    void seek(uint64_t milliseconds);
    void closeSong();
    unsigned read(float* interleaved);
    unsigned lengthMs() const { return length_; }
    unsigned rate() const {return rate_;}
    unsigned blockSize() const {return frames_;}
    int positionMs();
private:
    using Call = int(__cdecl*)();
    using One = int(__cdecl*)(int);
    using Control = int(__cdecl*)(int,unsigned,void*);
    HMODULE module_{};
    Engine engine_;
    unsigned rate_,frames_;
    Progress progress_;
    void progress(LoadStage stage){if(progress_) progress_(stage);}
    HMODULE mms_{}, hardware_{};
    std::unique_ptr<Ma5Clock> ma5Clock_;
    DLL_DIRECTORY_COOKIE directoryCookie_{};
    bool emulator_{}, initialized_{}, opened_{}, started_{};
    int handle_{};
    unsigned length_{}, repeats_{}, fadeMs_{}, tailFrames_{}, quietFrames_{};
    unsigned repeatsLeft_{};
    int tempo_=100, transpose_=0, volume_=127;
    bool ending_{}, finished_{};
    bool quietBlock_{};
    std::deque<std::vector<float>> heldTail_,readyAudio_;
    double ma5Position_{};
    uint64_t renderedTenthsMs_{};
    unsigned ma5PollBlocks_{};
    int lastPosition_=-1;
    std::vector<uint8_t> data_;
    Control control_{};
    int(__cdecl* generate_)(void*,unsigned){};
    template<typename T> T api(const char* name) const;
    template<typename T> T melodyApi(const char* name) const;
    template<typename T> T mmsApi(const char* name) const;
    int songControl(unsigned command,void* value=nullptr);
    void terminate();
    void close() noexcept;
    void resetDevice();
    void initialize();
    unsigned render(float* interleaved);
};
std::vector<uint8_t> readFile(const wchar_t* path);
}
