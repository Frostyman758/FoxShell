#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>

// Create an IDataObject for the selected items that actually carries their
// bytes, so dropping / pasting into a real Explorer folder (or any consumer of
// virtual files — Outlook, upload dialogs, etc.) extracts them on demand.
//
// The stock SHCreateDataObject only advertises a virtual PIDL list, which a
// filesystem drop target can't turn into files; this object instead exposes the
// standard CFSTR_FILEDESCRIPTOR + CFSTR_FILECONTENTS pair (the same mechanism
// zip folders and mail attachments use). Folders in the selection are walked
// recursively; a nested archive is copied as its own container file.
//
// archivePath/chain/dirPath describe the folder the items live in (mirrors the
// FoxShellFolder fields); apidl are the selected children.
HRESULT CreateFoxDataObject(const std::wstring& archivePath,
                            const std::vector<std::wstring>& chain,
                            const std::wstring& dirPath,
                            UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                            REFIID riid, void** ppv);
