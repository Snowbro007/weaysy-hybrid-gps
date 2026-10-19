// SPDX-License-Identifier: MIT
#pragma once
struct NativeFingerprint { size_t bytes; int kind; const char* sha256; };
inline constexpr NativeFingerprint kNativeFingerprints[] = {
 {376,3,"36fb3081f9fdfb4caca9f20875581911941da4f4f9c846f34313cae698b33f40"},
 {248,2,"52a9154c4fa05167001be47010d073848356cac1094a9a00374e98acbafabe8a"},
 {252,2,"7c7bb4086f0852804a8d85ad4c5c02d0c0cadbd2ff5f05b7c5b5b3d629768b9f"},
 {412,1,"86f5f00b3601248bac9a26a23ea899152789f5bf1c1620537cc099a83271b221"},
 {424,1,"e10bb0dc2040752eb6b580ea60adc8842ae5f097513b03c6c7a5dab7d0c06925"},
 {364,3,"e5d342d016615dfb5728052367dee18e6173ba027e024d6c1bb8fcc41844b822"},
 {2460,4,"f5cd9f911a79f4571e40dd3d8a59e6ca5bff4febf76f8fae978b143c9ece2494"},
 {2448,4,"f6985f3fbeefa8d9aeb69ffc9d416e49b84be4fa31f4c7b2f108b3515a4bd1c8"},
};
