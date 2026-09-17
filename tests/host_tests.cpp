// Loaded only into the disposable portable foobar profile created by Host-Test.ps1.
#include "../src/component.hpp"
#include "../src/resource.h"
#include <oleacc.h>
#include <cstdio>
#include <vector>
#include <stdexcept>
#include <commctrl.h>
#include <tlhelp32.h>
#include <set>
#include <filesystem>
#include <random>
#include <fstream>
#include <foobar2000/SDK/console_manager.h>
#include "freeze_helper.hpp"

DECLARE_COMPONENT_VERSION("foo_mmf host tests","0.1.0","Local integration tests; do not install in a normal profile.");
VALIDATE_COMPONENT_FILENAME("foo_mmf_tests.dll");
static void require(bool value,const char* message){if(!value) throw std::runtime_error(message);}
class Callback : public preferences_page_callback {void on_state_changed() override {}};
static FILE* logFile;
static unsigned expectedRate=44100;
static void capturePcm(const std::vector<audio_sample>& actual,const std::vector<audio_sample>& expected,const char* message) {
    if(!GetEnvironmentVariableW(L"FOO_MMF_CAPTURE_PCM",nullptr,0)) return;
    wchar_t root[32768];DWORD count=GetEnvironmentVariableW(L"FOO_MMF_TEST_ROOT",root,32768);
    if(!count||count>=32768) return;
    const auto captureId=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const auto folder=std::filesystem::path(root)/L"analysis"/L"pcm-captures"/captureId;
    std::filesystem::create_directories(folder);
    auto write=[&](const wchar_t* name,const std::vector<audio_sample>& samples) {
        std::ofstream file(folder/name,std::ios::binary);
        auto u16=[&](uint16_t value){file.put(char(value));file.put(char(value>>8));};
        auto u32=[&](uint32_t value){u16(uint16_t(value));u16(uint16_t(value>>16));};
        uint32_t bytes=static_cast<uint32_t>(samples.size()*sizeof(float));
        file.write("RIFF",4);u32(36+bytes);file.write("WAVEfmt ",8);u32(16);u16(3);u16(2);
        u32(expectedRate);u32(expectedRate*8);u16(8);u16(32);file.write("data",4);u32(bytes);
        for(auto sample:samples) {float value=static_cast<float>(sample);file.write(reinterpret_cast<const char*>(&value),sizeof(value));}
        if(!file) throw std::runtime_error("Cannot save PCM capture");
    };
    write(L"actual.wav",actual);write(L"reference.wav",expected);
    std::ofstream note(folder/L"capture.txt");note<<message<<"\nTest fixture: MarsMine.mmf, excerpt from start\nSample rate: "<<expectedRate<<"\nActual samples: "<<actual.size()<<"\nReference samples: "<<expected.size()<<"\n";
    fprintf(logFile,"Saved unmodified PCM pair in analysis/pcm-captures/%s\n",pfc::stringcvt::string_utf8_from_wide(captureId.c_str()).get_ptr());fflush(logFile);
}
static void log(const char* message){fprintf(logFile,"%s\n",message);fflush(logFile);}
static void requirePcmEqual(const std::vector<audio_sample>& actual,const std::vector<audio_sample>& expected,const char* message) {
    if(actual==expected) return;
    size_t different=0;double maxDelta=0,actualEnergy=0,expectedEnergy=0;
    for(size_t i=0;i<std::min(actual.size(),expected.size());++i) {
        if(actual[i]!=expected[i]) ++different;
        maxDelta=std::max(maxDelta,std::abs(double(actual[i])-expected[i]));
        actualEnergy+=double(actual[i])*actual[i];expectedEnergy+=double(expected[i])*expected[i];
    }
    fprintf(logFile,"PCM mismatch: actual=%zu expected=%zu differing=%zu maxDelta=%.9g energyRatio=%.9g\n",actual.size(),expected.size(),different,maxDelta,expectedEnergy?actualEnergy/expectedEnergy:0);
    capturePcm(actual,expected,message);
    fflush(logFile);throw std::runtime_error(message);
}
static std::wstring environment(const wchar_t* name){wchar_t value[32768];DWORD count=GetEnvironmentVariableW(name,value,32768);require(count&&count<32768,"Missing test environment");return value;}
static input_entry::ptr decoderEntry, mldEntry, midiEntry;
static void pump(unsigned milliseconds);
static void clearConsole(){console::clearBacklog();}
static unsigned countConsole(const char* text) {
    pump(30); // Let foobar publish queued console messages.
    unsigned n=0;auto lines=console::getLines();
    for(auto line:lines->typed<fb2k::string>()) if(strstr(line->c_str(),text)) ++n;
    return n;
}
static std::set<DWORD> helpers() {
    std::set<DWORD> result;HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Process32FirstW(snapshot,&entry)) do {if(entry.th32ParentProcessID==GetCurrentProcessId()&&!_wcsicmp(entry.szExeFile,L"smaf_helper.exe")) result.insert(entry.th32ProcessID);} while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return result;
}
static void pump(unsigned milliseconds) {
    ULONGLONG start=GetTickCount64();MSG message;
    while(GetTickCount64()-start<milliseconds) {
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        Sleep(5);
    }
}
static input_decoder::ptr openDecoder(const std::wstring& path,unsigned flags) {
    input_decoder::ptr decoder;
    auto ext=path.substr(path.find_last_of(L'.')+1);
    auto entry=ext==L"mld"?mldEntry:(ext==L"mid"||ext==L"midi")?midiEntry:decoderEntry;
    entry->open_for_decoding(decoder,nullptr,pfc::stringcvt::string_utf8_from_wide(path.c_str()),fb2k::noAbort);
    decoder->initialize(0,flags,fb2k::noAbort);return decoder;
}
static std::vector<audio_sample> collect(input_decoder::ptr decoder,size_t frames) {
    std::vector<audio_sample> result;audio_chunk_impl chunk;
    while(result.size()<frames*2&&decoder->run(chunk,fb2k::noAbort)) {
        require(chunk.get_sample_rate()==expectedRate&&chunk.get_channels()==2,"Wrong host PCM format");
        result.insert(result.end(),chunk.get_data(),chunk.get_data()+chunk.get_used_size());
    }
    return result;
}
static BOOL CALLBACK findControl(HWND window,LPARAM value) {
    DWORD process=0;GetWindowThreadProcessId(window,&process);wchar_t title[128];GetWindowTextW(window,title,128);
    if(process==GetCurrentProcessId()&&!wcscmp(title,L"SMAF control")) *reinterpret_cast<HWND*>(value)=window;
    return TRUE;
}
static void accessibleName(HWND window,int id,const wchar_t* expected) {
    IAccessible* accessible{};
    require(SUCCEEDED(AccessibleObjectFromWindow(GetDlgItem(window,id),OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void**>(&accessible))),"No MSAA interface");
    VARIANT child;VariantInit(&child);child.vt=VT_I4;child.lVal=CHILDID_SELF;BSTR name{};
    HRESULT status=accessible->get_accName(child,&name);
    bool match=SUCCEEDED(status)&&name&&wcsstr(name,expected);if(name) SysFreeString(name);accessible->Release();require(match,"Missing accessible field label");
}
static void recoveryTests(const std::wstring& root,preferences_page_instance::ptr instance) {
    HWND prefs=instance->get_wnd();
    SetDlgItemTextW(prefs,IDC_MA5_FOLDER,(root+L"\\newer_ma5").c_str());
    SendDlgItemMessageW(prefs,IDC_ENGINE,CB_SETCURSEL,1,0);instance->apply();
    auto song=root+L"\\test files\\Melody11.mmf";
    auto metadata=[&](abort_callback& abort) {
        input_info_reader::ptr reader;decoderEntry->open_for_info_read(reader,nullptr,pfc::stringcvt::string_utf8_from_wide(song.c_str()),abort);
        file_info_impl info;reader->get_info(0,info,abort);require(info.get_length()>1,"Missing recovered metadata");
        return std::string(info.info_get("synthesizer"));
    };
    auto verifyStopped=[](FreezeMa5& freeze,unsigned expected) {
        require(freeze.pids().size()==expected,"Could not suspend the expected MA5 helpers");
        for(auto pid:freeze.pids()) require(processExited(pid),"Timed-out helper was not terminated");
    };
    clearConsole();
    {
        FreezeMa5 freeze(1);auto began=GetTickCount64();
        require(metadata(fb2k::noAbort)=="Yamaha MA5","First stall did not recover with MA5 retry");
        require(GetTickCount64()-began<8000,"MA5 retry exceeded its bounded wait");verifyStopped(freeze,1);
    }
    require(countConsole("Retrying MA5 once")==1&&countConsole("retry succeeded")==1&&countConsole("Melody11.mmf")>=2,"Retry was not logged with filename");
    log("PASS suspended MA5 metadata load retries in a fresh helper and logs recovery");
    clearConsole();
    {
        FreezeMa5 freeze(2);auto began=GetTickCount64();
        require(metadata(fb2k::noAbort)=="Yamaha MCP-MA7","Repeated MA5 stalls did not fall back");
        require(GetTickCount64()-began<14000,"Repeated MA5 stalls exceeded bounded wait");verifyStopped(freeze,2);
    }
    require(countConsole("timed out")==3&&countConsole("Retrying MA5 once")==1&&countConsole("falling back to MA7")==1,"Repeated stall/fallback logging incorrect");
    auto playback=openDecoder(song,input_flag_playback);require(!collect(playback,4410).empty(),"Playback failed after metadata recovery");playback.release();
    log("PASS repeated stalls terminate both helpers and fall back to MA7; next file opens");
    SetDlgItemTextW(prefs,IDC_FOLDER,(root+L"\\analysis\\missing-ma7").c_str());instance->apply();clearConsole();
    {
        FreezeMa5 freeze(2);bool failed=false;auto began=GetTickCount64();
        try {metadata(fb2k::noAbort);} catch(const exception_io_data&) {failed=true;}
        require(failed&&GetTickCount64()-began<14000,"Missing MA7 did not report a bounded error");verifyStopped(freeze,2);
    }
    require(countConsole("No fallback engine available")==1,"Missing fallback not logged");
    log("PASS repeated stalls without MA7 fail cleanly and do not retry indefinitely");
    clearConsole();
    {
        FreezeMa5 freeze(1);abort_callback_impl abort;
        std::thread cancel([&]{Sleep(1000);abort.abort();});bool cancelled=false;auto began=GetTickCount64();
        try {metadata(abort);} catch(const exception_aborted&) {cancelled=true;} catch(...) {cancel.join();throw;}
        cancel.join();require(cancelled&&GetTickCount64()-began<2500,"Cancellation waited for stall timeout");verifyStopped(freeze,1);
    }
    require(countConsole("Retrying MA5")==0&&countConsole("falling back")==0,"Cancellation triggered recovery");
    log("PASS cancellation promptly discards frozen helper without retry or fallback");
    instance->reset();instance->apply();
}
static void featureTests(const std::wstring& root,preferences_page_instance::ptr instance) {
    HWND prefs=instance->get_wnd();accessibleName(prefs,IDC_SAMPLE_RATE,L"Sample rate");
    instance->reset();
    require(SendDlgItemMessageW(prefs,IDC_SAMPLE_RATE,CB_GETCURSEL,0,0)==1,"Sample rate default is not 48 kHz");
    HWND rateControl=GetDlgItem(prefs,IDC_SAMPLE_RATE);
    SetFocus(rateControl);SendMessageW(rateControl,WM_KEYDOWN,VK_UP,0);SendMessageW(rateControl,WM_KEYUP,VK_UP,0);
    require(SendMessageW(rateControl,CB_GETCURSEL,0,0)==0,"Sample rate cannot be changed with the keyboard");
    require(GetNextDlgTabItem(prefs,rateControl,FALSE)==GetDlgItem(prefs,IDC_REVERB),"Sample rate tab order is incorrect");
    RECT previous{},current{};GetWindowRect(rateControl,&previous);
    for(int id:{IDC_REVERB,IDC_CHORUS,IDC_REPEATS,IDC_FADE,IDC_VOLUME}) {
        GetWindowRect(GetDlgItem(prefs,id),&current);require(current.top>=previous.bottom,"Preferences controls overlap");previous=current;
    }
    SetDlgItemTextW(prefs,IDC_FOLDER,(root+L"\\MCP-MA7").c_str());
    SetDlgItemTextW(prefs,IDC_MA5_FOLDER,(root+L"\\newer_ma5").c_str());instance->apply();
    const auto song=root+L"\\test files\\meta\\GuitarMan.mmf";
    for(int engine:{2,1}) for(int rateIndex:{1,0}) {
        SendDlgItemMessageW(prefs,IDC_ENGINE,CB_SETCURSEL,engine,0);
        SendDlgItemMessageW(prefs,IDC_SAMPLE_RATE,CB_SETCURSEL,rateIndex,0);
        SendMessageW(prefs,WM_COMMAND,MAKEWPARAM(IDC_SAMPLE_RATE,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(prefs,IDC_SAMPLE_RATE)));
        require((instance->get_state()&preferences_state::changed)!=0,"Sample rate change did not mark preferences dirty");
        instance->apply();expectedRate=rateIndex?48000:44100;
        auto decoder=openDecoder(song,0);file_info_impl info;decoder->get_info(0,info,fb2k::noAbort);
        require(info.meta_get("TITLE",0)&&!strcmp(info.meta_get("TITLE",0),"Guitar Man"),"MMF title missing from host metadata");
        require(info.meta_get("COPYRIGHT",0)&&strstr(info.meta_get("COPYRIGHT",0),"Yamaha Corporation"),"MMF copyright missing from host metadata");
        require(info.info_get_int("samplerate")==expectedRate,"Metadata reports wrong rate");
        auto original=collect(decoder,expectedRate*3);
        decoder->seek(1.234,fb2k::noAbort);auto sought=collect(decoder,expectedRate/10);
        size_t offset=static_cast<size_t>(audio_math::time_to_samples(1.234,expectedRate))*2;
        require(std::equal(sought.begin(),sought.end(),original.begin()+offset),"Selected-rate conversion seek not sample exact");
        decoder.release();
        auto playback=openDecoder(song,input_flag_playback);playback->seek(20,fb2k::noAbort);require(!collect(playback,expectedRate/10).empty(),"Selected-rate native seek failed");
    }
    log("PASS MMF title/copyright in foobar and MA7/MA5 playback/conversion/native/exact seeks at both rates");
    auto active=openDecoder(song,0); // MA5 at 44.1 kHz after the loop above.
    SendDlgItemMessageW(prefs,IDC_SAMPLE_RATE,CB_SETCURSEL,1,0);instance->apply();
    require(!collect(active,4410).empty(),"Preference change interrupted the active format");
    active->initialize(0,0,fb2k::noAbort);expectedRate=48000;
    require(!collect(active,4800).empty(),"Reinitialized decoder ignored the new sample rate");active.release();
    log("PASS sample rate keyboard navigation, layout and session reinitialization");
    SendDlgItemMessageW(prefs,IDC_ENGINE,CB_SETCURSEL,2,0);
    SendDlgItemMessageW(prefs,IDC_SAMPLE_RATE,CB_SETCURSEL,1,0);instance->apply();expectedRate=48000;
    for(auto name:{L"GuitarMan.mmf",L"Cat dance.mmf",L"MarsMine.mmf"}) {
        auto path=root+L"\\test files\\"+name;
        for(unsigned pass=0;pass<2;++pass) {
            auto decoder=openDecoder(path,input_flag_playback);file_info_impl info;decoder->get_info(0,info,fb2k::noAbort);
            auto pcm=collect(decoder,expectedRate*60);
            require(std::abs(double(pcm.size()/2)/expectedRate-info.get_length())<0.011,"Loop example retains appended silence");
        }
    }
    log("PASS consecutive host decodes of all three loop examples have no 250 ms tail gap");
    instance->reset();instance->apply();expectedRate=44100;
}
static void endpointTests(const std::wstring& root,preferences_page_instance::ptr instance) {
    HWND prefs=instance->get_wnd();
    SetDlgItemTextW(prefs,IDC_MA5_FOLDER,(root+L"\\newer_ma5").c_str());
    for(int engine:{1,2}) for(int rateIndex:{0,1}) {
        SendDlgItemMessageW(prefs,IDC_ENGINE,CB_SETCURSEL,engine,0);
        SendDlgItemMessageW(prefs,IDC_SAMPLE_RATE,CB_SETCURSEL,rateIndex,0);instance->apply();
        const unsigned rate=rateIndex?48000:44100;
        for(auto name:{L"BoyMeetsGirl-Q-UTF8EN.mmf",L"BoyMeetsGirl-QQ-UTF8EN.mmf",L"ChuteDePluie-Q-UTF8EN.mmf",L"EatMyHeart-QQ-UTF8EN.mmf"}) {
            for(unsigned flags:{0u,unsigned(input_flag_testing_integrity|input_flag_no_looping),unsigned(input_flag_playback)}) {
                auto decoder=openDecoder(root+L"\\test files\\"+name,flags);file_info_impl info;decoder->get_info(0,info,fb2k::noAbort);
                require(info.info_get("synthesizer")&&!strcmp(info.info_get("synthesizer"),engine==1?"Yamaha MA5":"Yamaha MCP-MA7"),"Endpoint test used the wrong engine");
                audio_chunk_impl chunk;uint64_t frames=0;auto began=GetTickCount64();
                while(decoder->run(chunk,fb2k::noAbort)) {
                    require(chunk.get_sample_rate()==rate&&chunk.get_channels()==2,"Endpoint output format differs");frames+=chunk.get_sample_count();
                    require(double(frames)/rate<info.get_length()+11,"Endpoint host decoder never reached EOF");
                    require(GetTickCount64()-began<120000,"Endpoint host test deadline exceeded");
                }
                require(std::abs(double(frames)/rate-info.get_length())<0.15,"Endpoint host duration is incorrect");
                decoder->seek(info.get_length()+10,fb2k::noAbort);require(!decoder->run(chunk,fb2k::noAbort),"Endpoint seek past EOF returned audio");
                decoder->seek(0,fb2k::noAbort);require(decoder->run(chunk,fb2k::noAbort),"Endpoint seek back from EOF failed");
                fprintf(logFile,"PASS endpoint %s engine %d rate %u flags %u duration %.3f\n",pfc::stringcvt::string_utf8_from_wide(name).get_ptr(),engine,rate,flags,double(frames)/rate);fflush(logFile);
            }
        }
    }
    instance->reset();instance->apply();
    log("PASS endpoint files: both engines/rates, playback/conversion/integrity and EOF seeks");
}
static void corpusTests(const std::wstring& root,preferences_page_instance::ptr instance) {
    HWND prefs=instance->get_wnd();SendDlgItemMessageW(prefs,IDC_ENGINE,CB_SETCURSEL,2,0);
    SendDlgItemMessageW(prefs,IDC_SAMPLE_RATE,CB_SETCURSEL,1,0);instance->apply();
    std::vector<std::wstring> files;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root+L"\\test files\\n900i"))
        if(entry.is_regular_file()&&!_wcsicmp(entry.path().extension().c_str(),L".mld")) files.push_back(entry.path().wstring());
    require(!files.empty(),"No n900i MLD fixtures");std::mt19937 random(25);std::shuffle(files.begin(),files.end(),random);
    std::atomic<size_t> next{0};std::atomic<unsigned> failures{0};std::mutex output;
    std::vector<std::thread> threads;
    for(unsigned worker=0;worker<8;++worker) threads.emplace_back([&] {
        for(;;) {
            size_t index=next.fetch_add(1);if(index>=files.size()*2) break;
            auto& path=files[index%files.size()];
            unsigned flags=index<files.size()?0:input_flag_testing_integrity|input_flag_no_looping;
            try {
                auto decoder=openDecoder(path,flags);file_info_impl info;decoder->get_info(0,info,fb2k::noAbort);
                audio_chunk_impl chunk;uint64_t frames=0;ULONGLONG began=GetTickCount64();
                while(decoder->run(chunk,fb2k::noAbort)) {
                    require(chunk.get_sample_rate()==48000&&chunk.get_channels()==2,"Corpus output format differs");
                    frames+=chunk.get_sample_count();
                    require(double(frames)/48000<=info.get_length()+11,"Corpus decoder never reached EOF");
                    require(GetTickCount64()-began<120000,"Corpus render exceeded test deadline");
                }
                require(frames>0,"Corpus render is empty");
                // Exercise the validator's seeks into and back from EOF.
                decoder->seek(info.get_length()+10,fb2k::noAbort);require(!decoder->run(chunk,fb2k::noAbort),"Corpus seek beyond EOF returned audio");
                decoder->seek(0,fb2k::noAbort);require(decoder->run(chunk,fb2k::noAbort),"Corpus seek from EOF failed");
                {std::lock_guard<std::mutex> lock(output);fprintf(logFile,"OK corpus %zu %s %.3f s\n",index+1,flags?"integrity":"conversion",double(frames)/48000);fflush(logFile);}
            } catch(const std::exception& e) {
                ++failures;std::lock_guard<std::mutex> lock(output);
                fprintf(logFile,"FAIL corpus %zu %s: %s\n",index+1,pfc::stringcvt::string_utf8_from_wide(path.c_str()).get_ptr(),e.what());fflush(logFile);
            }
        }
    });
    for(auto& thread:threads) thread.join();
    require(failures==0,"Concurrent corpus conversion/integrity tests failed");
    log("PASS all n900i files, eight concurrent decoders, conversion/integrity EOF and seeks");
    instance->reset();instance->apply();
}
static void runTests() {
    std::wstring root;
    try {
        root=environment(L"FOO_MMF_TEST_ROOT");
        _wfopen_s(&logFile,(root+L"\\analysis\\host-results.txt").c_str(),L"w");if(!logFile) return;
        log("Starting real foobar host integration tests");
        preferences_page_v3::ptr page;
        service_enum_t<preferences_page> pages;preferences_page::ptr candidate;
        while(pages.next(candidate)) if(!strcmp(candidate->get_name(),"Yamaha SMAF")) {candidate->service_query_t(page);break;}
        require(page.is_valid(),"SMAF preferences were not registered");
        bool decodingPage=false;service_enum_t<preferences_page> priorityPages;
        while(priorityPages.next(candidate)) if(!strcmp(candidate->get_name(),"Decoding")&&candidate->get_parent_guid()==preferences_page::guid_playback) decodingPage=true;
        require(decodingPage,"Documented Playback > Decoding preference path not found");
        auto instance=page->instantiate(core_api::get_main_window(),fb2k::service_new<Callback>());HWND prefs=instance->get_wnd();
        SetDlgItemTextW(prefs,IDC_FOLDER,(root+L"\\MCP-MA7").c_str());
        SendDlgItemMessageW(prefs,IDC_SAMPLE_RATE,CB_SETCURSEL,0,0);
        SendDlgItemMessageW(prefs,IDC_ENGINE,CB_SETCURSEL,0,0);
        CheckDlgButton(prefs,IDC_REVERB,BST_CHECKED);SetDlgItemTextW(prefs,IDC_VOLUME,L"64");SetDlgItemTextW(prefs,IDC_REPEATS,L"0");SetDlgItemTextW(prefs,IDC_FADE,L"5");instance->apply();
        accessibleName(prefs,IDC_FOLDER,L"folder");accessibleName(prefs,IDC_REPEATS,L"repeats");
        accessibleName(prefs,IDC_ENGINE,L"engine");accessibleName(prefs,IDC_MA5_FOLDER,L"folder");
        accessibleName(prefs,IDC_CHORUS,L"chorus");
        require(IsDlgButtonChecked(prefs,IDC_CHORUS)==BST_CHECKED,"Chorus must default on");
        require(GetDlgCtrlID(GetNextDlgTabItem(prefs,GetDlgItem(prefs,IDC_REVERB),FALSE))==IDC_CHORUS,"Chorus tab order is incorrect");
        require(GetDlgCtrlID(GetNextDlgTabItem(prefs,GetDlgItem(prefs,IDC_ENGINE),FALSE))==IDC_FOLDER,"Engine combo tab order is incorrect");
        service_enum_t<input_entry> inputs;input_entry::ptr input;
        while(inputs.next(input)) {
            input_entry_v2::ptr v2;if(input->service_query_t(v2)) {if(!strcmp(v2->get_name(),"Yamaha MMF decoder")) decoderEntry=input; if(!strcmp(v2->get_name(),"Yamaha MLD decoder")) mldEntry=input; if(!strcmp(v2->get_name(),"Yamaha MIDI decoder")) midiEntry=input;}
        }
        require(decoderEntry.is_valid()&&mldEntry.is_valid()&&midiEntry.is_valid(),"Separate decoders were not registered");
        require(!decoderEntry->is_our_path("tone.mid","mid")&&!mldEntry->is_our_path("tone.mmf","mmf")&&!midiEntry->is_our_path("tone.mld","mld"),"Decoder extensions overlap");
        require(midiEntry->is_our_path("tone.mid","mid")&&midiEntry->is_our_path("tone.midi","midi"),"MIDI must always be registered");
        accessibleName(prefs,IDC_VOLUME,L"volume");
        log("PASS preferences, accessible labels and always-on MIDI");
        if(GetEnvironmentVariableW(L"FOO_MMF_TEST_FEATURES",nullptr,0)||GetEnvironmentVariableW(L"FOO_MMF_TEST_CORPUS",nullptr,0)||GetEnvironmentVariableW(L"FOO_MMF_TEST_ENDPOINTS",nullptr,0)) {
            if(GetEnvironmentVariableW(L"FOO_MMF_TEST_ENDPOINTS",nullptr,0)) endpointTests(root,instance);
            else if(GetEnvironmentVariableW(L"FOO_MMF_TEST_CORPUS",nullptr,0)) corpusTests(root,instance);else featureTests(root,instance);
            DestroyWindow(prefs);instance.release();decoderEntry.release();mldEntry.release();midiEntry.release();
            log("PASS ALL HOST TESTS");fclose(logFile);logFile=nullptr;standard_commands::main_exit();return;
        }
        if(GetEnvironmentVariableW(L"FOO_MMF_TEST_RECOVERY",nullptr,0)) {
            recoveryTests(root,instance);DestroyWindow(prefs);instance.release();decoderEntry.release();mldEntry.release();midiEntry.release();
            log("PASS ALL HOST TESTS");fclose(logFile);logFile=nullptr;standard_commands::main_exit();return;
        }
        const std::wstring song=root+L"\\test files\\MarsMine.mmf";
        for(auto name:{L"MarsMine.mmf",L"Cat dance.mmf",L"GuitarMan.mmf",L"930ca_11.mmf",L"4 Dragon Quest 8 Overture.mld",L"Melody11.mmf",L"Morning coffee.mmf"}) {
            auto decoder=openDecoder(root+L"\\test files\\"+name,0);
            file_info_impl info;decoder->get_info(0,info,fb2k::noAbort);require(info.get_length()>1,"Missing duration");
            auto pcm=collect(decoder,44100);double peak=0;for(auto sample:pcm) peak=std::max(peak,std::abs(double(sample)));require(peak>0.001,"Silent host decode");
        }
        auto pids=helpers();require(!pids.empty(),"No warm helper retained");
        auto midi=openDecoder(root+L"\\analysis\\tone.mid",0);auto wet=collect(midi,44100*6);midi.release();
        require(helpers()==pids,"Switching files restarted a helper");
        CheckDlgButton(prefs,IDC_REVERB,BST_UNCHECKED);instance->apply();
        midi=openDecoder(root+L"\\analysis\\tone.mid",0);auto dry=collect(midi,44100*6);midi.release();
        require(dry!=wet&&dry.size()<wet.size(),"MIDI reverb preference did not remove tail");
        require(midiEntry->is_our_path("tone.mid","mid"),"Reverb preference changed MIDI registration");
        CheckDlgButton(prefs,IDC_REVERB,BST_CHECKED);instance->apply();
        log("PASS warm helpers across files and MIDI reverb preference");
        midi=openDecoder(root+L"\\analysis\\chorus.mid",0);auto withChorus=collect(midi,44100*7);midi.release();
        // Exercise Space on the checkbox through the native button procedure.
        SendDlgItemMessageW(prefs,IDC_CHORUS,WM_KEYDOWN,VK_SPACE,0);SendDlgItemMessageW(prefs,IDC_CHORUS,WM_KEYUP,VK_SPACE,0);
        require(IsDlgButtonChecked(prefs,IDC_CHORUS)==BST_UNCHECKED,"Space did not toggle chorus");
        require(instance->get_state()&preferences_state::changed,"Chorus did not mark preferences changed");instance->apply();
        require(IsDlgButtonChecked(prefs,IDC_REVERB)==BST_CHECKED,"Chorus checkbox altered reverb");
        midi=openDecoder(root+L"\\analysis\\chorus.mid",0);auto withoutChorus=collect(midi,44100*7);midi.release();
        require(withChorus!=withoutChorus,"Chorus preference did not affect conversion");
        midi=openDecoder(root+L"\\analysis\\chorus.mid",input_flag_playback);
        require(collect(midi,44100*7)==withoutChorus,"Playback ignored chorus preference");midi.release();
        CheckDlgButton(prefs,IDC_CHORUS,BST_CHECKED);instance->apply();
        midi=openDecoder(root+L"\\analysis\\chorus.mid",0);require(collect(midi,44100*7)==withChorus,"Warm reuse retained disabled chorus");midi.release();
        log("PASS MIDI chorus default, accessible label, keyboard toggle, playback/conversion and warm reset");
        log("PASS MA2/MA3/MA5/MA7, MLD and MIDI decoding in foobar");
        auto decoder=openDecoder(song,0);auto reference=collect(decoder,44100*3);
        decoder->seek(1.234,fb2k::noAbort);auto sought=collect(decoder,4410);
        size_t offset=static_cast<size_t>(audio_math::time_to_samples(1.234,44100))*2;
        require(std::equal(sought.begin(),sought.end(),reference.begin()+offset),"Seek was not sample accurate");
        decoder->seek(0,fb2k::noAbort);require(collect(decoder,44100)==std::vector<audio_sample>(reference.begin(),reference.begin()+88200),"Seek to start differs");
        log("PASS sample-accurate seek and restart");
        GUID menuGuid={0xc684c572,0x217b,0x4051,{0xb4,0x85,0x60,0x7f,0x99,0x57,0x08,0x52}};
        require(mainmenu_commands::g_execute(menuGuid),"SMAF control command missing");HWND controls{};EnumWindows(findControl,reinterpret_cast<LPARAM>(&controls));require(controls!=nullptr,"SMAF control window missing");
        accessibleName(controls,IDC_TEMPO,L"Tempo");accessibleName(controls,IDC_TRANSPOSE,L"Transpose");accessibleName(controls,IDC_VOLUME,L"volume");
        require(GetDlgCtrlID(GetNextDlgTabItem(controls,GetDlgItem(controls,IDC_TEMPO),FALSE))==IDC_TRANSPOSE,"Tab order is incorrect");
        require(!GetDlgItem(controls,IDOK),"Apply button still present");
        require(GetDlgCtrlID(GetNextDlgTabItem(controls,GetDlgItem(controls,IDC_TRANSPOSE),FALSE))==IDC_VOLUME,"Volume tab order is incorrect");
        require(reinterpret_cast<HWND>(SendDlgItemMessageW(controls,IDC_TEMPO_SPIN,UDM_GETBUDDY,0,0))==GetDlgItem(controls,IDC_TEMPO),"Tempo spin has no buddy");
        int low=0,high=0;SendDlgItemMessageW(controls,IDC_VOLUME_SPIN,UDM_GETRANGE32,reinterpret_cast<WPARAM>(&low),reinterpret_cast<LPARAM>(&high));require(low==0&&high==127,"Volume spin range incorrect");
        // Exercise actual keyboard spin handling, then a volume-only debounced change.
        const unsigned volumeRounds=GetEnvironmentVariableW(L"FOO_MMF_CAPTURE_PCM",nullptr,0)?20:1;
        for(unsigned volumeRound=0;volumeRound<volumeRounds;++volumeRound) {
            SendDlgItemMessageW(controls,IDC_VOLUME,WM_KEYDOWN,VK_UP,0);SendDlgItemMessageW(controls,IDC_VOLUME,WM_KEYUP,VK_UP,0);
            require(GetDlgItemInt(controls,IDC_VOLUME,nullptr,FALSE)==65,"Up key did not increment volume spin");
            SetDlgItemTextW(controls,IDC_VOLUME,L"32");
            auto beforePause=openDecoder(song,input_flag_playback);auto normal=collect(beforePause,44100);beforePause.release();
            requirePcmEqual(normal,std::vector<audio_sample>(reference.begin(),reference.begin()+88200),"Volume applied before inactivity timer");
            pump(450);
            auto volumeOnly=openDecoder(song,input_flag_playback);auto quieter=collect(volumeOnly,44100);volumeOnly.release();
            if(quieter==normal) capturePcm(quieter,normal,"Temporary volume alone has no effect (both clips identical)");
            require(quieter!=normal,"Temporary volume alone has no effect");
            SetDlgItemTextW(controls,IDC_VOLUME,L"999");pump(450);
            volumeOnly=openDecoder(song,input_flag_playback);requirePcmEqual(collect(volumeOnly,44100),quieter,"Invalid volume changed playback");volumeOnly.release();
            SetDlgItemTextW(prefs,IDC_VOLUME,L"32");instance->apply();
            auto savedVolume=openDecoder(song,0);requirePcmEqual(collect(savedVolume,44100),quieter,"Saved volume not applied to a new decoder");savedVolume.release();
            SendMessageW(controls,WM_COMMAND,IDC_RESET,0);require(GetDlgItemInt(controls,IDC_VOLUME,nullptr,FALSE)==32,"Reset ignored saved volume");
            SetDlgItemTextW(prefs,IDC_VOLUME,L"64");instance->apply();SendMessageW(controls,WM_COMMAND,IDC_RESET,0);
        }
        log("PASS keyboard spins, debounce, invalid edits and saved/temporary volume");
        SetDlgItemTextW(controls,IDC_TEMPO,L"130");SetDlgItemTextW(controls,IDC_TRANSPOSE,L"12");SetDlgItemTextW(controls,IDC_VOLUME,L"32");pump(450);
        auto conversion=openDecoder(song,0);auto converted=collect(conversion,44100);
        requirePcmEqual(converted,std::vector<audio_sample>(reference.begin(),reference.begin()+88200),"Live controls changed conversion");
        auto playback=openDecoder(song,input_flag_playback);require(collect(playback,44100)!=converted,"Live controls did not change playback");
        pids=helpers();auto seekStart=GetTickCount64();playback->seek(24,fb2k::noAbort);
        require(GetTickCount64()-seekStart<1000,"Playback seek is too slow");require(helpers()==pids,"Playback seek restarted a helper");
        require(!collect(playback,4410).empty(),"Playback after native seek failed");
        playback->seek(999,fb2k::noAbort);require(collect(playback,441).empty(),"Seek past end must reach EOF");
        playback->seek(0,fb2k::noAbort);require(!collect(playback,4410).empty(),"Native seek from EOF failed");
        SendMessageW(controls,WM_COMMAND,IDC_RESET,0);SendMessageW(controls,WM_COMMAND,IDOK,0);
        require(!IsWindow(controls),"Enter did not close SMAF control");
        log("PASS accessible control labels, tab order, live playback and conversion isolation");
        decoder.release();conversion.release();playback.release();
        SetDlgItemTextW(prefs,IDC_REPEATS,L"1");instance->apply();
        auto repeated=openDecoder(song,0);audio_chunk_impl chunk;uint64_t frames=0;
        while(repeated->run(chunk,fb2k::noAbort)){frames+=chunk.get_sample_count();require(frames<44100*65,"Repeat did not end");}
        require(frames>44100*55&&frames<44100*58,"Incorrect repeat duration");repeated.release();
        auto noLoops=openDecoder(song,input_flag_no_looping);frames=0;
        while(noLoops->run(chunk,fb2k::noAbort)){frames+=chunk.get_sample_count();require(frames<44100*32,"No-loop flag ignored");}
        noLoops.release();log("PASS finite repeats, EOF and no-loop flag");
        SetDlgItemTextW(prefs,IDC_REPEATS,L"0");
        SetDlgItemTextW(prefs,IDC_MA5_FOLDER,(root+L"\\newer_ma5").c_str());instance->apply();
        const auto melody=root+L"\\test files\\Melody11.mmf";
        auto selected=openDecoder(melody,0);file_info_impl engineInfo;selected->get_info(0,engineInfo,fb2k::noAbort);
        require(!strcmp(engineInfo.info_get("synthesizer"),"Yamaha MCP-MA7"),"Automatic selection did not prefer MA7");
        auto ma7Pcm=collect(selected,44100);selected.release();
        SendDlgItemMessageW(prefs,IDC_ENGINE,CB_SETCURSEL,1,0);instance->apply();
        selected=openDecoder(melody,0);selected->get_info(0,engineInfo,fb2k::noAbort);
        require(!strcmp(engineInfo.info_get("synthesizer"),"Yamaha MA5"),"MA5 selection was ignored");
        auto ma5Pcm=collect(selected,44100*3);require(!std::equal(ma7Pcm.begin(),ma7Pcm.end(),ma5Pcm.begin()),"MA5 selection did not change Melody11 audio");
        selected->seek(1.234,fb2k::noAbort);auto ma5Seek=collect(selected,4410);
        require(std::equal(ma5Seek.begin(),ma5Seek.end(),ma5Pcm.begin()+offset),"MA5 host reconstruction is not sample exact");
        selected.release();pids=helpers();
        selected=openDecoder(song,0);collect(selected,4410);selected.release();require(helpers()==pids,"MA5 warm helper was not retained");
        for(auto path:{root+L"\\test files\\930ca_11.mmf",root+L"\\test files\\4 Dragon Quest 8 Overture.mld",root+L"\\analysis\\tone.mid"}) {
            selected=openDecoder(path,0);selected->get_info(0,engineInfo,fb2k::noAbort);
            require(!strcmp(engineInfo.info_get("synthesizer"),"Yamaha MCP-MA7"),"Unsupported MA5 file did not fall back to MA7");
            require(!collect(selected,44100).empty(),"Fallback produced no audio");selected.release();
        }
        SetDlgItemTextW(prefs,IDC_FOLDER,(root+L"\\analysis\\missing-ma7").c_str());
        SendDlgItemMessageW(prefs,IDC_ENGINE,CB_SETCURSEL,0,0);instance->apply();
        selected=openDecoder(melody,0);selected->get_info(0,engineInfo,fb2k::noAbort);
        require(!strcmp(engineInfo.info_get("synthesizer"),"Yamaha MA5"),"Automatic MA5-only selection failed");selected.release();
        bool unsupported=false;try {selected=openDecoder(root+L"\\analysis\\tone.mid",0);} catch(const std::exception&) {unsupported=true;}
        require(unsupported,"Unsupported MIDI succeeded with MA7 absent");
        SetDlgItemTextW(prefs,IDC_FOLDER,(root+L"\\MCP-MA7").c_str());
        SetDlgItemTextW(prefs,IDC_MA5_FOLDER,L"");SendDlgItemMessageW(prefs,IDC_ENGINE,CB_SETCURSEL,1,0);instance->apply();
        selected=openDecoder(melody,0);selected->get_info(0,engineInfo,fb2k::noAbort);
        require(!strcmp(engineInfo.info_get("synthesizer"),"Yamaha MA5"),"Default MidRadio path/older MA5 did not load");
        require(collect(selected,44100)==std::vector<audio_sample>(ma5Pcm.begin(),ma5Pcm.begin()+88200),"Older/newer MA5 Melody11 mismatch");selected.release();
        SetDlgItemTextW(prefs,IDC_MA5_FOLDER,(root+L"\\analysis\\missing-ma5").c_str());instance->apply();
        selected=openDecoder(melody,0);require(collect(selected,44100)==ma7Pcm,"Missing MA5 did not recover through MA7");selected.release();
        instance->reset();instance->apply();
        require(SendDlgItemMessageW(prefs,IDC_ENGINE,CB_GETCURSEL,0,0)==0,"Reset did not restore automatic engine selection");
        require(IsDlgButtonChecked(prefs,IDC_CHORUS)==BST_CHECKED,"Reset did not enable chorus");
        log("PASS MA5 selection, accessible combo/path, both library versions, warm reuse, exact seek, automatic selection and MA7 fallback");
        DestroyWindow(prefs);instance.release();decoderEntry.release();mldEntry.release();midiEntry.release();
        log("PASS ALL HOST TESTS");
    } catch(const std::exception& e) {if(logFile){fprintf(logFile,"FAIL: %s\n",e.what());fflush(logFile);}}
    if(logFile){fclose(logFile);logFile=nullptr;}
    standard_commands::main_exit();
}
class TestStartup : public initquit {
    void on_init() override{if(GetEnvironmentVariableW(L"FOO_MMF_TEST_ROOT",nullptr,0)) fb2k::inMainThread(runTests);}
};
static initquit_factory_t<TestStartup> startup;
