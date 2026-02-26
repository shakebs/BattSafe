param(
    [string]$ToolPrefix = "",
    [string]$BuildDir = "3_Firmware\\build",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

if (-not $ToolPrefix) {
    if ($env:RISCV_PREFIX) {
        $ToolPrefix = $env:RISCV_PREFIX
    } else {
        $ToolPrefix = "riscv64-unknown-elf-"
    }
}

$cc = "${ToolPrefix}gcc"
$objcopy = "${ToolPrefix}objcopy"
$sizeTool = "${ToolPrefix}size"

function Assert-Tool([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "Missing tool '$name'. Install RISC-V GCC and/or set -ToolPrefix or RISCV_PREFIX."
    }
}

Assert-Tool $cc
Assert-Tool $objcopy
Assert-Tool $sizeTool

$repoRoot = (Get-Location).Path
if (-not (Test-Path "3_Firmware\\src\\main.c")) {
    throw "Run this script from repository root (BattSafe)."
}

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$sources = @(
    "3_Firmware\\target\\startup.S",
    "3_Firmware\\target\\syscalls.c",
    "3_Firmware\\src\\main.c",
    "3_Firmware\\src\\anomaly_eval.c",
    "3_Firmware\\src\\correlation_engine.c",
    "3_Firmware\\src\\hal_gpio.c",
    "3_Firmware\\src\\hal_uart.c",
    "3_Firmware\\src\\input_packet.c",
    "3_Firmware\\src\\packet_format.c"
)

$includes = @(
    "-I3_Firmware\\src",
    "-I3_Firmware\\target"
)

$cflags = @(
    "-DTARGET_THEJAS32",
    "-Os",
    "-ffreestanding",
    "-fdata-sections",
    "-ffunction-sections",
    "-fno-common",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-march=rv32imac",
    "-mabi=ilp32"
)

$ldflags = @(
    "-nostartfiles",
    "-Wl,--gc-sections",
    "-Wl,-Map=$BuildDir\\user.map",
    "-T3_Firmware\\target\\thejas32_linker.ld",
    "-march=rv32imac",
    "-mabi=ilp32",
    "-lm"
)

$objects = @()
foreach ($src in $sources) {
    $obj = Join-Path $BuildDir (([System.IO.Path]::GetFileNameWithoutExtension($src)) + ".o")
    Write-Host "[CC] $src"
    & $cc @cflags @includes -c $src -o $obj
    $objects += $obj
}

$elf = Join-Path $BuildDir "user.elf"
$bin = Join-Path $BuildDir "user.bin"

Write-Host "[LD] $elf"
& $cc @cflags @objects @ldflags -o $elf

Write-Host "[OBJCOPY] $bin"
& $objcopy -O binary $elf $bin

Write-Host "[SIZE]"
& $sizeTool $elf

Write-Host ""
Write-Host "Build complete:"
Write-Host "  ELF: $elf"
Write-Host "  BIN: $bin"
Write-Host ""
Write-Host "Flash command:"
Write-Host "  python 3_Firmware\\target\\upload.py $bin --port COMx"
