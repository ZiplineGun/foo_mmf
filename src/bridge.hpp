#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include "protocol.hpp"
#include "engine.hpp"
#include "loading.hpp"
#include <stdexcept>

namespace smaf {
class HelperTimeout : public std::runtime_error {public:using std::runtime_error::runtime_error;};
class Bridge {
public:
    using Check=std::function<void()>;
    Bridge(const std::wstring& executable,const std::wstring& folder,const std::vector<uint8_t>& data,unsigned repeats,unsigned fadeMs,const Check& check,int volume=64,bool reverb=true,Engine engine=Engine::ma7,bool chorus=true,unsigned rate=44100);
    ~Bridge();
    Bridge(const Bridge&)=delete;
    Bridge& operator=(const Bridge&)=delete;
    unsigned lengthMs() const {return length_;}
    unsigned rate() const {return rate_;}
    void open(const std::wstring& folder,const std::vector<uint8_t>& data,unsigned repeats,unsigned fadeMs,const Check& check,int volume=64,bool reverb=true,bool chorus=true,unsigned rate=44100);
    std::vector<float> read(int tempo,int transpose,const Check& check,int volume=64);
    void seek(uint64_t milliseconds,const Check& check);
    std::vector<float> discard(unsigned frames,const Check& check,int volume,bool& eof);
    bool healthy() const {return process_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT;}
    DWORD processId() const {return GetProcessId(process_);}
    const std::wstring& folder() const {return folder_;}
    Engine engine() const {return engine_;}
private:
    HANDLE pipe_=INVALID_HANDLE_VALUE,process_{},job_{};
    unsigned length_{};
    std::wstring folder_;
    Engine engine_;
    unsigned rate_=44100;
    ULONGLONG requestStarted_{},progressAt_{};
    bool opening_{};
    LoadStage stage_=LoadStage::connecting;
    void beginRequest(bool opening,LoadStage stage);
    void checkDeadline(const Check& check);
    void close() noexcept;
    void wait(OVERLAPPED& operation,const Check& check);
    void transfer(void* data,size_t size,bool write,const Check& check);
    std::vector<uint8_t> transact(uint32_t command,const void* data,uint32_t bytes,const Check& check);
};
}
