$ErrorActionPreference = "Stop"

$platformio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
if (-not (Test-Path $platformio)) {
    throw "PlatformIO not found at $platformio"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    & $platformio run --target upload @args
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    $openocd = Join-Path $env:USERPROFILE ".platformio\\packages\\tool-openocd\\bin\\openocd.exe"
    if (Test-Path $openocd) {
        & $openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "init" -c "reset run" -c "shutdown"
    }
}
finally {
    Pop-Location
}
