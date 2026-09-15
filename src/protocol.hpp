#pragma once
#include <cstdint>
namespace smaf::ipc {
constexpr uint32_t magic=0x37464d53, version=6, maxPayload=65*1024*1024;
enum Command : uint32_t { open=1, read=2, quit=3, seek=4, discard=5, progress=6, error=0xffff };
struct Header {uint32_t magicValue=magic, protocolVersion=version, command=0, bytes=0;};
struct Open {uint32_t fileBytes, folderCharacters, repeats, fadeMs; int32_t volume=64; uint32_t reverb=1, engine=7, chorus=1, sampleRate=44100;};
struct OpenReply {uint32_t lengthMs, sampleRate, channels;};
struct Read {int32_t tempo=100, transpose=0, volume=64;};
struct Seek {uint64_t milliseconds;};
struct Discard {uint32_t frames; Read controls;};
struct DiscardReply {uint32_t eof;};
static_assert(sizeof(Header)==16&&sizeof(Open)==36&&sizeof(Read)==12&&sizeof(Seek)==8&&sizeof(Discard)==16);
}
