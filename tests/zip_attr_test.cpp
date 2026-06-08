// Introspect a SUB-FOLDER inside a host folder (zip OR our archive): print the
// full SFGAO attribute set + the parsing names. Run on a .zip and on our .dat
// and diff — the difference is why DefView navigates into zip but not us.
// Usage: zip_attr_test.exe <archive> <subFolderName>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

static void dumpAttrs(SFGAOF a) {
    struct { SFGAOF bit; const wchar_t* n; } t[] = {
        {SFGAO_CANCOPY,L"CANCOPY"},{SFGAO_CANMOVE,L"CANMOVE"},{SFGAO_CANLINK,L"CANLINK"},
        {SFGAO_STORAGE,L"STORAGE"},{SFGAO_CANRENAME,L"CANRENAME"},{SFGAO_CANDELETE,L"CANDELETE"},
        {SFGAO_HASPROPSHEET,L"HASPROPSHEET"},{SFGAO_DROPTARGET,L"DROPTARGET"},
        {SFGAO_ENCRYPTED,L"ENCRYPTED"},{SFGAO_ISSLOW,L"ISSLOW"},{SFGAO_GHOSTED,L"GHOSTED"},
        {SFGAO_LINK,L"LINK"},{SFGAO_SHARE,L"SHARE"},{SFGAO_READONLY,L"READONLY"},{SFGAO_HIDDEN,L"HIDDEN"},
        {SFGAO_FILESYSANCESTOR,L"FILESYSANCESTOR"},{SFGAO_FOLDER,L"FOLDER"},{SFGAO_FILESYSTEM,L"FILESYSTEM"},
        {SFGAO_HASSUBFOLDER,L"HASSUBFOLDER"},{SFGAO_STREAM,L"STREAM"},{SFGAO_STORAGEANCESTOR,L"STORAGEANCESTOR"},
        {SFGAO_BROWSABLE,L"BROWSABLE"},{SFGAO_NONENUMERATED,L"NONENUMERATED"},{SFGAO_NEWCONTENT,L"NEWCONTENT"},
    };
    wprintf(L"  attrs=0x%08X:", a);
    for (auto& x : t) if (a & x.bit) wprintf(L" %s", x.n);
    wprintf(L"\n");
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { wprintf(L"usage: zip_attr_test <archive> <subFolder>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE p = nullptr;
    if (FAILED(SHParseDisplayName(argv[1], nullptr, &p, 0, nullptr))) { wprintf(L"parse fail\n"); return 1; }
    IShellFolder* root = nullptr;
    if (FAILED(SHBindToObject(nullptr, p, nullptr, IID_IShellFolder, (void**)&root))) { wprintf(L"bind fail\n"); return 1; }

    IEnumIDList* e = nullptr; root->EnumObjects(nullptr, SHCONTF_FOLDERS|SHCONTF_NONFOLDERS, &e);
    PITEMID_CHILD c = nullptr, found = nullptr; ULONG f = 0;
    while (e->Next(1,&c,&f)==S_OK && f==1) {
        STRRET sr; wchar_t nm[260]; root->GetDisplayNameOf(c, SHGDN_NORMAL, &sr); StrRetToBufW(&sr,c,nm,260);
        if (_wcsicmp(nm, argv[2])==0) { found=(PITEMID_CHILD)ILClone(c); CoTaskMemFree(c); break; }
        CoTaskMemFree(c);
    }
    e->Release();
    if (!found) { wprintf(L"'%s' not found\n", argv[2]); return 1; }

    wprintf(L"=== sub-folder '%s' in %s ===\n", argv[2], argv[1]);
    SFGAOF a = 0xFFFFFFFF; root->GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)&found, &a);
    dumpAttrs(a);

    STRRET sr; wchar_t buf[1024];
    root->GetDisplayNameOf(found, SHGDN_INFOLDER, &sr); StrRetToBufW(&sr, found, buf, 1024);
    wprintf(L"  name INFOLDER          = '%s'\n", buf);
    root->GetDisplayNameOf(found, SHGDN_FORPARSING|SHGDN_INFOLDER, &sr); StrRetToBufW(&sr, found, buf, 1024);
    wprintf(L"  name FORPARSING|INFOLDER = '%s'\n", buf);
    if (SUCCEEDED(root->GetDisplayNameOf(found, SHGDN_FORPARSING, &sr))) { StrRetToBufW(&sr, found, buf, 1024); wprintf(L"  name FORPARSING         = '%s'\n", buf); }
    else wprintf(L"  name FORPARSING         = <failed>\n");

    // does binding the child as a folder work?
    IShellFolder* sub = nullptr;
    HRESULT hr = root->BindToObject(found, nullptr, IID_IShellFolder, (void**)&sub);
    wprintf(L"  BindToObject(IShellFolder) hr=0x%08X\n", hr);
    if (sub) sub->Release();
    return 0;
}
