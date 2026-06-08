# Reverse install.ps1 (per-user).
[CmdletBinding()]
param(
    [string[]]$Ext = @('.dat', '.qar', '.fpk', '.fpkd', '.pftxs', '.g0s', '.sbp', '.stp', '.sab', '.fsop', '.mtar'),
    [string]$InstallDir = "$env:USERPROFILE\foxshell"
)
$ErrorActionPreference = 'SilentlyContinue'

$ProgId  = 'Fox.Archive'
$classes = 'HKCU:\Software\Classes'
$backup  = 'HKCU:\Software\foxshell\backup'

# Restore extension defaults from backup.
foreach ($e in $Ext) {
    $key = "$classes\$e"
    $old = (Get-ItemProperty -Path $backup -Name $e -ErrorAction SilentlyContinue).$e
    if ($old -eq '<absent>')      { Remove-Item -Recurse -Force $key }
    elseif ($old -eq '<none>')    { Remove-ItemProperty -Path $key -Name '(default)' }
    elseif ($null -ne $old)       { Set-ItemProperty -Path $key -Name '(default)' -Value $old }
    Write-Host "  restored $e"
}

# Remove the base ProgID, the per-extension ProgIDs, and the backup store.
Remove-Item -Recurse -Force "$classes\$ProgId"
foreach ($e in $Ext) { Remove-Item -Recurse -Force "$classes\Fox.Archive$e" }
Remove-Item -Recurse -Force 'HKCU:\Software\foxshell'

# Unregister COM server.
$dll = Join-Path $InstallDir 'foxshellext.dll'
if (Test-Path $dll) { Start-Process regsvr32.exe -ArgumentList '/u','/s',"`"$dll`"" -Wait }

# Best-effort: remove any stale HKLM\Software\Classes entries left over from
# early debugging (these were never part of the per-user install). Needs admin;
# silently skipped if declined.
$clsid = '{5AA92D71-4013-463E-BFDE-673DB7C70FCF}'
$hklmDel = @(
    "reg delete `"HKLM\SOFTWARE\Classes\CLSID\$clsid`" /f",
    'reg delete "HKLM\SOFTWARE\Classes\Fox.Archive" /f',
    'reg delete "HKLM\SOFTWARE\Classes\.fpk" /f'
) -join ' & '
try { Start-Process cmd.exe -ArgumentList '/c', $hklmDel -Verb RunAs -Wait -ErrorAction Stop } catch {}

# Notify shell, then remove files (DLL may be locked if Explorer still hosts it).
$sig = '[DllImport("shell32.dll")] public static extern void SHChangeNotify(int e, uint f, IntPtr a, IntPtr b);'
$shell = Add-Type -MemberDefinition $sig -Name Notify -Namespace WUn -PassThru
$shell::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)

Start-Sleep -Milliseconds 300
Remove-Item -Recurse -Force $InstallDir
Write-Host "Uninstalled. If files were locked, restart Explorer and delete $InstallDir."
