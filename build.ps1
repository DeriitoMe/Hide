param([switch]$PrepareAssets, [switch]$NoInstall)
$ErrorActionPreference = 'Stop'
$cursorProject = $PSScriptRoot
Set-Location -LiteralPath $cursorProject
if ($PrepareAssets) {
    & python "$cursorProject\tools\prepare_assets.py"
    if ($LASTEXITCODE -ne 0) { throw 'Cursor asset preparation failed.' }
}
$cursorCompiler = Get-ChildItem -Path 'D:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe','C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $cursorCompiler) { throw 'Install the Visual Studio C++ desktop build tools before rebuilding.' }
$cursorToolset = Split-Path (Split-Path (Split-Path (Split-Path $cursorCompiler.FullName)))
$cursorKitRoot = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$cursorKitVersion = Get-ChildItem -LiteralPath "$cursorKitRoot\Include" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
$cursorIncludes = @("$cursorToolset\include","$cursorKitRoot\Include\$cursorKitVersion\ucrt","$cursorKitRoot\Include\$cursorKitVersion\shared","$cursorKitRoot\Include\$cursorKitVersion\um")
$cursorLibs = @("$cursorToolset\lib\x64","$cursorKitRoot\Lib\$cursorKitVersion\ucrt\x64","$cursorKitRoot\Lib\$cursorKitVersion\um\x64") | ForEach-Object { "/LIBPATH:$_" }
$cursorFlags = @('/nologo','/O2','/EHsc','/std:c++17','/utf-8','/MT','/W4','/guard:cf','/DUNICODE','/D_UNICODE') + @($cursorIncludes | ForEach-Object { "/I$_" })
New-Item -ItemType Directory -Path "$cursorProject\build" -Force | Out-Null
& "$cursorKitRoot\bin\$cursorKitVersion\x64\rc.exe" /nologo "/I$cursorKitRoot\Include\$cursorKitVersion\shared" "/I$cursorKitRoot\Include\$cursorKitVersion\um" /fobuild/Hide.res src/Hide.rc
if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
& $cursorCompiler.FullName @cursorFlags src/main.cpp src/cursor_codec.cpp src/scheme_tracker.cpp build/Hide.res /Febuild/Hide.exe /Fobuild/ /link /SUBSYSTEM:WINDOWS /MANIFEST:NO @cursorLibs user32.lib gdi32.lib kernel32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uiautomationcore.lib uuid.lib psapi.lib wtsapi32.lib bcrypt.lib windowscodecs.lib
if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed.' }
& $cursorCompiler.FullName @cursorFlags tests/state_tests.cpp /Febuild/state_tests.exe /Fobuild/state_tests.obj /link @cursorLibs
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
& "$cursorProject\build\state_tests.exe"
if ($LASTEXITCODE -ne 0) { throw 'State tests failed.' }
& $cursorCompiler.FullName @cursorFlags tests/cursor_tests.cpp src/cursor_codec.cpp src/scheme_tracker.cpp /Febuild/cursor_tests.exe /Fobuild/ /link @cursorLibs user32.lib gdi32.lib advapi32.lib ole32.lib windowscodecs.lib bcrypt.lib
if ($LASTEXITCODE -ne 0) { throw 'Cursor test compilation failed.' }
& "$cursorProject\build\cursor_tests.exe"
if ($LASTEXITCODE -ne 0) { throw 'Cursor tests failed.' }
if (-not $NoInstall) { Copy-Item -LiteralPath "$cursorProject\build\Hide.exe" -Destination "$cursorProject\Hide.exe" -Force }
Write-Output 'Built Hide.exe. Cursor variants are generated locally from the current scheme.'
