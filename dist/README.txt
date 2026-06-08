Fox Shell Extension — rel2
==========================

Browse MGSV Fox Engine archives in Windows Explorer like folders: drill in,
preview .ftex textures, and drag files straight out. Works for TPP and Ground
Zeroes, including archives nested inside other archives. Nothing is written to
disk while browsing — only when you open or drag a file out.

Supported (detected by content where possible, not just extension):
  .dat / .qar    QAR archives (TPP)
  .g0s           QAR archives (Ground Zeroes)
  .fpk / .fpkd   Fox Packages (TPP "win" and GZ "ste")
  .pftxs         Packed Fox Textures (TPP and GZ layouts)
  .sbp           Sound Bank Package  -> bnk + stp/sab
  .stp           Streamed Package    -> wem (+ ls2)
  .sab           Streamed Animation  -> lsst
  .fsop          Fox Shader Pack     -> *_vs.fxc / *_ps.fxc
  .mtar          Motion Archive      -> gani + trk/chnk/exchnk/enchnk

Opening a .wem (Wwise audio) auto-decodes it to a playable .wav via the bundled
vgmstream (vendor/vgmstream, ISC + LGPL codecs — see its COPYING), so VLC / any
player just opens it. No effect on browsing; conversion happens only on open.

INSTALL (per-user, no admin needed):
  Right-click install.ps1 -> "Run with PowerShell"
  …or in a terminal:  pwsh -File install.ps1
  It installs to %USERPROFILE%\foxshell and restarts Explorer.

  If a copy ever fails because Explorer has the DLL locked, just close all
  Explorer windows and run install.ps1 again — it retries and verifies.

UNINSTALL:
  pwsh -File uninstall.ps1

NOTES:
  - The Explorer search box filters the current folder only; it does not search
    recursively into subfolders (a Windows limitation for virtual folders).
  - Extracted/opened files are cached under %TEMP%\fox\ and cleared each Explorer
    session.

FILES:
  foxshellext.dll      the Explorer namespace extension (native C++ COM)
  foxarchive.dll       the archive reader (NativeAOT; no .NET runtime needed)
  qar/gzs/fpk_*.txt    name dictionaries (resolve hashed entry names)
  install/uninstall.ps1
