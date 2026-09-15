#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <vector>

namespace smaf {
using Metadata=std::map<std::string,std::string>;
namespace metadata_detail {
inline uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
inline const char* field(const uint8_t* tag) {
    struct Entry {const char* tag;const char* name;};
    static const Entry fields[]={{"ST","TITLE"},{"AN","ARTIST"},{"SW","COMPOSER"},{"WW","LYRICIST"},
        {"AW","ARRANGER"},{"CR","COPYRIGHT"},{"CA","CATEGORY"},{"VN","VENDOR"},{"CN","CARRIER"},
        {"GR","RIGHTS_ORGANIZATION"},{"MI","RIGHTS_INFORMATION"},{"CD","CREATION_DATE"},{"UD","MODIFICATION_DATE"}};
    for(auto& entry:fields) if(tag[0]==entry.tag[0]&&tag[1]==entry.tag[1]) return entry.name;
    return nullptr;
}
inline std::string decode(const uint8_t* p,size_t n,unsigned code,bool optional) {
    if(!n||n>65535) return {};
    std::wstring wide;
    if(code==0x20||code==0x21||code==0x24||code==0x25) {
        bool four=code==0x21||code==0x25,little=false;size_t unit=four?4:2;
        if(n%unit) return {};
        if(four&&n>=4&&p[0]==0xff&&p[1]==0xfe&&p[2]==0&&p[3]==0) {little=true;p+=4;n-=4;}
        else if(four&&n>=4&&be32(p)==0xfeff) {p+=4;n-=4;}
        else if(!four&&n>=2&&p[0]==0xff&&p[1]==0xfe) {little=true;p+=2;n-=2;}
        else if(!four&&n>=2&&p[0]==0xfe&&p[1]==0xff) {p+=2;n-=2;}
        for(size_t i=0;i<n;i+=unit) {
            uint32_t value=0;
            for(size_t j=0;j<unit;++j) value=(value<<8)|p[i+(little?unit-1-j:j)];
            if(four) {
                if(value>0x10ffff||(value>=0xd800&&value<=0xdfff)) return {};
                if(value>=0x10000) {value-=0x10000;wide.push_back(wchar_t(0xd800+(value>>10)));wide.push_back(wchar_t(0xdc00+(value&1023)));continue;}
            }
            wide.push_back(wchar_t(value));
        }
    } else {
        UINT page=0;
        switch(code) {case 0:page=932;break;case 1:page=28591;break;case 2:page=optional?50225:51949;break;
        case 3:page=52936;break;case 4:page=950;break;case 5:page=20866;break;
        case 0x22:page=CP_UTF7;break;case 0x23:page=CP_UTF8;break;default:return {};}
        DWORD flags=page==CP_UTF8?MB_ERR_INVALID_CHARS:0;
        int count=MultiByteToWideChar(page,flags,reinterpret_cast<const char*>(p),int(n),nullptr,0);
        if(!count) return {};wide.resize(count);
        if(!MultiByteToWideChar(page,flags,reinterpret_cast<const char*>(p),int(n),wide.data(),count)) return {};
    }
    if(!wide.empty()&&wide.front()==0xfeff) wide.erase(wide.begin());
    for(auto& c:wide) if(c<32||c==127) c=L' ';
    auto first=wide.find_first_not_of(L" \t\r\n"),last=wide.find_last_not_of(L" \t\r\n");
    if(first==std::wstring::npos) return {};wide=wide.substr(first,last-first+1);
    int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),int(wide.size()),nullptr,0,nullptr,nullptr);
    if(!count) return {};std::string result(count,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),int(wide.size()),result.data(),count,nullptr,nullptr);
    return result;
}
inline void add(Metadata& result,const uint8_t* tag,const uint8_t* value,size_t size,unsigned code,bool optional) {
    auto name=field(tag);if(!name) return;
    auto text=decode(value,size,code,optional);if(!text.empty()) result[name]=std::move(text);
}
inline void contents(Metadata& result,const uint8_t* p,size_t n) {
    if(n<5||n>1024*1024) return;unsigned code=p[2];size_t pos=5;
    while(pos+3<=n) {
        const auto tag=p+pos;if(p[pos+2]!=':') break;pos+=3;
        std::vector<uint8_t> value;
        while(pos<n) {
            auto c=p[pos++];if(c==',') break;
            if(c=='\\') {if(pos==n) break;c=p[pos++];}
            if(value.size()<65536) value.push_back(c);
        }
        add(result,tag,value.data(),value.size(),code,false);
    }
}
inline void optional(Metadata& result,const uint8_t* p,size_t n) {
    for(size_t pos=0;pos+8<=n;) {
        auto id=p+pos;size_t length=be32(id+4);pos+=8;if(length>n-pos) break;
        if(!memcmp(id,"Dch",3)&&id[3]!=255&&length<=1024*1024) {
            size_t end=pos+length;
            for(size_t q=pos;q+4<=end;) {
                auto tag=p+q;size_t size=size_t(p[q+2])<<8|p[q+3];q+=4;
                if(size>end-q) break;
                add(result,tag,p+q,size,id[3],true);q+=size;
            }
        }
        pos+=length;
    }
}
}
inline Metadata readMetadata(const std::vector<uint8_t>& data) {
    Metadata result;
    if(data.size()<10||memcmp(data.data(),"MMMD",4)) return result;
    size_t length=metadata_detail::be32(data.data()+4);
    if(length<2||length>data.size()-8) return result;
    size_t end=8+length-2; // File chunk ends with a two-byte CRC.
    for(size_t pos=8;pos+8<=end;) {
        auto id=data.data()+pos;size_t size=metadata_detail::be32(id+4);pos+=8;
        if(size>end-pos) break;
        if(!memcmp(id,"CNTI",4)) metadata_detail::contents(result,data.data()+pos,size);
        else if(!memcmp(id,"OPDA",4)) metadata_detail::optional(result,data.data()+pos,size);
        pos+=size;
    }
    return result;
}
}
