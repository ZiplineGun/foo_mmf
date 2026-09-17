#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace smaf {
// The MA5 multimedia scheduler can stop sound at a graphics endpoint while
// audio is still buffered. Feed its audio decoder a private, graphics-free
// SMAF copy. Preserve every other chunk, including metadata and audio data.
inline uint16_t smafChecksum(const uint8_t* data,size_t size) {
    uint16_t crc=0xffff;
    for(size_t i=0;i<size;++i) {
        crc^=uint16_t(data[i])<<8;
        for(unsigned bit=0;bit<8;++bit) crc=uint16_t((crc<<1)^((crc&0x8000)?0x1021:0));
    }
    return uint16_t(crc^0xffff);
}
inline std::vector<uint8_t> audioOnlySmaf(std::vector<uint8_t> bytes) {
    if(bytes.size()<10||std::memcmp(bytes.data(),"MMMD",4)) return bytes;
    auto sizeAt=[&](size_t at) {return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];};
    if(sizeAt(4)!=bytes.size()-8) return bytes;
    const size_t end=bytes.size()-2;bool graphics=false;
    // Validate the complete container before changing anything. Invalid input
    // remains the library's responsibility; this filter must not repair it.
    for(size_t at=8;at<end;) {
        if(end-at<8) return bytes;
        const size_t count=sizeAt(at+4);
        if(count>end-at-8) return bytes;
        graphics|=std::memcmp(bytes.data()+at,"GTR",3)==0;
        at+=8+count;
    }
    if(!graphics||smafChecksum(bytes.data(),end)!=(uint16_t(bytes[end])<<8|bytes[end+1])) return bytes;
    size_t write=8;
    for(size_t at=8;at<end;) {
        const size_t count=sizeAt(at+4)+size_t(8);
        if(std::memcmp(bytes.data()+at,"GTR",3)) {std::memmove(bytes.data()+write,bytes.data()+at,count);write+=count;}
        at+=count;
    }
    bytes.resize(write+2);
    const uint32_t length=static_cast<uint32_t>(bytes.size()-8);
    for(unsigned i=0;i<4;++i) bytes[4+i]=uint8_t(length>>(24-i*8));
    const auto crc=smafChecksum(bytes.data(),write);bytes[write]=uint8_t(crc>>8);bytes[write+1]=uint8_t(crc);
    return bytes;
}
}
