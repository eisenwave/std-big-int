@rem SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
@rem SPDX-License-Identifier: BSL-1.0
@rem
@rem Builds big_int_printer_msvc.dll, the Visual Studio debugger expression-
@rem evaluator add-in, from an already-built beman.big_int static library.
@rem
@rem   big_int_printer_msvc.bat <path\to\beman.big_int.lib> [amd64|x86|arm64]
@rem
@rem The architecture defaults to amd64 and must match both the library and the
@rem program you intend to debug. The library must have been built with the same
@rem options (limb width, BEMAN_BIG_INT_SIMD_MUL) as that program.
@rem
@rem CMake builds and installs the same DLL directly with
@rem -DBEMAN_BIG_INT_BUILD_MSVC_DEBUGGER_ADDIN=ON; see the "Debugger Visualizers"
@rem documentation page. This script exists for builds that do not use CMake.

@echo off
setlocal

set "BIGINTLIB=%~1"
set "ARCH=%~2"
if "%ARCH%"=="" set "ARCH=amd64"

if "%BIGINTLIB%"=="" (
    echo Usage: %~nx0 ^<path\to\beman.big_int.lib^> [amd64^|x86^|arm64]
    echo.
    echo Build the library first, for example:
    echo     cmake --preset msvc-release
    echo     cmake --build build\msvc-release
    exit /b 1
)
if not exist "%BIGINTLIB%" (
    echo Not found: %BIGINTLIB%
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Could not find vswhere.exe; is Visual Studio installed?
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (
    `"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`
) do set "VSPATH=%%i"

if not defined VSPATH (
    echo Could not find a Visual Studio installation with the C++ toolset.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
if errorlevel 1 exit /b 1

cl.exe ^
    /nologo ^
    /LD ^
    /std:c++latest ^
    /EHsc ^
    /O2 ^
    /MT ^
    /W4 ^
    /permissive- ^
    /Zc:__cplusplus ^
    /Fe:big_int_printer_msvc.dll ^
    /I"%~dp0..\include" ^
    "%~dp0big_int_printer_msvc.cpp" ^
    /link ^
    "%BIGINTLIB%"
if errorlevel 1 exit /b %errorlevel%

echo.
echo Built big_int_printer_msvc.dll
echo Copy it and big_int_printer_msvc.natvis into
echo   %%USERPROFILE%%\Documents\Visual Studio 2022\Visualizers\
echo (substituting your Visual Studio version), then restart debugging.
