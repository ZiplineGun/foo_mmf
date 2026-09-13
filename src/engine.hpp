#pragma once
#include <cstdint>
namespace smaf {
enum class Engine : uint32_t { automatic=0, ma5=5, ma7=7 };
inline const wchar_t* smwName(Engine engine){return engine==Engine::ma5?L"M5_EmuSmw5.dll":L"M7_EmuSmw7.dll";}
inline const wchar_t* mmsName(Engine engine){return engine==Engine::ma5?L"SMAFMMS5EMU.dll":L"SMAFMMS7EMU.dll";}
inline const wchar_t* hardwareName(Engine engine){return engine==Engine::ma5?L"M5_EmuHw.dll":L"M7_EmuHw.dll";}
}
