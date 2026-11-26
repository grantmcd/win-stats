param(
    [ValidateSet('x64','arm64')][string] $Arch = 'x64',
    [switch] $SmokeTest
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$out = Join-Path $repo "out/win-$Arch"
$build = Join-Path $repo "out/obj-$Arch"
New-Item -ItemType Directory -Force $out, $build | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install the Visual Studio C++ desktop build tools.' }
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=$Arch -host_arch=x64" | Out-Null

Push-Location $build
try {
    & cl /nologo /std:c++20 /EHsc /utf-8 /O2 /MT /DUNICODE /D_UNICODE "$repo/src/Monitor/main.cpp" /link /SUBSYSTEM:WINDOWS "/OUT:$out/WinStats.exe" /MANIFEST:EMBED "/MANIFESTINPUT:$repo/src/Monitor/WinStats.manifest" user32.lib gdi32.lib shell32.lib advapi32.lib iphlpapi.lib
    if ($LASTEXITCODE) { throw "Monitor compilation failed ($LASTEXITCODE)." }
} finally { Pop-Location }

$platform = if ($Arch -eq 'arm64') { 'ARM64' } else { 'x64' }
& dotnet publish "$repo/src/Display/WinStats.Display.csproj" -c Release -r "win-$Arch" "-p:Platform=$platform" --self-contained true -p:DebugType=None -p:DebugSymbols=false -o "$out/Display"
if ($LASTEXITCODE) { throw "Display publication failed ($LASTEXITCODE)." }

# Unpackaged, self-contained Windows App SDK apps also need the VC++ runtime.
# Include the redistributable DLLs app-locally for clean Windows installations.
$crt = Get-ChildItem "$env:VCToolsRedistDir/$Arch/Microsoft.VC*.CRT" -Directory | Select-Object -First 1
if (!$crt) { throw "Missing Visual C++ redistributables for $Arch." }
Copy-Item "$($crt.FullName)/*.dll" "$out/Display/" -Force
Copy-Item "$repo/README.md" $out
Copy-Item "$repo/THIRD-PARTY-NOTICES.txt" $out

if ($SmokeTest) {
    $log = Join-Path ([IO.Path]::GetTempPath()) 'WinStats-SmokeTest.log'
    if (Test-Path $log) { Remove-Item $log }
    $process = Start-Process (Join-Path $out 'WinStats.exe') -ArgumentList '--self-test' -PassThru
    if (!$process.WaitForExit(45000)) { $process.Kill(); throw 'Monitor/display smoke test timed out.' }
    if ($process.ExitCode -ne 0) {
        if (Test-Path $log) { Get-Content $log }
        throw "Monitor/display smoke test failed with exit code $($process.ExitCode)."
    }
}
