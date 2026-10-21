$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$gcc='G:\Xilinx2025\2025.2\Vitis\gnu\aarch32\nt\gcc-arm-none-eabi\bin\arm-none-eabi-gcc.exe'
$packageRoot=Split-Path (Split-Path $root -Parent) -Parent
$bsp=Join-Path $packageRoot 'AX7010_Camera_HDMI30\vitis\camera_platform\ps7_cortexa9_0\standalone_domain\bsp\ps7_cortexa9_0'
$linker=Join-Path $packageRoot 'AX7010_Camera_HDMI30\vitis\camera_init\src\lscript.ld'
$spec=Join-Path $packageRoot 'AX7010_Camera_HDMI30\vitis\camera_init\src\Xilinx.spec'
$sources=@(Get-ChildItem -LiteralPath "$root\src" -Recurse -Filter '*.c' | Where-Object {$_.Name -ne 'stereo_match.c'} | ForEach-Object {$_.FullName})
$sources += "$root\palette.c"
& $gcc -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -O3 "-specs=$spec" '-Wl,--build-id=none' "-Wl,-T,$linker" "-I$bsp\include" "-I$root\src" @sources "-L$bsp\lib" '-Wl,--start-group' -lxil -lgcc -lc -lm '-Wl,--end-group' -o "$root\stereo_preview.elf"
if($LASTEXITCODE -ne 0){throw 'Preview firmware compilation failed'}
& $gcc -mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=softfp -O3 -ffreestanding -fno-builtin -nostdlib '-Wl,-Ttext=0x02000000' '-Wl,--entry=_start' '-Wl,--build-id=none' "$root\worker_entry.S" "$root\tracking_worker.c" -lgcc -o "$root\depth_worker.elf"
if($LASTEXITCODE -ne 0){throw 'Depth worker compilation failed'}
