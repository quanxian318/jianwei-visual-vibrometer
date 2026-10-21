param([string]$VivadoRoot='G:\Xilinx2025\2025.2\Vivado')
$ErrorActionPreference='Stop'
$env:PROCESSOR_ARCHITECTURE='AMD64'
$env:XILINX_VIVADO=$VivadoRoot
$env:RDI_APPROOT=$VivadoRoot
$env:HDI_APPROOT=$VivadoRoot
$env:RDI_BASEROOT=Split-Path $VivadoRoot
$env:RDI_DATADIR="$VivadoRoot\data"
$env:RDI_LIBDIR="$VivadoRoot\lib\win64.o"
$env:RDI_BINDIR="$VivadoRoot\bin"
$env:RDI_BINROOT="$VivadoRoot\bin"
$env:RDI_PLATFORM='win64'
$env:RDI_OPT_EXT='.o'
$env:TCL_LIBRARY="$VivadoRoot/tps/tcl/tcl8.6"
$env:PATH="$VivadoRoot\lib\win64.o;$VivadoRoot\bin;$VivadoRoot\tps\win64\jre21.0.5_11\bin\server;$VivadoRoot\tps\win64\jre21.0.5_11\bin;$VivadoRoot\tps\win64\python-3.13.0;$VivadoRoot\tps\mingw\10.0.0\win64.o\nt\bin;"+$env:PATH
& "$VivadoRoot\bin\vivado.bat" -mode batch -source "$PSScriptRoot\Verify_Project.tcl" -log "$PSScriptRoot\vivado_open_check.log" -journal "$PSScriptRoot\vivado_open_check.jou"
if($LASTEXITCODE){throw 'Vivado package check failed'}
