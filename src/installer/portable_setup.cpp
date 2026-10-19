// SPDX-License-Identifier: MIT
// Windows x64 companion updater. No Python, administrator or SDK setting needed.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdio>
#include "package_files.h"
namespace fs=std::filesystem;
const char* game_hash="c60e8ee02cc97b3cc6ef8c6298bb5c3c23c4ffd330b8185b646fb1b608a3c7b4";
const char* reshade_hash="0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7";

std::string sha(const fs::path& path){
 std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read file for verification");
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 unavailable");
 if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(algorithm,0);throw std::runtime_error("SHA256 unavailable");}
 char buffer[65536];bool ok=true;
 while(in){in.read(buffer,sizeof(buffer));auto bytes=in.gcount();if(bytes&&BCryptHashData(hash,(PUCHAR)buffer,ULONG(bytes),0)<0)ok=false;}
 unsigned char result[32];if(BCryptFinishHash(hash,result,32,0)<0)ok=false;
 BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
 if(!ok||in.bad())throw std::runtime_error("File verification failed");
 char text[65]{};for(unsigned i=0;i<32;++i)std::sprintf(text+2*i,"%02x",result[i]);return text;
}
bool same_path(const fs::path& a,const fs::path& b){return _wcsicmp(fs::weakly_canonical(a).c_str(),fs::weakly_canonical(b).c_str())==0;}
bool running(const fs::path& executable){
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot check running games");
 PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
 if(Process32FirstW(snapshot,&entry))do{
  if(_wcsicmp(entry.szExeFile,L"amtrucks.exe"))continue;
  HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
  wchar_t path[32768];DWORD chars=32768;
  if(!process){found=true;break;}
  if(!QueryFullProcessImageNameW(process,0,path,&chars))found=true;
  else if(same_path(path,executable))found=true;
  CloseHandle(process);if(found)break;
 }while(Process32NextW(snapshot,&entry));
 CloseHandle(snapshot);return found;
}
fs::path autodetect(){
 wchar_t steam[32768]{};DWORD bytes=sizeof(steam);
 if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,steam,&bytes)!=ERROR_SUCCESS)return {};
 std::vector<fs::path> libraries{steam};
 std::ifstream in(fs::path(steam)/L"steamapps/libraryfolders.vdf");std::string line;
 std::regex path_line("\\\"path\\\"\\s+\\\"([^\\\"]+)\\\"");std::smatch match;
 while(std::getline(in,line))if(std::regex_search(line,match,path_line)){
  std::string value=match[1];size_t offset=0;while((offset=value.find("\\\\",offset))!=std::string::npos)value.replace(offset,2,"\\");
  libraries.push_back(fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size())));
 }
 for(const auto& library:libraries){auto game=library/L"steamapps/common/American Truck Simulator";if(fs::exists(game/L"bin/win_x64/amtrucks.exe"))return game;}
 return {};
}
fs::path choose_folder(){
 BROWSEINFOW info{};info.lpszTitle=L"Select the American Truck Simulator installation folder";info.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
 auto* selected=SHBrowseForFolderW(&info);if(!selected)return {};
 wchar_t path[MAX_PATH]{};bool ok=SHGetPathFromIDListW(selected,path)!=FALSE;CoTaskMemFree(selected);return ok?fs::path(path):fs::path{};
}
std::string timestamp(){SYSTEMTIME t{};GetLocalTime(&t);char out[64];std::sprintf(out,"hybrid-%04u%02u%02u-%02u%02u%02u-%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());return out;}
bool is_config(const PackageFile& file){return !std::wcscmp(file.relative,L"plugins/WeaysyTimeReader.ini");}
bool allowed_old(const PackageFile& file,const std::string& hash){return is_config(file)||hash==file.sha256||hash==file.previous_sha256;}

int wmain(int argc,wchar_t** argv){
 bool gui=argc==1;
 HANDLE exclusive=CreateMutexW(nullptr,FALSE,L"Local\\WeaysyHybridSetupV010");
 struct CloseMutex{HANDLE handle;~CloseMutex(){if(handle)CloseHandle(handle);}} close_mutex{exclusive};
 if(!exclusive||GetLastError()==ERROR_ALREADY_EXISTS){std::fprintf(stderr,"Another Weaysy installer is running\n");return 1;}
 try{
  std::wstring mode=argc>1?argv[1]:L"--install";
  if(mode!=L"--install"&&mode!=L"--check"&&mode!=L"--disable")throw std::runtime_error("Use --check, --install or --disable [game folder]");
  wchar_t self[32768];GetModuleFileNameW(nullptr,self,32768);fs::path package=fs::path(self).parent_path();
  fs::path game=argc>2?fs::path(argv[2]):autodetect();
  if(game.empty()&&gui){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);game=choose_folder();}
  if(game.empty())throw std::runtime_error("ATS not found. Supply its installation folder.");
  fs::path bin=fs::weakly_canonical(game/L"bin/win_x64");
  if(!fs::exists(bin/L"amtrucks.exe")||(mode!=L"--disable"&&sha(bin/L"amtrucks.exe")!=game_hash))throw std::runtime_error("Unsupported ATS build. Hybrid Preview 0.1.0 supports only ATS 1.61.3.18 Windows x64. No files changed.");
  for(const auto& file:package_files)if(sha(package/L"payload"/file.relative)!=file.sha256)throw std::runtime_error("Damaged companion package. No files changed.");
  if(mode==L"--check"){
   unsigned matching=0;for(const auto& file:package_files)if(fs::exists(bin/file.relative)&&sha(bin/file.relative)==file.sha256)++matching;
   std::printf("CHECK game_supported=1 payload_verified=1 installed_matching=%u total=%zu game_running=%u\n",matching,std::size(package_files),unsigned(running(bin/L"amtrucks.exe")));
   return 0;
  }
  if(gui&&MessageBoxW(nullptr,L"Install or update the Weaysy Hybrid Preview 0.1.0 companion?\n\nClose ATS first. Known Weaysy files are backed up. Other mods and saves are left in place.\n\nThis is an experimental preview for ATS 1.61.3.18 Windows x64 / DX11.",L"Weaysy companion Hybrid Preview 0.1.0",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;
  if(running(bin/L"amtrucks.exe"))throw std::runtime_error("Close ATS completely before changing the companion. No files changed.");
  for(const auto& file:package_files){
   auto target=bin/file.relative;
   if(fs::exists(fs::path(target.wstring()+L".weaysy-new")))throw std::runtime_error("A pending update file already exists. No files changed.");
   if(fs::exists(target)&&!allowed_old(file,sha(target)))throw std::runtime_error("An unfamiliar existing Weaysy file was found. Refusing to overwrite or disable it.");
   auto inactive=fs::path(target.wstring()+L".weaysy-disabled");
   if(fs::exists(inactive)&&(fs::exists(target)||!allowed_old(file,sha(inactive))))throw std::runtime_error("An unfamiliar or conflicting disabled file exists. No files changed.");
   if(mode==L"--disable"&&fs::exists(inactive))throw std::runtime_error("Companion is already disabled. Use --install to enable the updated files.");
  }
  if(mode==L"--install"&&(fs::exists(bin/L"WeaysyIncidentMarkers.addon64")||fs::exists(bin/L"plugins/WeaysyIncidentTelemetry.dll")))throw std::runtime_error("Old incident research plugins found. Disable them before installing this theme-only preview.");
  fs::path runtime=bin/L"dxgi.dll";
  bool restore_runtime=false;
  if(mode==L"--install"){
   if(fs::exists(runtime)&&sha(runtime)!=reshade_hash)throw std::runtime_error("Unrecognized ReShade loader. No files changed; install the tested official full-add-on version first.");
   if(!fs::exists(runtime))throw std::runtime_error("ReShade 6.8 full-add-on loader required. Install it from reshade.me first; this package does not redistribute it.");
  }
  fs::path backup=bin/L"Weaysy-backups"/timestamp();fs::create_directories(backup);
  struct Change{fs::path target,saved;bool existed=false,inactive=false;};std::vector<Change> changes;
  try{
   for(const auto& file:package_files){
    auto target=bin/file.relative;auto inactive=fs::path(target.wstring()+L".weaysy-disabled");
    bool existed=fs::exists(target),was_inactive=fs::exists(inactive);if(mode==L"--disable"&&!existed)continue;
    auto saved=backup/file.relative;
    if(existed||was_inactive){auto old=existed?target:inactive;fs::create_directories(saved.parent_path());fs::copy_file(old,saved);if(sha(saved)!=sha(old))throw std::runtime_error("Backup verification failed");}
    changes.push_back({target,saved,existed,was_inactive});
    if(mode==L"--disable"){fs::rename(target,fs::path(target.wstring()+L".weaysy-disabled"));continue;}
    fs::create_directories(target.parent_path());
    auto temporary=fs::path(target.wstring()+L".weaysy-new");
    if(fs::exists(temporary))throw std::runtime_error("A pending update file already exists");
    auto input=(is_config(file)&&(existed||was_inactive))?(existed?target:inactive):(package/L"payload"/file.relative);
    fs::copy_file(input,temporary);
    if(sha(temporary)!=(is_config(file)?sha(input):file.sha256))throw std::runtime_error("New file verification failed");
    if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot commit update");
   }

   for(const auto& change:changes)if(change.inactive)fs::rename(fs::path(change.target.wstring()+L".weaysy-disabled"),fs::path(change.saved.wstring()+L".was-disabled"));
  }catch(...){
   // Undo only changes recorded by this transaction. Leave every saved backup.
   for(auto it=changes.rbegin();it!=changes.rend();++it){std::error_code error;
    if(it->existed)fs::copy_file(it->saved,it->target,fs::copy_options::overwrite_existing,error);
    else fs::remove(it->target,error);
    fs::remove(fs::path(it->target.wstring()+L".weaysy-new"),error);
    if(it->inactive)fs::copy_file(it->saved,fs::path(it->target.wstring()+L".weaysy-disabled"),fs::copy_options::overwrite_existing,error);
    else fs::remove(fs::path(it->target.wstring()+L".weaysy-disabled"),error);
   }
   if(restore_runtime){std::error_code error;fs::remove(runtime,error);}throw;
  }
  std::ofstream status(backup/L"README.txt");status<<"Weaysy Hybrid Preview 0.1.0: "<<(mode==L"--install"?"installed":"disabled")<<"\nBackups of pre-existing files are retained in this folder.\n";
  auto backup_text=backup.u8string();
  std::printf("SUCCESS %s files=%zu backup=%s\n",mode==L"--install"?"installed":"disabled",changes.size(),reinterpret_cast<const char*>(backup_text.c_str()));
  if(gui)MessageBoxW(nullptr,L"Companion update complete. Start ATS normally through Steam.\n\nCopy the included SCS to your ATS mod folder, then activate only this GPS theme.",L"Weaysy Hybrid Preview 0.1.0",MB_OK|MB_ICONINFORMATION);
  return 0;
 }catch(const std::exception& error){std::fprintf(stderr,"ERROR: %s\n",error.what());if(gui)MessageBoxA(nullptr,error.what(),"Weaysy Hybrid Preview 0.1.0",MB_OK|MB_ICONERROR);return 1;}
}
