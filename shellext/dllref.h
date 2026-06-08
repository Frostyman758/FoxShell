#pragma once
// Module lifetime ref count (drives DllCanUnloadNow). Each COM object created
// by this DLL bumps it in its constructor and drops it in its destructor.
void DllAddRef();
void DllRelease();
