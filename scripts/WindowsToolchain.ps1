function Initialize-WindowsToolchain {
    if ($env:OS -ne 'Windows_NT') {
        throw 'Use setup.sh / build.sh on macOS or Linux.'
    }
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Missing Visual Studio C++ Build Tools. Run .\setup.ps1 first.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'Missing Visual Studio C++ toolchain. Run .\setup.ps1 first.' }
    Import-Module (Join-Path $installation 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Visual Studio C++ toolchain initialization failed.' }
}
