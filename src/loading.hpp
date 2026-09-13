#pragma once
#include <cstdint>
namespace smaf {
inline constexpr uint64_t ma5LoadStallMs=5000;
inline constexpr uint64_t requestLimitMs=30000;
enum class LoadStage : uint32_t { connecting, sendingFile, libraries, initialize, releaseSong, resetEngine,
    load, open, standby, ready, length, controls, start, count };
inline const char* stageName(LoadStage stage) {
    switch(stage) {
    case LoadStage::connecting:return "connecting to helper";
    case LoadStage::sendingFile:return "sending file to helper";
    case LoadStage::libraries:return "loading Yamaha DLLs";
    case LoadStage::initialize:return "initializing Yamaha";
    case LoadStage::releaseSong:return "releasing previous song";
    case LoadStage::resetEngine:return "resetting Yamaha";
    case LoadStage::load:return "loading song";
    case LoadStage::open:return "opening song";
    case LoadStage::standby:return "preparing song";
    case LoadStage::ready:return "waiting for Yamaha readiness";
    case LoadStage::length:return "reading song length";
    case LoadStage::controls:return "setting song controls";
    case LoadStage::start:return "starting song";
    default:return "unknown loading stage";
    }
}
}
