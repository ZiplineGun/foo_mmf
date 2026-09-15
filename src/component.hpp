#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <ole2.h>
#include <foobar2000/SDK/foobar2000.h>
#include <atomic>
#include <string>
#include "engine.hpp"
namespace smaf {
struct Settings {std::wstring folder,ma5Folder;Engine engine=Engine::automatic; bool reverb=true,chorus=true;unsigned repeats=0,fadeMs=5000,sampleRate=48000;int volume=64;};
Settings settings();
std::wstring defaultFolder();
std::wstring defaultMa5Folder();
bool engineAvailable(const std::wstring& folder,Engine engine);
extern std::atomic<uint32_t> liveControls;
// -1 follows the saved default; any other value is a session override.
extern std::atomic<int> liveVolume;
void showControls();
}
