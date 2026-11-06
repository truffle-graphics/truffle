param(
    [string]$Preset = 'debug',
    [ValidateSet('on', 'off')]
    [string]$WarningsAsErrors,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$BuildArguments
)
$ErrorActionPreference = 'Stop'
try {
    if ($Preset -in @('--help', '-h')) {
        Write-Output 'Usage: .\build.ps1 [debug|release|relwithdebinfo|package] [-WarningsAsErrors on|off] [CMake build arguments]'
        Write-Output 'Warnings default to off in a new build; the selected build remembers the setting.'
        exit 0
    }
    . (Join-Path $PSScriptRoot 'scripts/WindowsToolchain.ps1')
    Initialize-WindowsToolchain
    foreach ($tool in @('cmake', 'ninja')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing $tool. Run .\setup.ps1 first." }
    }
    Push-Location $PSScriptRoot
    try {
        $configureArguments = @('--preset', $Preset)
        if ($WarningsAsErrors) {
            $configureArguments += "-DTFL_WARNINGS_AS_ERRORS=$($WarningsAsErrors.ToUpperInvariant())"
        }
        & cmake @configureArguments
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & cmake --build --preset $Preset @BuildArguments
        exit $LASTEXITCODE
    } finally { Pop-Location }
} catch { Write-Error $_; exit 1 }
