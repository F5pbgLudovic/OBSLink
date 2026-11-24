# Compile OBSLink.dll (Windows x64). Prérequis : VS Build Tools 2022 (C++), CMake.
# Usage : powershell -File build.ps1
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$obsDir = 'C:\Program Files\obs-studio'
$work = Join-Path $root '_build'
$libDir = Join-Path $work 'implibs'
New-Item -ItemType Directory -Force $libDir | Out-Null

# 1) Environnement MSVC
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio Build Tools (C++) introuvable.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$envDump = cmd /c "`"$vcvars`" >nul && set"
foreach ($l in $envDump) { if ($l -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] } }

# 2) Bibliothèques d'import à partir des DLL d'OBS installées (obs.dll, obs-frontend-api.dll)
foreach ($name in 'obs', 'obs-frontend-api') {
    $lib = Join-Path $libDir "$name.lib"
    if (Test-Path $lib) { continue }
    $dll = Join-Path $obsDir "bin\64bit\$name.dll"
    $exports = (& dumpbin /exports $dll) | ForEach-Object {
        if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)') { $Matches[1] }
    }
    $def = Join-Path $libDir "$name.def"
    @("LIBRARY $name") + @('EXPORTS') + ($exports | ForEach-Object { "  $_" }) | Set-Content $def -Encoding ASCII
    & lib /nologo /def:$def /machine:x64 /out:$lib | Out-Null
    "Lib $name : $(@($exports).Count) fonctions"
}

# 3) CMake + compilation
$obsSrc = Join-Path $root '_src\obs'
$qt = Join-Path $root '_deps\qt6'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
& $cmake -S $root -B $work -G 'Visual Studio 17 2022' -A x64 `
    "-DOBS_INC=$obsSrc/libobs" "-DOBS_FRONTEND_INC=$obsSrc/frontend/api" "-DOBS_LIB_DIR=$libDir" `
    "-DCMAKE_PREFIX_PATH=$qt" "-DCMAKE_BUILD_TYPE=Release"
if ($LASTEXITCODE) { throw 'cmake configure a échoué' }
& $cmake --build $work --config Release
if ($LASTEXITCODE) { throw 'compilation échouée' }

$out = Join-Path $root 'dist\OBSLink\bin\64bit'
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item (Join-Path $work 'Release\OBSLink.dll') $out -Force
$dataOut = Join-Path $root 'dist\OBSLink\data\locale'
New-Item -ItemType Directory -Force $dataOut | Out-Null
Copy-Item (Join-Path $root 'data\locale\*.ini') $dataOut -Force
"OK : $out\OBSLink.dll"
