#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include "FoxEnumIDList.h"
#include "dllref.h"

FoxEnumIDList::FoxEnumIDList(std::vector<PITEMID_CHILD>&& items) : m_items(std::move(items))
{ DllAddRef(); }

FoxEnumIDList::~FoxEnumIDList()
{
    for (auto p : m_items) if (p) CoTaskMemFree(p);
    DllRelease();
}

IFACEMETHODIMP FoxEnumIDList::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IEnumIDList) { *ppv = static_cast<IEnumIDList*>(this); AddRef(); return S_OK; }
    *ppv = nullptr; return E_NOINTERFACE;
}
IFACEMETHODIMP_(ULONG) FoxEnumIDList::AddRef() { return InterlockedIncrement(&m_ref); }
IFACEMETHODIMP_(ULONG) FoxEnumIDList::Release()
{ LONG r = InterlockedDecrement(&m_ref); if (r == 0) delete this; return r; }

IFACEMETHODIMP FoxEnumIDList::Next(ULONG celt, PITEMID_CHILD* rgelt, ULONG* fetched)
{
    ULONG got = 0;
    while (got < celt && m_pos < m_items.size())
    {
        rgelt[got] = reinterpret_cast<PITEMID_CHILD>(ILClone(m_items[m_pos]));
        if (!rgelt[got]) break;
        got++; m_pos++;
    }
    if (fetched) *fetched = got;
    return (got == celt) ? S_OK : S_FALSE;
}
IFACEMETHODIMP FoxEnumIDList::Skip(ULONG celt) { m_pos += celt; return S_OK; }
IFACEMETHODIMP FoxEnumIDList::Reset() { m_pos = 0; return S_OK; }
IFACEMETHODIMP FoxEnumIDList::Clone(IEnumIDList** pp) { if (pp) *pp = nullptr; return E_NOTIMPL; }
