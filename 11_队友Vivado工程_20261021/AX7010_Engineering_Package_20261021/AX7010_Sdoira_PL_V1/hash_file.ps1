param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference='Stop'
$sha=[System.Security.Cryptography.SHA256]::Create()
try {
    [System.BitConverter]::ToString($sha.ComputeHash([System.IO.File]::ReadAllBytes($Path))).Replace('-','')
} finally { $sha.Dispose() }
