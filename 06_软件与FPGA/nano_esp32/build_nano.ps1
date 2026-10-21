# Builds vibmon_nano with the Arduino IDE 1.8 command-line builder.
#
#   powershell -ExecutionPolicy Bypass -File build_nano.ps1 [-ArduinoDir D:\Arduino]
#
# Two Windows limits are worked around without touching the installed board
# package:
#  * the ESP32 toolchain cannot open paths with Chinese characters, so the
#    sketch and the shared library are copied to an ASCII temp folder;
#  * the core passes ~200 include paths inline, which exceeds the 32 KiB
#    CreateProcess limit when the core lives under %LOCALAPPDATA%. A temporary
#    directory junction gives the core a short path for this build only.
param(
  [string]$ArduinoDir = "D:\Arduino",
  [string]$Packages = "$env:LOCALAPPDATA\Arduino15\packages",
  [string]$Fqbn = "arduino:esp32:nano_nora:PartitionScheme=default,PinNumbers=default,USBMode=default",
  [string]$WorkDir = "$env:TEMP\vibmon_nano_build"
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$sketchSrc = Join-Path $here "vibmon_nano"
$libSrc = Join-Path (Split-Path -Parent $here) "shared\vibproto"

$coreRoot = Join-Path $Packages "arduino\hardware\esp32"
$core = Get-ChildItem $coreRoot -Directory | Sort-Object Name | Select-Object -Last 1
if (-not $core) { throw "Arduino ESP32 Boards (arduino:esp32) not installed under $coreRoot" }

# Package layout <hw>\<packager>\hardware\<arch>\<semver>; a short semver keeps the path short.
$hw = Join-Path $env:TEMP "v"
$link = Join-Path $hw "arduino\hardware\esp32\2.0.18"
# Directory.Delete without recursion removes only the junction, never the target.
if (Test-Path $link) { [System.IO.Directory]::Delete($link) }
New-Item -ItemType Directory -Force (Split-Path -Parent $link) | Out-Null
New-Item -ItemType Junction -Path $link -Target $core.FullName | Out-Null

# The renamed platform is not in the package index, so its tool dependencies
# are passed explicitly.
function Latest-Tool([string]$name) {
  $d = Get-ChildItem (Join-Path $Packages "esp32\tools\$name") -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name | Select-Object -Last 1
  if (-not $d) { throw "tool $name not found under $Packages\esp32\tools" }
  return $d.FullName
}
$gcc = Latest-Tool "xtensa-esp32s3-elf-gcc"
$esptool = Latest-Tool "esptool_py"

try {
  if (Test-Path $WorkDir) { Remove-Item -Recurse -Force $WorkDir }
  $sketch = Join-Path $WorkDir "vibmon_nano"
  $libs = Join-Path $WorkDir "libraries"
  $out = Join-Path $WorkDir "out"
  New-Item -ItemType Directory -Force $sketch, $libs, $out | Out-Null
  Copy-Item "$sketchSrc\*" $sketch -Recurse
  Copy-Item $libSrc (Join-Path $libs "vibproto") -Recurse

  $builder = Join-Path $ArduinoDir "arduino-builder.exe"
  $log = Join-Path $WorkDir "build.log"
  & $builder -compile -logger=human `
    -hardware (Join-Path $ArduinoDir "hardware") -hardware $hw `
    -tools (Join-Path $ArduinoDir "tools-builder") -tools (Join-Path $ArduinoDir "hardware\tools\avr") -tools $Packages `
    -built-in-libraries (Join-Path $ArduinoDir "libraries") -libraries $libs `
    -fqbn $Fqbn -ide-version 10819 -build-path $out -warnings default `
    -prefs "build.warn_data_percentage=75" `
    -prefs "runtime.tools.xtensa-esp32s3-elf-gcc.path=$gcc" `
    -prefs "runtime.tools.esptool_py.path=$esptool" `
    (Join-Path $sketch "vibmon_nano.ino") *> $log
  $code = $LASTEXITCODE
  Get-Content $log | Where-Object { $_ -match "error|warning|Sketch uses|Global variables" } | Select-Object -First 80
  if ($code -ne 0) { throw "arduino-builder failed ($code); full log: $log" }

  $bin = Join-Path $here "bin"
  New-Item -ItemType Directory -Force $bin | Out-Null
  foreach ($f in @("vibmon_nano.ino.bin", "vibmon_nano.ino.bootloader.bin", "vibmon_nano.ino.partitions.bin", "vibmon_nano.ino.elf")) {
    $p = Join-Path $out $f
    if (Test-Path $p) { Copy-Item $p $bin -Force }
  }
  Get-ChildItem $bin | ForEach-Object {
    "{0}  {1,9}  {2}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash, $_.Length, $_.Name
  }
} finally {
  if (Test-Path $link) { [System.IO.Directory]::Delete($link) }
}
