# Install the Fox archive shell extension (per-user, no elevation required).
#
#   - Copies foxshellext.dll + foxarchive.dll + qar_dictionary.txt to the
#     install dir.
#   - Registers the COM server (regsvr32 -> DllRegisterServer, HKCU).
#   - Associates .dat/.qar/.fpk/.fpkd with our folder ProgID so Explorer opens
#     them as browsable folders. Previous defaults are backed up for uninstall.
#
# Usage:
#   pwsh -File install.ps1                 # default extensions
#   pwsh -File install.ps1 -Ext .fpk,.fpkd # only these
[CmdletBinding()]
param(
    [string[]]$Ext = @('.dat', '.qar', '.fpk', '.fpkd', '.pftxs', '.g0s', '.sbp', '.stp', '.sab', '.fsop', '.mtar'),
    [string]$InstallDir = "$env:USERPROFILE\foxshell"
)
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Test-Path (Join-Path $here 'foxshellext.dll')) {
    # Dist mode: shipped release — all binaries + dictionaries sit next to this script.
    $shellDll  = Join-Path $here 'foxshellext.dll'
    $bridgePub = Join-Path $here 'foxarchive.dll'
    $foxbrowse = Join-Path $here 'foxbrowse.exe'
    $dict      = Join-Path $here 'qar_dictionary.txt'
    $dictGz    = Join-Path $here 'gzs_dictionary.txt'
    $dictFpk   = Join-Path $here 'fpk_dictionary.txt'
    $vgmSrc    = Join-Path $here 'vgmstream'
} else {
    # Repo mode: pull straight from the build-output locations.
    $repo      = Split-Path -Parent $here
    $bridgePub = Join-Path $repo 'bridge\bin\Release\net10.0\win-x64\publish\foxarchive.dll'
    $shellDll  = Join-Path $repo 'shellext\build\foxshellext.dll'
    $foxbrowse = Join-Path $repo 'shellext\build\foxbrowse.exe'
    $dict      = Join-Path $repo 'Fox_parser\qar_dictionary.txt'
    if (-not (Test-Path $dict)) { $dict = Join-Path (Split-Path $repo) 'Fox_parser\qar_dictionary.txt' }
    $dictGz    = Join-Path (Split-Path $repo) 'Fox_parser\dist\dict\gzs_dictionary.txt'
    if (-not (Test-Path $dictGz)) { $dictGz = Join-Path $repo 'Fox_parser\dist\dict\gzs_dictionary.txt' }
    $dictFpk   = Join-Path (Split-Path $repo) 'gzstool\fpk_dictionary.txt'
    if (-not (Test-Path $dictFpk)) { $dictFpk = Join-Path (Split-Path $repo) 'Fox_parser\dist\dict\fpk_dictionary.txt' }
    $vgmSrc    = Join-Path $repo 'vendor\vgmstream'
}

foreach ($f in @($bridgePub, $shellDll)) {
    if (-not (Test-Path $f)) { throw "missing build artifact: $f  (build the bridge and shellext first)" }
}

$ProgId   = 'Fox.Archive'
$Clsid    = '{5AA92D71-4013-463E-BFDE-673DB7C70FCF}'
$FRIENDLY  = 'MGSV Fox Archive'
$FolderAttrs = 0x200001A0  # matches zipfldr; makes the shell mount file-as-folder

