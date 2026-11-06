param([switch]$Check)
$ErrorActionPreference = 'Stop'
try {
    if ($env:OS -ne 'Windows_NT') { throw 'Use ./setup.sh on macOS or Linux.' }
    function Install-MissingTool([string]$Command, [string]$Package, [string[]]$ExtraArguments = @()) {
        if (Get-Command $Command -ErrorAction SilentlyContinue) { return }
        if ($Check) { throw "Missing $Command. Run .\setup.ps1." }
        if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { throw "Install $Package manually (winget is unavailable), then rerun setup." }
        if ((Read-Host "Install $Package using winget? [y/N]") -notin @('y', 'Y')) { throw 'Installation declined.' }
        & winget install --exact --id $Package @ExtraArguments
        if ($LASTEXITCODE -ne 0) { throw "Installation of $Package failed ($LASTEXITCODE)." }
        $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
        if (-not (Get-Command $Command -ErrorAction SilentlyContinue)) { throw "Restart PowerShell after installing $Package, then rerun setup." }
    }
    Install-MissingTool git Git.Git
    Install-MissingTool cmake Kitware.CMake
    Install-MissingTool ninja Ninja-build.Ninja
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installation = $null
    if (Test-Path $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0) { throw 'Visual Studio discovery failed.' }
    }
    if (-not $installation -and -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        if ($Check) { throw 'Missing Visual Studio C++ Build Tools.' }
        if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { throw 'Install Visual Studio C++ Build Tools and the Windows SDK manually, then rerun setup.' }
        if ((Read-Host 'Install Visual Studio 2022 C++ Build Tools and Windows SDK using winget? [y/N]') -notin @('y', 'Y')) { throw 'Installation declined.' }
        & winget install --exact --id Microsoft.VisualStudio.2022.BuildTools --override '--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
        if ($LASTEXITCODE -ne 0) { throw "Visual Studio installation failed ($LASTEXITCODE)." }
    }
    . (Join-Path $PSScriptRoot 'scripts/WindowsToolchain.ps1')
    Initialize-WindowsToolchain
    Push-Location $PSScriptRoot
    try {
        & cmake --list-presets
        if ($LASTEXITCODE -ne 0) { throw 'CMake 3.23 or newer is required.' }
        foreach ($path in @('vendor/vulkan-headers', 'vendor/volk', 'vendor/glslang', 'vendor/freetype', 'vendor/harfbuzz', 'vendor/sheenbidi', 'vendor/utf8proc')) {
            $entry = & git ls-files --stage -- $path
            if ($LASTEXITCODE -ne 0 -or $entry -notmatch '^160000 ([0-9a-f]+) ') { throw "No pinned submodule at $path." }
            $expected = $Matches[1]
            $actual = ''
            if (Test-Path "$path/.git") {
                $actual = & git -C $path rev-parse HEAD
                if ($LASTEXITCODE -ne 0) { throw "Cannot read $path HEAD." }
            }
            if ($actual -eq $expected) { continue }
            if ($Check) { throw "Missing or out-of-date submodule: $path. Run .\setup.ps1." }
            if ($actual) {
                $dirty = & git -C $path status --porcelain
                if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $path." }
                if ($dirty) { throw "Refusing to move dirty submodule $path. Preserve its changes before rerunning setup." }
            }
            & git submodule update --init -- $path
            if ($LASTEXITCODE -ne 0) { throw "Submodule preparation failed: $path." }
        }
    } finally { Pop-Location }
    Write-Output 'Setup ready. Run .\build.ps1 (debug), .\build.ps1 release, or .\build.ps1 relwithdebinfo.'
} catch { Write-Error $_; exit 1 }
