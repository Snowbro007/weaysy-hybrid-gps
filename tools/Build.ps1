param([Parameter(Mandatory=$true)][string]$ZigPath)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = Join-Path $repo 'build'
$dist = Join-Path $repo 'dist/Weaysy-Hybrid-GPS-ATS-0.1.0-preview.1'
if (Test-Path -LiteralPath (Join-Path $dist 'WeaysySetup.exe')) { throw 'Output already exists; use a fresh build directory.' }
New-Item -ItemType Directory -Force -Path $build, (Join-Path $dist 'payload/plugins') | Out-Null
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $build 'zig-cache'
$clock = Join-Path $repo 'src/time_reader'
$sdk = Join-Path $repo 'third_party/scs_sdk/include'
$reshade = Join-Path $repo 'third_party/reshade/include'
function Compile([string[]]$Arguments) {
    & $ZigPath c++ @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed ($LASTEXITCODE)." }
}
Write-Output 'Building the two theme-only companion modules...'
Compile @('-target','x86_64-windows-gnu','-std=c++17','-O2','-s','-shared','-Wall','-Wextra','-Werror','-I',"$clock/include",'-I',$sdk,"$clock/src/time_reader.cpp","$clock/time_reader.def",'-luser32','-ladvapi32','-o',"$dist/payload/plugins/WeaysyTimeReader.dll")
Compile @('-target','x86_64-windows-gnu','-std=c++20','-O2','-s','-shared','-fms-extensions','-fno-exceptions','-I',$reshade,"$repo/src/theme_graphics/render_probe.cpp",'-lbcrypt','-luser32','-o',"$dist/payload/WeaysyRenderProbe.addon64")
Copy-Item -LiteralPath "$clock/WeaysyTimeReader.ini" -Destination "$dist/payload/plugins/WeaysyTimeReader.ini"
Copy-Item -LiteralPath "$repo/assets/weaysy_ats_hybrid_gps.scs" -Destination $dist

Write-Output 'Running clock policy, timezone, restart and IPC tests...'
Compile @('-target','x86_64-windows-gnu','-std=c++17','-O2','-s',"$clock/tests/policy_tests.cpp",'-I',"$clock/include",'-o',"$build/policy_tests.exe")
Compile @('-target','x86_64-windows-gnu','-std=c++17','-O2','-s',"$clock/tests/native_clock_tests.cpp",'-I',"$clock/include",'-ladvapi32','-o',"$build/native_clock_tests.exe")
Compile @('-target','x86_64-windows-gnu','-std=c++17','-O2','-s',"$clock/src/clock_cli.cpp",'-I',"$clock/include",'-luser32','-o',"$build/clock_cli.exe")
Compile @('-target','x86_64-windows-gnu','-std=c++17','-O2','-s',"$clock/tests/mock_sdk_host.cpp",'-I',"$clock/include",'-I',$sdk,'-luser32','-o',"$build/mock_sdk_host.exe")
$results = @()
foreach ($name in @('policy_tests','native_clock_tests')) {
    $results += (& "$build/$name.exe" | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw "$name failed." }
}
$results += (& "$build/mock_sdk_host.exe" "$dist/payload/plugins/WeaysyTimeReader.dll" "$build/clock_cli.exe" | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'SDK/IPC tests failed.' }
$results | Write-Output
foreach ($pair in @(@("$dist/payload/render_probe.lib","$build/render_probe.lib"),@("$dist/payload/plugins/time_reader.lib","$build/time_reader.lib"),@("$dist/payload/plugins/WeaysyTimeReader.log","$build/mock-clock.log"))) {
    if (Test-Path -LiteralPath $pair[0]) { Move-Item -LiteralPath $pair[0] -Destination $pair[1] -Force }
}

Write-Output 'Checking all eight original shaders on DX11 WARP...'
Compile @('-target','x86_64-windows-gnu','-std=c++17','-O2','-s',"$repo/tests/shader_smoke.cpp",'-ld3dcompiler_47','-ld3d11','-o',"$build/shader_smoke.exe")
$shaderResult = (& "$build/shader_smoke.exe" | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Shader checks failed.' }
$shaderResult | Write-Output
@{passed=$true;result=$shaderResult;actual_game_loaded=$false} | ConvertTo-Json | Set-Content -LiteralPath "$build/shader-tests.json" -Encoding utf8

$oldHashes = @{
 'WeaysyRenderProbe.addon64' = 'c9e645d11af185471d51dba523878e073402e784e34e6e571d5c328b8a8fbd6d'
 'plugins/WeaysyTimeReader.dll' = '68c2c17ef6de41778ea73261a0ec99ad61cc01acfb62858b2ca81e4ec027da29'
 'plugins/WeaysyTimeReader.ini' = 'f63c6a49eec7e4a5936bc2b5434bd3125ff5890caeff362b182d934c1e525a7e'
}
$header = @('// SPDX-License-Identifier: MIT','#pragma once','struct PackageFile { const wchar_t* relative; const char* sha256; const char* previous_sha256; };','inline constexpr PackageFile package_files[] = {')
foreach ($name in @('WeaysyRenderProbe.addon64','plugins/WeaysyTimeReader.dll','plugins/WeaysyTimeReader.ini')) {
    $hash = (Get-FileHash -LiteralPath "$dist/payload/$name").Hash.ToLowerInvariant()
    $header += ' {L"' + $name + '","' + $hash + '","' + $oldHashes[$name] + '"},'
}
$header += '};'
[IO.File]::WriteAllText("$build/package_files.h", ($header -join "`n"), [Text.UTF8Encoding]::new($false))
Compile @('-target','x86_64-windows-gnu','-std=c++20','-O2','-s','-municode','-I',$build,"$repo/src/installer/portable_setup.cpp",'-lbcrypt','-luser32','-lshell32','-lole32','-ladvapi32','-o',"$dist/WeaysySetup.exe")
Copy-Item -LiteralPath "$repo/README.md","$repo/LICENSE","$repo/NOTICE.md" -Destination $dist
Copy-Item -LiteralPath "$repo/licenses" -Destination $dist -Recurse
Copy-Item -LiteralPath "$repo/docs" -Destination $dist -Recurse
New-Item -ItemType Directory -Force -Path "$dist/licenses/third_party" | Out-Null
Copy-Item -LiteralPath "$repo/third_party/scs_sdk/LICENSE.txt" -Destination "$dist/licenses/third_party/SCS-SDK.txt"
Copy-Item -LiteralPath "$repo/third_party/reshade/LICENSE.txt" -Destination "$dist/licenses/third_party/ReShade-API.txt"
foreach ($pair in @(@('CHECK','--check'),@('DISABLE','--disable'))) {
    [IO.File]::WriteAllText("$dist/$($pair[0]).cmd", "@echo off`r`ncd /d `"%~dp0`"`r`nWeaysySetup.exe $($pair[1])`r`npause`r`n",[Text.Encoding]::ASCII)
}
@{passed=$true;scope='Clock policy, native timezone layout and mock SDK/IPC; no actual game drive';results=$results;compiler=((& $ZigPath version)|Out-String).Trim()} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$build/clock-tests.json" -Encoding utf8
Write-Output "Built: $dist"