# Per-user association overrides under Explorer\FileExts win over our HKCR
# association (UserChoice / OpenWithProgids / *_auto_file). Clear them so the
# shell falls back to our folder ProgID. UserChoice carries a Deny ACE, so we
# reset its DACL (we own it) before deleting the FileExts\<ext> subtree.
function Clear-ShellAssocOverride([string]$ext) {
    $reg  = [Microsoft.Win32.Registry]::CurrentUser
    $base = "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\$ext"
    if (-not $reg.OpenSubKey($base)) { return }
    $uc = $reg.OpenSubKey("$base\UserChoice",
            [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
            [System.Security.AccessControl.RegistryRights]::ChangePermissions)
    if ($uc) {
        try {
            $acl = $uc.GetAccessControl()
            foreach ($r in @($acl.Access)) { if ($r.AccessControlType -eq 'Deny') { [void]$acl.RemoveAccessRule($r) } }
            $me = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
            $acl.AddAccessRule((New-Object System.Security.AccessControl.RegistryAccessRule($me,'FullControl','Allow')))
            $uc.SetAccessControl($acl)
        } catch {}
        $uc.Close()
    }
    try {
        $p = $reg.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts', $true)
        $p.DeleteSubKeyTree($ext); $p.Close()
    } catch {}
}

# Copy a file into the install dir, retrying until it actually lands. Explorer
# can re-grab a loaded DLL within ~1s of being killed and silently lock it, so a
# plain Copy-Item can fail/leave a stale file without erroring. We retry and then
# VERIFY the destination size matches the source so the install can't lie.
function Install-File([string]$src, [string]$dir, [switch]$Required) {
    $dst = Join-Path $dir (Split-Path $src -Leaf)
    $want = (Get-Item $src).Length
    for ($i = 0; $i -lt 12; $i++) {
        try { Copy-Item $src $dst -Force -ErrorAction Stop } catch {}
        if ((Test-Path $dst) -and (Get-Item $dst).Length -eq $want) { return $true }
        Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }
    if ($Required) { throw "could not write $dst (Explorer keeps locking it). Close all Explorer windows and re-run." }
    return $false
}

# Explorer may already have an older copy of our DLL loaded (which both locks
# the file and serves stale behaviour). Stop it so the copy lands and the new
# handler is picked up; we restart it at the end.
Write-Host "Stopping Explorer so the handler can be updated..."
Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800

Write-Host "Installing to $InstallDir"
New-Item -ItemType Directory -Force $InstallDir | Out-Null
[void](Install-File $shellDll  $InstallDir -Required)   # foxshellext.dll MUST land
[void](Install-File $bridgePub $InstallDir -Required)   # foxarchive.dll  MUST land
if (Test-Path $foxbrowse) { [void](Install-File $foxbrowse $InstallDir) }
if (Test-Path $dict)    { [void](Install-File $dict    $InstallDir) } else { Write-Warning "qar_dictionary.txt not found; QAR names will be hashes" }
if (Test-Path $dictGz)  { [void](Install-File $dictGz  $InstallDir) } else { Write-Warning "gzs_dictionary.txt not found; .g0s names will be hashes" }
if (Test-Path $dictFpk) { [void](Install-File $dictFpk $InstallDir) } else { Write-Warning "fpk_dictionary.txt not found; GZ fpk entry names will be md5 hashes" }

# vgmstream (bundled) — opens .wem as playable .wav. Copy the whole folder.
if (Test-Path $vgmSrc) {
    $vgmDst = Join-Path $InstallDir 'vgmstream'
    New-Item -ItemType Directory -Force $vgmDst | Out-Null
    Copy-Item (Join-Path $vgmSrc '*') $vgmDst -Recurse -Force
} else { Write-Warning "vgmstream not found at $vgmSrc; opening a .wem will not auto-convert to .wav" }

$installedShellDll = Join-Path $InstallDir 'foxshellext.dll'

# Register the COM server (writes HKCU\Software\Classes\CLSID\$Clsid).
Write-Host "Registering COM server..."
$rc = Start-Process regsvr32.exe -ArgumentList '/s', "`"$installedShellDll`"" -Wait -PassThru
if ($rc.ExitCode -ne 0) { throw "regsvr32 failed ($($rc.ExitCode))" }

$classes = 'HKCU:\Software\Classes'
$backup  = 'HKCU:\Software\foxshell\backup'
New-Item -Force -Path $backup | Out-Null

# Per-extension friendly "Type" names (from the wiki, mirrors foxtypes.h). Each
# extension gets its OWN ProgID so Explorer shows the real archive type on the
# top-level file (e.g. a .dat reads "QAR Archive", a .fpk "Fox Package") instead
# of one shared "MGSV Fox Archive". All ProgIDs point at the same CLSID handler.
$ExtTypeName = @{
    '.dat'   = 'QAR Archive'
    '.qar'   = 'QAR Archive'
    '.fpk'   = 'Fox Package'
    '.fpkd'  = 'Fox Package Data'
    '.pftxs' = 'Packed Fox Textures'
    '.g0s'   = 'QAR Archive (GZ)'
    '.sbp'   = 'Sound Bank Package'
    '.stp'   = 'Streamed Package'
    '.sab'   = 'Streamed Animation'
    '.fsop'  = 'Fox Shader Object Pack'
    '.mtar'  = 'Motion Archive'
}

# Keep the canonical base ProgID too (the CLSID's ProgID back-ref points here).
New-Item -Force -Path "$classes\$ProgId" | Out-Null
Set-ItemProperty -Path "$classes\$ProgId" -Name '(default)' -Value $FRIENDLY -ErrorAction SilentlyContinue

foreach ($e in $Ext) {
    $typeName = $ExtTypeName[$e]; if (-not $typeName) { $typeName = $FRIENDLY }
    $pE = "Fox.Archive$e"   # e.g. "Fox.Archive.dat"

    # Per-extension ProgID -> our CLSID, carrying the friendly type name.
    New-Item -Force -Path "$classes\$pE" | Out-Null
    Set-ItemProperty -Path "$classes\$pE" -Name '(default)' -Value $typeName
    New-Item -Force -Path "$classes\$pE\CLSID" | Out-Null
    Set-ItemProperty -Path "$classes\$pE\CLSID" -Name '(default)' -Value $Clsid
    New-Item -Force -Path "$classes\$pE\ShellFolder" | Out-Null
    New-ItemProperty -Force -Path "$classes\$pE\ShellFolder" -Name 'Attributes' -PropertyType DWord -Value $FolderAttrs | Out-Null

    # Associate the extension, backing up any existing per-user default.
    $key = "$classes\$e"
    if (Test-Path $key) {
        $old = (Get-ItemProperty -Path $key -Name '(default)' -ErrorAction SilentlyContinue).'(default)'
        if ($null -ne $old) { New-ItemProperty -Force -Path $backup -Name $e -Value $old | Out-Null }
        else { New-ItemProperty -Force -Path $backup -Name $e -Value '<none>' | Out-Null }
    } else {
        New-Item -Force -Path $key | Out-Null
        New-ItemProperty -Force -Path $backup -Name $e -Value '<absent>' | Out-Null
    }
    Set-ItemProperty -Path $key -Name '(default)' -Value $pE
    Clear-ShellAssocOverride $e
    Write-Host "  $e -> $pE ($typeName)"
}

# Tell the shell associations changed.
$sig = '[DllImport("shell32.dll")] public static extern void SHChangeNotify(int e, uint f, IntPtr a, IntPtr b);'
$shell = Add-Type -MemberDefinition $sig -Name Notify -Namespace W -PassThru
$shell::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)  # SHCNE_ASSOCCHANGED

# Restart Explorer so it loads the freshly-installed handler cleanly.
Start-Sleep -Milliseconds 300
if (-not (Get-Process -Name explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }

Write-Host "`nDone. Open a $($Ext -join '/') file in Explorer and double-click into it."
Write-Host "Folders inside now navigate; nothing is extracted to disk (all in-memory)."
