// SPDX-License-Identifier: MIT
// Compile all original replacement shaders and create them on a WARP DX11 device.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include "../src/theme_graphics/generated_shaders.h"
int main() {
 ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr;
 HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
 if(FAILED(hr)){std::printf("WARP failed %lx\n",hr);return 1;}
 const char* shaders[]={kUiDay,kUiNight,kDryDay,kDryNight,kTexDay,kTexNight,kFontDay,kFontNight};
 for(unsigned i=0;i<8;++i){
  ID3DBlob *code=nullptr,*errors=nullptr;
  hr=D3DCompile(shaders[i],std::strlen(shaders[i]),"WeaysyOwnShader",nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
  if(errors){std::printf("%.*s",int(errors->GetBufferSize()),static_cast<const char*>(errors->GetBufferPointer()));errors->Release();}
  if(FAILED(hr)||!code){std::printf("Shader %u compile failed\n",i);return 2;}
  ID3D11PixelShader* shader=nullptr;
  hr=device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader);
  code->Release();if(FAILED(hr)||!shader){std::printf("Shader %u create failed\n",i);return 3;}
  shader->Release();
 }
 context->Release();device->Release();
 std::puts("PASS 8 original day/night pixel shaders compiled and created on DX11 WARP");
 return 0;
}
