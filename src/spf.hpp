#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace smaf {
// SPF (SMAF Phrase) is an "MMMD" container whose score lives in an "MMMG" chunk.
// Yamaha's MA7 Melody API rejects it (-18); it must go through Mapi_Phrase_*.
// The decision is made from the contents, never from the file extension, so an
// SPF saved as .mmf (or the reverse) still takes the right path.
// Only top-level chunk headers are inspected, within the declared bounds; the
// input is never modified. Same rule as smaf2wav's smaf_format_sniff().
inline bool isSpf(const uint8_t* d,size_t n) {
    if(n<10||std::memcmp(d,"MMMD",4)) return false;
    auto be32=[&](size_t at){return size_t(d[at])<<24|size_t(d[at+1])<<16|size_t(d[at+2])<<8|size_t(d[at+3]);};
    const size_t end=n-2; // trailing 16-bit checksum
    bool phrase=false,tracks=false;
    for(size_t at=8;at<end;) {
        if(end-at<8) break;
        const size_t count=be32(at+4);
        if(count>end-at-8) break;
        if(!std::memcmp(d+at,"MMMG",4)) phrase=true;
        if(!std::memcmp(d+at,"MTR",3)||!std::memcmp(d+at,"ATR",3)||!std::memcmp(d+at,"GTR",3)) tracks=true;
        at+=8+count;
    }
    return phrase&&!tracks;
}
inline bool isSpf(const std::vector<uint8_t>& data){return isSpf(data.data(),data.size());}

// The saved/live Yamaha volume is 0..127 with 64 as the melody default, but
// Mapi_Phrase_SetVolume (-1..127) defaults to 100 when left alone. Map the
// shared 0..127 scale piecewise-linearly so that the default 64 gives the
// phrase API's own default 100, 0 stays silent and 127 stays 127.
inline int phraseVolume(int volume) {
    if(volume<0) return 0;
    if(volume>127) return 127;
    if(volume<=64) return (volume*100+32)/64;
    return 100+((volume-64)*27+31)/63;
}
}
