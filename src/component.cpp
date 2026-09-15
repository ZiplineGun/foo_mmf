#include "component.hpp"
#include "bridge.hpp"
#include "metadata.hpp"
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>

DECLARE_COMPONENT_VERSION("Yamaha SMAF decoder","1.1.1","SMAF MA2/MA3/MA5/MA7, MLD and MIDI playback using user-supplied Yamaha MA7 or MA5 libraries.\nYamaha libraries run in an isolated x86 helper process.");
VALIDATE_COMPONENT_FILENAME("foo_mmf.dll");

namespace {
// Each checked-out helper belongs to one decoder. Two idle workers accommodate
// foobar's overlapping current/next-track opens without sharing Yamaha state.
std::mutex poolMutex;
std::vector<std::unique_ptr<smaf::Bridge>> idleHelpers;
bool stopping=false;
void recycle(std::unique_ptr<smaf::Bridge>& bridge) noexcept {
    if(!bridge) return;
    std::lock_guard<std::mutex> lock(poolMutex);
    if(!stopping&&bridge->healthy()) {
        try {if(idleHelpers.size()==2) idleHelpers.erase(idleHelpers.begin());idleHelpers.push_back(std::move(bridge));} catch(...) {}
    }
    bridge.reset();
}
std::unique_ptr<smaf::Bridge> acquire(const std::wstring& folder,smaf::Engine engine) {
    std::lock_guard<std::mutex> lock(poolMutex);
    for(auto it=idleHelpers.begin();it!=idleHelpers.end();) {
        if(!(*it)->healthy()) {it=idleHelpers.erase(it);continue;}
        if((*it)->folder()==folder&&(*it)->engine()==engine) {
            auto bridge=std::move(*it);idleHelpers.erase(it);return bridge;
        }
        ++it;
    }
    return {};
}
class PoolShutdown : public initquit {
    void on_quit() override {std::lock_guard<std::mutex> lock(poolMutex);stopping=true;idleHelpers.clear();}
};
initquit_factory_t<PoolShutdown> poolShutdown;
std::wstring helperPath() {
    std::wstring path=pfc::stringcvt::string_wide_from_utf8(core_api::get_my_full_path());
    auto slash=path.find_last_of(L"\\/");
    if(slash==std::wstring::npos) throw exception_io_data("Cannot locate the component folder");
    return path.substr(0,slash+1)+L"smaf_helper.exe";
}
enum class Format {mmf,mld,midi};
template<Format format> class input_smaf : public input_stubs {
    file::ptr file_;
    std::vector<uint8_t> data_;
    smaf::Metadata metadata_;
    std::unique_ptr<smaf::Bridge> bridge_;
    smaf::Settings settings_;
    unsigned length_{};
    unsigned sampleRate_=48000;
    unsigned decodeRepeats_{};
    unsigned loadedRepeats_{};
    bool playback_{};
    bool pristine_=false;
    std::vector<float> pending_;
    const char* codec_="SMAF";
    std::string path_;
    smaf::Engine actualEngine_=smaf::Engine::ma7;
    void startEngine(smaf::Engine engine,abort_callback& abort,unsigned repeats) {
        const auto& folder=engine==smaf::Engine::ma5?settings_.ma5Folder:settings_.folder;
        if(bridge_&&(bridge_->engine()!=engine||bridge_->folder()!=folder)) recycle(bridge_);
        if(!bridge_) bridge_=acquire(folder,engine);
        for(unsigned attempt=0;;++attempt) {
            try {
                if(bridge_) bridge_->open(folder,data_,repeats,settings_.fadeMs,[&]{abort.check();},settings_.volume,settings_.reverb,settings_.chorus,settings_.sampleRate);
                else bridge_=std::make_unique<smaf::Bridge>(helperPath(),folder,data_,repeats,settings_.fadeMs,[&]{abort.check();},settings_.volume,settings_.reverb,engine,settings_.chorus,settings_.sampleRate);
                if(attempt) console::printf("foo_mmf: MA5 retry succeeded for \"%s\".",path_.c_str());
                break;
            } catch(const smaf::HelperTimeout& e) {
                bridge_.reset();abort.check();
                console::printf("foo_mmf: %s: %s; file \"%s\".",engine==smaf::Engine::ma5?"MA5":"MA7",e.what(),path_.c_str());
                if(engine!=smaf::Engine::ma5||attempt) throw;
                console::printf("foo_mmf: Retrying MA5 once in a fresh helper for \"%s\".",path_.c_str());
            }
        }
        actualEngine_=engine;
    }
    void start(abort_callback& abort,unsigned repeats) {
        pending_.clear();
        try {
            auto engine=settings_.engine;
            if(engine==smaf::Engine::automatic) engine=smaf::engineAvailable(settings_.folder,smaf::Engine::ma7)?smaf::Engine::ma7:smaf::Engine::ma5;
            try {startEngine(engine,abort,repeats);}
            catch(const exception_aborted&) {throw;}
            catch(const std::exception& first) {
                bridge_.reset();abort.check();
                // Automatic selection can recover from an unusable MA7 installation.
                auto fallback=engine==smaf::Engine::ma5?smaf::Engine::ma7:smaf::Engine::ma5;
                const auto& folder=fallback==smaf::Engine::ma5?settings_.ma5Folder:settings_.folder;
                if((engine!=smaf::Engine::ma5&&settings_.engine!=smaf::Engine::automatic)||!smaf::engineAvailable(folder,fallback)) {
                    if(dynamic_cast<const smaf::HelperTimeout*>(&first)) console::printf("foo_mmf: No fallback engine available for \"%s\"; giving up.",path_.c_str());
                    throw;
                }
                console::printf("foo_mmf: %s open failed (%s); falling back to %s for \"%s\".",engine==smaf::Engine::ma5?"MA5":"MA7",first.what(),fallback==smaf::Engine::ma5?"MA5":"MA7",path_.c_str());
                std::string error=first.what();
                try {startEngine(fallback,abort,repeats);}
                catch(const exception_aborted&) {throw;}
                catch(const std::exception& second) {throw std::runtime_error(error+"; fallback: "+second.what());}
            }
        }
        catch(const exception_aborted&) {bridge_.reset();throw;}
        catch(const std::exception& e) {bridge_.reset();throw exception_io_data(e.what());}
        length_=bridge_->lengthMs();
        sampleRate_=bridge_->rate();
        loadedRepeats_=repeats;
        pristine_=true;
    }
    std::vector<float> read(abort_callback& abort,bool normal=false) {
        auto controls=playback_&&!normal?smaf::liveControls.load():100u;
        int pitch=static_cast<int16_t>(controls>>16);
        int volume=playback_&&!normal?smaf::liveVolume.load():-1;
        if(volume<0) volume=settings_.volume;
        pristine_=false;
        try {return bridge_->read(controls&0xffff,pitch,[&]{abort.check();},volume);}
        catch(const exception_aborted&) {bridge_.reset();throw;}
        catch(const std::exception& e) {bridge_.reset();throw exception_io_data(e.what());}
    }
public:
    ~input_smaf(){recycle(bridge_);}
    void open(file::ptr hint,const char* path,t_input_open_reason reason,abort_callback& abort) {
        if(reason==input_open_info_write) throw exception_tagging_unsupported();
        path_=path;
        file_=hint;input_open_file_helper(file_,path,reason,abort);
        auto size=file_->get_size_ex(abort);
        if(size<12||size>64*1024*1024) throw exception_io_data("Invalid or oversized SMAF input (maximum 64 MiB)");
        data_.resize(static_cast<size_t>(size));file_->read_object(data_.data(),data_.size(),abort);
        if(!memcmp(data_.data(),"MMMD",4)) codec_="SMAF";
        else if(!memcmp(data_.data(),"melo",4)) codec_="MLD";
        else if(!memcmp(data_.data(),"MThd",4)) codec_="MIDI";
        else throw exception_io_data("Unrecognized SMAF, MLD or MIDI header");
        if((format==Format::mmf&&strcmp(codec_,"SMAF"))||(format==Format::mld&&strcmp(codec_,"MLD"))||(format==Format::midi&&strcmp(codec_,"MIDI"))) throw exception_io_unsupported_format();
        metadata_=smaf::readMetadata(data_);
        settings_=smaf::settings();start(abort,0);
        if(reason==input_open_info_read) recycle(bridge_);
    }
    void get_info(file_info& info,abort_callback&) {
        for(const auto& entry:metadata_) info.meta_set(entry.first.c_str(),entry.second.c_str());
        info.set_length(double(length_)*(settings_.repeats+1)/1000.0);
        info.info_set("codec",codec_);info.info_set("synthesizer",actualEngine_==smaf::Engine::ma5?"Yamaha MA5":"Yamaha MCP-MA7");
        info.info_set("encoding","synthesized");info.info_set_int("samplerate",sampleRate_);
        info.info_set_int("channels",2);info.info_set_int("bitspersample",16);
    }
    t_filestats get_file_stats(abort_callback& abort){return file_->get_stats(abort);}
    t_filestats2 get_stats2(unsigned flags,abort_callback& abort){return file_->get_stats2_(flags,abort);}
    void decode_initialize(unsigned flags,abort_callback& abort) {
        playback_=(flags&input_flag_playback)!=0&&(flags&(input_flag_testing_integrity|input_flag_canonical_decode))==0;
        auto previous=settings_;settings_=smaf::settings();
        decodeRepeats_=(flags&(input_flag_no_looping|input_flag_canonical_decode))?0:settings_.repeats;
        if(!bridge_||!pristine_||decodeRepeats_!=loadedRepeats_||previous.folder!=settings_.folder||previous.ma5Folder!=settings_.ma5Folder||previous.engine!=settings_.engine||previous.volume!=settings_.volume||previous.reverb!=settings_.reverb||previous.chorus!=settings_.chorus||previous.fadeMs!=settings_.fadeMs||previous.sampleRate!=settings_.sampleRate) start(abort,decodeRepeats_);
    }
    bool decode_run(audio_chunk& chunk,abort_callback& abort) {
        abort.check();auto pcm=pending_.empty()?read(abort):std::move(pending_);pending_.clear();
        if(pcm.empty()) return false;
        chunk.set_data_32(pcm.data(),pcm.size()/2,2,sampleRate_);return true;
    }
    void decode_seek(double seconds,abort_callback& abort) {
        if(!std::isfinite(seconds)||seconds<0) throw exception_io_data("Invalid seek position");
        seconds=std::min(seconds,double(length_)*(decodeRepeats_+1)/1000+10);
        try {
            pending_.clear();
            if(playback_) {
                bridge_->seek(static_cast<uint64_t>(seconds*1000),[&]{abort.check();});pristine_=false;
            } else {
                start(abort,decodeRepeats_);
                uint64_t remaining=audio_math::time_to_samples(seconds,sampleRate_);
                while(remaining) {
                    unsigned frames=static_cast<unsigned>(std::min(remaining,uint64_t(sampleRate_*5)));
                    bool eof=false;pending_=bridge_->discard(frames,[&]{abort.check();},settings_.volume,eof);remaining-=frames;
                    if(eof) break;
                }
                pristine_=false;
            }
        }
        catch(const exception_aborted&) {bridge_.reset();throw;}
        catch(const std::exception& e) {bridge_.reset();throw exception_io_data(e.what());}
    }
    bool decode_can_seek(){return true;}
    bool decode_get_dynamic_info(file_info&,double&){return false;}
    bool decode_get_dynamic_info_track(file_info&,double&){return false;}
    void decode_on_idle(abort_callback& abort){abort.check();}
    void retag(const file_info&,abort_callback&){throw exception_tagging_unsupported();}
    void remove_tags(abort_callback&){throw exception_tagging_unsupported();}
    static bool g_is_our_content_type(const char*){return false;}
    static bool g_is_our_path(const char*,const char* ext) {
        if constexpr(format==Format::mmf) return !_stricmp(ext,"mmf");
        if constexpr(format==Format::mld) return !_stricmp(ext,"mld");
        return !_stricmp(ext,"mid")||!_stricmp(ext,"midi");
    }
    static const char* g_get_name(){return format==Format::mmf?"Yamaha MMF decoder":format==Format::mld?"Yamaha MLD decoder":"Yamaha MIDI decoder";}
    static GUID g_get_guid(){
        if constexpr(format==Format::mld) return {0x66a884a5,0x2d78,0x4057,{0xb1,0x27,0x98,0x55,0xd7,0xd5,0x4c,0x23}};
        if constexpr(format==Format::midi) return {0xc5745014,0xf0b2,0x417f,{0x91,0x9e,0xa0,0x37,0x11,0xbd,0x02,0x30}};
        return {0x78efc790,0x61e2,0x4ebe,{0x88,0x13,0x5f,0x4b,0xad,0x92,0x13,0xeb}};
    }
};
input_singletrack_factory_t<input_smaf<Format::mmf>> mmfFactory;
input_singletrack_factory_t<input_smaf<Format::mld>> mldFactory;
input_singletrack_factory_t<input_smaf<Format::midi>> midiFactory;
class file_types : public input_file_type {
public:
    unsigned get_count() override{return 3;}
    bool get_name(unsigned index,pfc::string_base& out) override{out=index==0?"Yamaha SMAF (MMF)":index==1?"Yamaha MLD":"Yamaha MIDI";return index<3;}
    bool get_mask(unsigned index,pfc::string_base& out) override{out=index==0?"*.MMF":index==1?"*.MLD":"*.MID;*.MIDI";return index<3;}
    bool is_associatable(unsigned) override{return true;}
};
service_factory_single_t<file_types> typeFactory;
class menu : public mainmenu_commands {
public:
    t_uint32 get_command_count() override{return 1;}
    GUID get_command(t_uint32) override{return {0xc684c572,0x217b,0x4051,{0xb4,0x85,0x60,0x7f,0x99,0x57,0x08,0x52}};}
    void get_name(t_uint32,pfc::string_base& out) override{out="SMAF control";}
    bool get_description(t_uint32,pfc::string_base& out) override{out="Adjust Yamaha playback tempo, transposition and volume for this session.";return true;}
    GUID get_parent() override{return mainmenu_groups::view;}
    void execute(t_uint32,service_ptr_t<service_base>) override{smaf::showControls();}
};
mainmenu_commands_factory_t<menu> menuFactory;
}
