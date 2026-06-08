#pragma once
#include <initguid.h>

// COM class id of the Fox archive namespace extension.
// {5AA92D71-4013-463E-BFDE-673DB7C70FCF}
DEFINE_GUID(CLSID_FoxShellFolder,
    0x5aa92d71, 0x4013, 0x463e, 0xbf, 0xde, 0x67, 0x3d, 0xb7, 0xc7, 0x0f, 0xcf);

#define FOX_CLSID_STR L"{5AA92D71-4013-463E-BFDE-673DB7C70FCF}"
#define FOX_FRIENDLY  L"MGSV Fox Archive"

// COM class id of the .ftex texture thumbnail provider.
// {A57C06D6-E969-484F-A708-9B73BF3B861D}
DEFINE_GUID(CLSID_FoxThumbProvider,
    0xa57c06d6, 0xe969, 0x484f, 0xa7, 0x08, 0x9b, 0x73, 0xbf, 0x3b, 0x86, 0x1d);

#define FOX_THUMB_CLSID_STR L"{A57C06D6-E969-484F-A708-9B73BF3B861D}"
