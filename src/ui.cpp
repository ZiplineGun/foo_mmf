#include "component.hpp"
#include "resource.h"
#include <shobjidl.h>
#include <algorithm>
#include <mutex>
#include <commctrl.h>
#pragma comment(lib,"comctl32.lib")

namespace smaf {
static cfg_string folderSetting({0x927e5cca,0x8d30,0x4825,{0x80,0x19,0x69,0x0a,0x92,0x1a,0xfa,0x23}},"");
static cfg_string ma5FolderSetting({0x261427d4,0xe61e,0x4e6d,{0xb3,0x36,0x66,0x8c,0x84,0x83,0x35,0x1f}},"");
static cfg_uint engineSetting({0x5c95eda1,0xffaa,0x4d02,{0xb5,0xa7,0xd6,0x16,0x8b,0x76,0xe0,0xd5}},0);
static cfg_uint sampleRateSetting({0x137f8e4b,0xb648,0x4311,{0x81,0x35,0xc4,0xd6,0x0a,0xd9,0xdb,0x21}},48000);
static cfg_bool chorusSetting({0x646d36dc,0x8a1a,0x46e2,{0xa1,0x22,0xe9,0x24,0xc0,0xf1,0xce,0x80}},true);
static cfg_bool reverbSetting({0x92712d17,0x8d22,0x4875,{0xa3,0x21,0x58,0xf0,0x90,0x37,0x62,0x13}},true);
static cfg_uint volumeSetting({0x8655d5ad,0x1460,0x4079,{0xb4,0x93,0xf4,0x13,0x54,0x40,0x6b,0xcc}},64);
static cfg_uint repeatsSetting({0x077d15fa,0x4ef6,0x4c64,{0xad,0xf3,0xa9,0xdd,0x45,0xf1,0x72,0x5d}},0);
static cfg_uint fadeSetting({0x7d06f817,0x45ea,0x49ce,{0xa7,0xd6,0xb3,0x3c,0xa8,0xc9,0xe2,0x28}},5);
static std::mutex settingsMutex;
std::atomic<uint32_t> liveControls{100};
std::atomic<int> liveVolume{-1};
static HWND controlWindow{};
std::wstring defaultFolder() {
    USHORT processMachine=0,nativeMachine=0;
    using Query=BOOL(WINAPI*)(HANDLE,USHORT*,USHORT*);
    auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"IsWow64Process2"));
    bool os64=false;
    if(query&&query(GetCurrentProcess(),&processMachine,&nativeMachine)) os64=nativeMachine==IMAGE_FILE_MACHINE_AMD64||nativeMachine==IMAGE_FILE_MACHINE_ARM64;
    else {SYSTEM_INFO info;GetNativeSystemInfo(&info);os64=info.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64||info.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_ARM64;}
    wchar_t drive[16]=L"C:";GetEnvironmentVariableW(L"SystemDrive",drive,16);
    return std::wstring(drive)+(os64?L"\\Program Files (x86)":L"\\Program Files")+L"\\Yamaha\\MCP-MA7";
}
Settings settings() {
    std::lock_guard<std::mutex> lock(settingsMutex);
    Settings result;auto folder=folderSetting.get();
    result.folder=folder.is_empty()?defaultFolder():std::wstring(pfc::stringcvt::string_wide_from_utf8(folder));
    auto ma5Folder=ma5FolderSetting.get();
    result.ma5Folder=ma5Folder.is_empty()?defaultMa5Folder():std::wstring(pfc::stringcvt::string_wide_from_utf8(ma5Folder));
    auto engine=engineSetting.get();result.engine=engine==5?Engine::ma5:engine==7?Engine::ma7:Engine::automatic;
    result.sampleRate=sampleRateSetting.get()==44100?44100:48000;
    result.reverb=reverbSetting.get();result.chorus=chorusSetting.get();result.volume=static_cast<int>(std::clamp<int64_t>(volumeSetting.get(),0,127));
    result.repeats=static_cast<unsigned>(std::clamp<int64_t>(repeatsSetting.get(),0,254));
    result.fadeMs=static_cast<unsigned>(std::clamp<int64_t>(fadeSetting.get(),0,30))*1000;
    return result;
}
std::wstring defaultMa5Folder() {
    SYSTEM_INFO info;GetNativeSystemInfo(&info);
    bool os64=info.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64||info.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_ARM64;
    wchar_t drive[16]=L"C:";GetEnvironmentVariableW(L"SystemDrive",drive,16);
    return std::wstring(drive)+(os64?L"\\Program Files (x86)":L"\\Program Files")+L"\\Yamaha\\MidRadio Player";
}
bool engineAvailable(const std::wstring& folder,Engine engine) {
    for(auto dll:{smwName(engine),mmsName(engine),hardwareName(engine),engine==Engine::ma7?L"EMOJIMGR.dll":L""}) {
        if(!*dll) continue;
        DWORD attributes=GetFileAttributesW((folder+L"\\"+dll).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)) return false;
    }
    return true;
}
static std::wstring text(HWND window,int id) {
    HWND control=GetDlgItem(window,id);int count=GetWindowTextLengthW(control);
    std::wstring value(size_t(count)+1,L'\0');GetWindowTextW(control,value.data(),count+1);value.resize(count);return value;
}
static bool number(HWND window,int id,int minimum,int maximum,int& value) {
    auto str=text(window,id);wchar_t* end{};long parsed=wcstol(str.c_str(),&end,10);
    if(str.empty()||end==str.c_str()||*end||parsed<minimum||parsed>maximum) return false;
    value=static_cast<int>(parsed);return true;
}
static void setNumber(HWND window,int id,int value){SetDlgItemTextW(window,id,std::to_wstring(value).c_str());}
class Preferences : public preferences_page_instance {
    HWND window_{};
    preferences_page_callback::ptr callback_;
    bool dirty_{},loading_{};
    void status() {
        auto folder=text(window_,IDC_FOLDER);if(folder.empty()) folder=defaultFolder();
        auto ma5=text(window_,IDC_MA5_FOLDER);if(ma5.empty()) ma5=defaultMa5Folder();
        std::wstring message=engineAvailable(folder,Engine::ma7)?L"MA7: files found. ":L"MA7: files missing. ";
        message+=engineAvailable(ma5,Engine::ma5)?L"MA5: files found.":L"MA5: files missing.";
        message+=L"\nMA5 falls back to MA7 for unsupported files.";
        SetDlgItemTextW(window_,IDC_STATUS,message.c_str());
    }
    void populate(bool defaults) {
        loading_=true;
        auto folder=defaults?pfc::string8():folderSetting.get();
        SetDlgItemTextW(window_,IDC_FOLDER,pfc::stringcvt::string_wide_from_utf8(folder));
        auto ma5Folder=defaults?pfc::string8():ma5FolderSetting.get();
        SetDlgItemTextW(window_,IDC_MA5_FOLDER,pfc::stringcvt::string_wide_from_utf8(ma5Folder));
        auto engine=defaults?0:engineSetting.get();
        SendDlgItemMessageW(window_,IDC_ENGINE,CB_SETCURSEL,engine==5?1:engine==7?2:0,0);
        SendDlgItemMessageW(window_,IDC_SAMPLE_RATE,CB_SETCURSEL,!defaults&&sampleRateSetting.get()==44100?0:1,0);
        CheckDlgButton(window_,IDC_REVERB,defaults||reverbSetting.get()?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(window_,IDC_CHORUS,defaults||chorusSetting.get()?BST_CHECKED:BST_UNCHECKED);
        setNumber(window_,IDC_VOLUME,defaults?64:static_cast<int>(volumeSetting.get()));
        setNumber(window_,IDC_REPEATS,defaults?0:static_cast<int>(repeatsSetting.get()));
        setNumber(window_,IDC_FADE,defaults?5:static_cast<int>(fadeSetting.get()));
        loading_=false;status();
    }
    void browse(bool ma5) {
        // Keep the service alive if the preferences host closes during the shell dialog.
        preferences_page_instance::ptr hold=this;
        IFileOpenDialog* dialog{};
        if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) return;
        DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
        dialog->SetTitle(ma5?L"Locate the Yamaha MA5 library folder":L"Locate the Yamaha MCP-MA7 folder");
        if(SUCCEEDED(dialog->Show(window_))) {
            IShellItem* item{};
            if(SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path{};if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {if(IsWindow(window_)) SetDlgItemTextW(window_,ma5?IDC_MA5_FOLDER:IDC_FOLDER,path);CoTaskMemFree(path);}
                item->Release();
            }
        }
        dialog->Release();
    }
    static INT_PTR CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
        auto self=reinterpret_cast<Preferences*>(GetWindowLongPtrW(window,DWLP_USER));
        if(message==WM_INITDIALOG) {
            self=reinterpret_cast<Preferences*>(lp);self->window_=window;SetWindowLongPtrW(window,DWLP_USER,lp);
            for(auto label:{L"Automatic (prefer MA7)",L"MA5 (fall back to MA7)",L"MA7"}) SendDlgItemMessageW(window,IDC_ENGINE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            for(auto label:{L"44,100 Hz",L"48,000 Hz"}) SendDlgItemMessageW(window,IDC_SAMPLE_RATE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            self->populate(false);return TRUE;
        }
        if(!self) return FALSE;
        if(message==WM_DESTROY) {self->window_=nullptr;return TRUE;}
        if(message==WM_COMMAND) {
            if(LOWORD(wp)==IDC_BROWSE||LOWORD(wp)==IDC_MA5_BROWSE) {self->browse(LOWORD(wp)==IDC_MA5_BROWSE);return TRUE;}
            if(!self->loading_&&(HIWORD(wp)==EN_CHANGE||((LOWORD(wp)==IDC_ENGINE||LOWORD(wp)==IDC_SAMPLE_RATE)&&HIWORD(wp)==CBN_SELCHANGE)||LOWORD(wp)==IDC_REVERB||LOWORD(wp)==IDC_CHORUS)) {
                self->dirty_=true;if(LOWORD(wp)==IDC_FOLDER||LOWORD(wp)==IDC_MA5_FOLDER) self->status();self->callback_->on_state_changed();return TRUE;
            }
        }
        return FALSE;
    }
public:
    Preferences(HWND parent,preferences_page_callback::ptr callback):callback_(callback) {
        if(!CreateDialogParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_PREFERENCES),parent,proc,reinterpret_cast<LPARAM>(this))) throw std::runtime_error("Cannot create SMAF preferences");
    }
    t_uint32 get_state() override{return preferences_state::resettable|(dirty_?preferences_state::changed:0);}
    HWND get_wnd() override{return window_;}
    void apply() override {
        int repeats,fade,volume;
        if(!number(window_,IDC_REPEATS,0,254,repeats)||!number(window_,IDC_FADE,0,30,fade)||!number(window_,IDC_VOLUME,0,127,volume)) {
            MessageBoxW(window_,L"Enter 0 to 254 repeats, 0 to 30 seconds of fade and a Yamaha volume from 0 to 127.",L"SMAF settings",MB_OK|MB_ICONINFORMATION);return;
        }
        auto folder=text(window_,IDC_FOLDER);
        auto ma5Folder=text(window_,IDC_MA5_FOLDER);
        for(auto path:{&folder,&ma5Folder}) if(!path->empty()) {
            wchar_t absolute[32768];DWORD count=GetFullPathNameW(path->c_str(),32768,absolute,nullptr);
            if(!count||count>=32768) return;*path=absolute;
        }
        {std::lock_guard<std::mutex> lock(settingsMutex);
            folderSetting=pfc::stringcvt::string_utf8_from_wide(folder.c_str());reverbSetting=IsDlgButtonChecked(window_,IDC_REVERB)==BST_CHECKED;
            chorusSetting=IsDlgButtonChecked(window_,IDC_CHORUS)==BST_CHECKED;
            ma5FolderSetting=pfc::stringcvt::string_utf8_from_wide(ma5Folder.c_str());
            auto selection=SendDlgItemMessageW(window_,IDC_ENGINE,CB_GETCURSEL,0,0);engineSetting=selection==1?5:selection==2?7:0;
            sampleRateSetting=SendDlgItemMessageW(window_,IDC_SAMPLE_RATE,CB_GETCURSEL,0,0)==0?44100:48000;
            repeatsSetting=repeats;fadeSetting=fade;volumeSetting=volume;
        }
        dirty_=false;callback_->on_state_changed();
    }
    void reset() override{populate(true);dirty_=true;callback_->on_state_changed();}
};
class PreferencesPage : public preferences_page_v3 {
public:
    const char* get_name() override{return "Yamaha SMAF";}
    GUID get_guid() override{return {0x58a9bb91,0xe3ae,0x4e62,{0x80,0x1e,0x04,0x24,0x37,0xf3,0x82,0xb7}};}
    GUID get_parent_guid() override{return guid_input;}
    preferences_page_instance::ptr instantiate(HWND parent,preferences_page_callback::ptr callback) override{return fb2k::service_new<Preferences>(parent,callback);}
};
static preferences_page_factory_t<PreferencesPage> preferencesFactory;
static bool controlLoading=false;
static constexpr UINT_PTR controlTimer=1;
static void spin(HWND window,int spinId,int buddyId,int minimum,int maximum) {
    HWND control=GetDlgItem(window,spinId);
    SendMessageW(control,UDM_SETBUDDY,reinterpret_cast<WPARAM>(GetDlgItem(window,buddyId)),0);
    SendMessageW(control,UDM_SETRANGE32,minimum,maximum);
}
static void fillControls(HWND window) {
    controlLoading=true;
    auto packed=liveControls.load();setNumber(window,IDC_TEMPO,packed&0xffff);setNumber(window,IDC_TRANSPOSE,static_cast<int16_t>(packed>>16));
    int volume=liveVolume.load();setNumber(window,IDC_VOLUME,volume<0?settings().volume:volume);
    controlLoading=false;
}
static void applyControls(HWND window) {
    KillTimer(window,controlTimer);
    int tempo,pitch,volume;
    // Incomplete or out-of-range typing leaves the last valid settings active.
    if(!number(window,IDC_TEMPO,70,130,tempo)||!number(window,IDC_TRANSPOSE,-12,12,pitch)||!number(window,IDC_VOLUME,0,127,volume)) return;
    uint32_t packed=uint32_t(tempo)|(uint32_t(uint16_t(pitch))<<16);
    if(liveControls.load()!=packed) liveControls.store(packed);
    int previous=liveVolume.load();if(volume!=(previous<0?settings().volume:previous)) liveVolume.store(volume);
}
static INT_PTR CALLBACK controlProc(HWND window,UINT message,WPARAM wp,LPARAM) {
    if(message==WM_INITDIALOG) {
        controlWindow=window;controlLoading=true;
        spin(window,IDC_TEMPO_SPIN,IDC_TEMPO,70,130);spin(window,IDC_TRANSPOSE_SPIN,IDC_TRANSPOSE,-12,12);spin(window,IDC_VOLUME_SPIN,IDC_VOLUME,0,127);
        fillControls(window);modeless_dialog_manager::g_add(window);return TRUE;
    }
    if(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(wp)==IDCANCEL)) {applyControls(window);DestroyWindow(window);return TRUE;}
    if(message==WM_DESTROY) {modeless_dialog_manager::g_remove(window);controlWindow=nullptr;return TRUE;}
    if(message==WM_COMMAND&&LOWORD(wp)==IDC_RESET) {KillTimer(window,controlTimer);liveControls.store(100);liveVolume.store(settings().volume);fillControls(window);return TRUE;}
    if(message==WM_TIMER&&wp==controlTimer) {applyControls(window);return TRUE;}
    if(message==WM_COMMAND&&LOWORD(wp)==IDOK) {applyControls(window);DestroyWindow(window);return TRUE;}
    if(message==WM_COMMAND&&HIWORD(wp)==EN_CHANGE&&!controlLoading) {
        SetTimer(window,controlTimer,350,nullptr);return TRUE;
    }
    return FALSE;
}
void showControls() {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_UPDOWN_CLASS};InitCommonControlsEx(&controls);
    if(!controlWindow) CreateDialogParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_CONTROLS),core_api::get_main_window(),controlProc,0);
    if(controlWindow) {ShowWindow(controlWindow,SW_SHOWNORMAL);SetForegroundWindow(controlWindow);}
}
class Shutdown : public initquit {void on_quit() override {if(controlWindow) DestroyWindow(controlWindow);}};
static initquit_factory_t<Shutdown> shutdownFactory;
}
