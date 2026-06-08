#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <vector>

// IEnumIDList over a snapshot of one directory's children. FoxShellFolder
// builds the item vector in EnumObjects and hands ownership here.
class FoxEnumIDList : public IEnumIDList
{
public:
    FoxEnumIDList(std::vector<PITEMID_CHILD>&& items);
    ~FoxEnumIDList();

    IFACEMETHODIMP QueryInterface(REFIID, void**) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    IFACEMETHODIMP Next(ULONG, PITEMID_CHILD*, ULONG*) override;
    IFACEMETHODIMP Skip(ULONG) override;
    IFACEMETHODIMP Reset() override;
    IFACEMETHODIMP Clone(IEnumIDList**) override;

private:
    LONG m_ref = 1;
    std::vector<PITEMID_CHILD> m_items;
    size_t m_pos = 0;
};
