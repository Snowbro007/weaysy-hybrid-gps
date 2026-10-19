// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Weaysy
// Local research add-on: exact native shader fingerprints + own tagged colors.
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <map>
#include <mutex>
#include <vector>
#include <string>
#include <atomic>
#include "reshade.hpp"
#include "generated_shaders.h"
#include "native_fingerprints.h"
#include "../time_reader/include/weaysy_theme_ipc.h"

namespace api = reshade::api;
extern "C" __declspec(dllexport) const char* NAME = "Weaysy Full GPS Clock Probe";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "LOCAL ONLY 0.0.5: one game.time theme, tagged GPS colors and adaptive dark/green map labels, DX11.";

namespace {
struct DeviceState { api::pipeline shaders[4][2]{}; bool ready=false; };
struct CommandState {
    api::device* owner=nullptr;
    int kind=0; uint64_t native=0, bound=0;
    uint64_t rtv=0; float width=0, height=0;
};
std::recursive_mutex lock;
std::mutex log_lock;
std::map<api::device*,DeviceState> devices;
struct NativeShader { api::device* owner; int kind; };
std::map<uint64_t,NativeShader> native_shaders;
std::map<api::command_list*,CommandState> commands;
thread_local bool internal_call=false;
HMODULE self_module=nullptr, compiler_module=nullptr;
HANDLE log_file=INVALID_HANDLE_VALUE, mapping=nullptr;
const weaysy::ThemeSnapshot* mapped=nullptr;
std::atomic<unsigned> phase{0};
uint64_t frame_number=0, calls[4]{};
unsigned trace_budget=15;
bool last_time_valid=false;
weaysy::ThemeSnapshot last_snapshot{};
using CompileFn = HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
CompileFn compile=nullptr;

void log(const char* fmt,...) {
    char buffer[2048]; va_list args; va_start(args,fmt);
    int n=vsnprintf(buffer,sizeof(buffer)-3,fmt,args); va_end(args);
    if(n<0) return; if(n>int(sizeof(buffer)-3)) n=int(sizeof(buffer)-3);
    buffer[n++]='\r'; buffer[n++]='\n';
    std::lock_guard<std::mutex> guard(log_lock);
    if(log_file!=INVALID_HANDLE_VALUE) { DWORD wrote; WriteFile(log_file,buffer,n,&wrote,nullptr); }
}

std::string sha256(const void* data,size_t bytes) {
    BCRYPT_ALG_HANDLE algorithm=nullptr; unsigned char digest[32]{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
    NTSTATUS result=BCryptHash(algorithm,nullptr,0,(PUCHAR)data,ULONG(bytes),digest,sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(result<0) return {};
    char hex[65]{}; for(size_t i=0;i<32;i++) sprintf(hex+2*i,"%02x",digest[i]);
    return hex;
}

bool compile_source(const char* source,std::vector<unsigned char>& output) {
    if(!compile) return false;
    ID3DBlob *bytecode=nullptr,*errors=nullptr;
    HRESULT hr=compile(source,strlen(source),"WeaysyOwnPalette",nullptr,nullptr,"main","ps_5_0",
                       D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytecode,&errors);
    if(errors) { log("HLSL: %.*s",int(errors->GetBufferSize()),(const char*)errors->GetBufferPointer()); errors->Release(); }
    if(FAILED(hr)||!bytecode) { log("Own HLSL compilation failed: 0x%08lx",(unsigned long)hr); return false; }
    auto p=(const unsigned char*)bytecode->GetBufferPointer();
    output.assign(p,p+bytecode->GetBufferSize()); bytecode->Release(); return true;
}

void on_init_device(api::device* device) {
    if(device->get_api()!=api::device_api::d3d11) { log("Unsupported graphics API; no changes."); return; }
    std::lock_guard<std::recursive_mutex> guard(lock);
    DeviceState state{};
    const char* sources[4][2]={{kUiDay,kUiNight},{kDryDay,kDryNight},{kTexDay,kTexNight},{kFontDay,kFontNight}};
    internal_call=true;
    bool ok=true;
    for(int kind=0;kind<4 && ok;kind++) for(int night=0;night<2;night++) {
        std::vector<unsigned char> bytes;
        if(!compile_source(sources[kind][night],bytes)) {ok=false;break;}
        api::shader_desc shader{bytes.data(),bytes.size()};
        api::pipeline_subobject sub{api::pipeline_subobject_type::pixel_shader,1,&shader};
        if(!device->create_pipeline({},1,&sub,&state.shaders[kind][night])) {ok=false;break;}
    }
    if(!ok) {
        for(auto& pair:state.shaders) for(auto pipeline:pair) if(pipeline.handle) device->destroy_pipeline(pipeline);
        state={};
    }
    state.ready=ok; devices[device]=state; internal_call=false;
    log("DX11 device ready=%u; own eight palette/font shaders compiled=%u",unsigned(ok),unsigned(ok));
}

void on_destroy_device(api::device* device) {
    std::lock_guard<std::recursive_mutex> guard(lock);
    auto found=devices.find(device); if(found==devices.end()) return;
    internal_call=true;
    for(auto& pair:found->second.shaders) for(auto pipeline:pair) if(pipeline.handle) device->destroy_pipeline(pipeline);
    internal_call=false; devices.erase(found);
    for(auto it=native_shaders.begin();it!=native_shaders.end();) {
        if(it->second.owner==device) it=native_shaders.erase(it); else ++it;
    }
    for(auto it=commands.begin();it!=commands.end();) {
        if(it->second.owner==device) it=commands.erase(it); else ++it;
    }
}

void on_init_pipeline(api::device* device,api::pipeline_layout,uint32_t count,
                      const api::pipeline_subobject* objects,api::pipeline pipeline) {
    if(internal_call || device->get_api()!=api::device_api::d3d11) return;
    for(uint32_t i=0;i<count;i++) {
        if(objects[i].type!=api::pipeline_subobject_type::pixel_shader || objects[i].count!=1) continue;
        const auto& shader=*static_cast<const api::shader_desc*>(objects[i].data);
        if(!shader.code || shader.code_size>1000000) continue;
        bool size_candidate=false;
        for(const auto& f:kNativeFingerprints) if(shader.code_size==f.bytes) size_candidate=true;
        if(!size_candidate) continue;
        auto digest=sha256(shader.code,shader.code_size);
        for(const auto& f:kNativeFingerprints) {
            if(shader.code_size==f.bytes && digest==f.sha256) {
                std::lock_guard<std::recursive_mutex> guard(lock);
                native_shaders[pipeline.handle]={device,f.kind};
                log("Exact native PS match kind=%d pipeline=%llx sha256=%s",f.kind,
                    (unsigned long long)pipeline.handle,digest.c_str());
                return;
            }
        }
    }
}

void on_destroy_pipeline(api::device*,api::pipeline pipeline) {
    if(internal_call) return;
    std::lock_guard<std::recursive_mutex> guard(lock);
    native_shaders.erase(pipeline.handle);
    for(auto& [cmd,state]:commands) if(state.native==pipeline.handle) {state.kind=0;state.native=0;state.bound=0;}
}

void on_bind_pipeline(api::command_list* cmd,api::pipeline_stage stages,api::pipeline pipeline) {
    if(internal_call || (uint32_t(stages)&uint32_t(api::pipeline_stage::pixel_shader))==0) return;
    std::lock_guard<std::recursive_mutex> guard(lock);
    auto& state=commands[cmd];
    state.owner=cmd->get_device();
    auto found=native_shaders.find(pipeline.handle);
    state.kind=found==native_shaders.end()?0:found->second.kind;
    state.native=pipeline.handle; state.bound=pipeline.handle;
}

void on_rt(api::command_list* cmd,uint32_t count,const api::resource_view* rtvs,api::resource_view) {
    if(internal_call) return;
    std::lock_guard<std::recursive_mutex> guard(lock);
    commands[cmd].owner=cmd->get_device(); commands[cmd].rtv=count?rtvs[0].handle:0;
}

void on_viewports(api::command_list* cmd,uint32_t first,uint32_t count,const api::viewport* viewports) {
    if(internal_call || first || !count) return;
    std::lock_guard<std::recursive_mutex> guard(lock);
    commands[cmd].owner=cmd->get_device(); commands[cmd].width=viewports[0].width; commands[cmd].height=viewports[0].height;
}

bool before_draw(api::command_list* cmd) {
    if(internal_call) return false;
    std::lock_guard<std::recursive_mutex> guard(lock);
    auto current=commands.find(cmd); if(current==commands.end() || current->second.kind<1) return false;
    auto device=devices.find(cmd->get_device()); if(device==devices.end() || !device->second.ready) return false;
    auto& state=current->second; unsigned night=phase.load(std::memory_order_relaxed);
    auto replacement=device->second.shaders[state.kind-1][night];
    if(replacement.handle && state.bound!=replacement.handle) {
        internal_call=true;
        cmd->bind_pipeline(api::pipeline_stage::pixel_shader,replacement);
        internal_call=false; state.bound=replacement.handle;
    }
    calls[state.kind-1]++;
    if(trace_budget) {
        --trace_budget;
        log("DRAW frame=%llu kind=%d phase=%u native=%llx replacement=%llx rtv=%llx viewport=%.0fx%.0f",
            (unsigned long long)frame_number,state.kind,night,(unsigned long long)state.native,
            (unsigned long long)replacement.handle,(unsigned long long)state.rtv,state.width,state.height);
    }
    return false; // Original native draw still executes, with all geometry/controls intact.
}
bool on_draw(api::command_list* cmd,uint32_t,uint32_t,uint32_t,uint32_t) { return before_draw(cmd); }
bool on_draw_indexed(api::command_list* cmd,uint32_t,uint32_t,uint32_t,int32_t,uint32_t) {return before_draw(cmd);}
void on_destroy_command(api::command_list* cmd) {std::lock_guard<std::recursive_mutex> guard(lock);commands.erase(cmd);}

void sample_time() {
    if(!mapped) {
        wchar_t name[64]; weaysy::mapping_name(GetCurrentProcessId(),name);
        mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
        if(mapping) mapped=(const weaysy::ThemeSnapshot*)MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(weaysy::ThemeSnapshot));
        if(mapping && !mapped) {CloseHandle(mapping);mapping=nullptr;}
    }
    weaysy::ThemeSnapshot snapshot{};
    bool valid=weaysy::read_theme_snapshot(mapped,snapshot);
    const uint32_t required=weaysy::publisher_running|weaysy::has_valid_time|weaysy::time_currently_available;
    if(valid) valid=(snapshot.flags&required)==required && GetTickCount64()>=snapshot.publish_tick_ms &&
                    GetTickCount64()-snapshot.publish_tick_ms<5000;
    // Keep the last genuinely observed theme during pause/load or unavailable
    // samples. Initially DAY. Never extrapolate time or claim a stale sample valid.
    unsigned next=valid?snapshot.theme:phase.load(std::memory_order_relaxed);
    if(valid!=last_time_valid || (valid && (snapshot.generation!=last_snapshot.generation || snapshot.minute_of_day!=last_snapshot.minute_of_day))) {
        log("CLOCK frame=%llu valid=%u minute=%u phase=%u generation=%u (one snapshot for both GPS views)",
             (unsigned long long)frame_number,unsigned(valid),valid?snapshot.minute_of_day:0,next,valid?snapshot.generation:0);
    }
    if(next!=phase.load() || valid!=last_time_valid) trace_budget=15;
    phase.store(next,std::memory_order_relaxed); last_time_valid=valid; last_snapshot=snapshot;
}

void on_present(api::command_queue*,api::swapchain*,const api::rect*,const api::rect*,uint32_t,const api::rect*) {
    std::lock_guard<std::recursive_mutex> guard(lock);
    ++frame_number;
    sample_time();
    if(frame_number%600==0) {
        log("STATS frame=%llu draws ui=%llu dry=%llu tex=%llu font=%llu matched_native_ps=%zu clock_valid=%u",
            (unsigned long long)frame_number,(unsigned long long)calls[0],(unsigned long long)calls[1],
            (unsigned long long)calls[2],(unsigned long long)calls[3],native_shaders.size(),unsigned(last_time_valid));
    }
}
} // namespace

extern "C" __declspec(dllexport) bool AddonInit(HMODULE module,HMODULE reshade_module) {
    self_module=module;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(module,path,MAX_PATH);
    wchar_t* slash=wcsrchr(path,L'\\'); if(slash) wcscpy(slash+1,L"WeaysyRenderProbe.log");
    log_file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    log("START local research graphics add-on v0.0.5 pid=%lu; dark/green map-font day/night; no native code patching",GetCurrentProcessId());
    compiler_module=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    compile=compiler_module?(CompileFn)GetProcAddress(compiler_module,"D3DCompile"):nullptr;
    if(!compile || !reshade::register_addon(module,reshade_module)) {
        log("Initialization failed; add-on not registered.");
        if(compiler_module) {FreeLibrary(compiler_module);compiler_module=nullptr;}
        if(log_file!=INVALID_HANDLE_VALUE) {CloseHandle(log_file);log_file=INVALID_HANDLE_VALUE;}
        return false;
    }
#define REGISTER(ev,fn) reshade::register_event<reshade::addon_event::ev>(fn)
    REGISTER(init_device,on_init_device); REGISTER(destroy_device,on_destroy_device);
    REGISTER(init_pipeline,on_init_pipeline); REGISTER(destroy_pipeline,on_destroy_pipeline);
    REGISTER(bind_pipeline,on_bind_pipeline); REGISTER(bind_render_targets_and_depth_stencil,on_rt);
    REGISTER(bind_viewports,on_viewports); REGISTER(draw,on_draw); REGISTER(draw_indexed,on_draw_indexed);
    REGISTER(destroy_command_list,on_destroy_command); REGISTER(present,on_present);
#undef REGISTER
    log("REGISTERED exact shader hashes; 23 markerRGBA colors + known outline-map font signatures/dark-neutral or native teal city label gate; no fullscreen postfilter.");
    return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE module,HMODULE reshade_module) {
    reshade::unregister_addon(module,reshade_module);
    log("STOP frame=%llu native matches=%zu",(unsigned long long)frame_number,native_shaders.size());
    if(mapped) {UnmapViewOfFile(mapped);mapped=nullptr;}
    if(mapping) {CloseHandle(mapping);mapping=nullptr;}
    if(compiler_module) {FreeLibrary(compiler_module);compiler_module=nullptr;}
    if(log_file!=INVALID_HANDLE_VALUE) {CloseHandle(log_file);log_file=INVALID_HANDLE_VALUE;}
}

BOOL WINAPI DllMain(HMODULE,DWORD,LPVOID) {return TRUE;}
