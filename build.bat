@echo off
rem ---------------------------------------------------------------------------
rem Builds AnimatedWorld with Visual Studio 2022 (v143), which is what
rem CommonLibF4RD requires. Visual Studio 2019 ships v142 and cannot build it.
rem
rem   build.bat            -> Release
rem   build.bat Debug      -> Debug
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

if /i "%CONFIG%"=="Release" (
	set "BUILD_PRESET=vs2022-release"
) else if /i "%CONFIG%"=="Debug" (
	set "BUILD_PRESET=vs2022-debug"
) else (
	echo Unknown configuration "%CONFIG%". Use Release or Debug.
	exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
	echo Could not find vswhere.exe. Is Visual Studio installed?
	exit /b 1
)

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version "[17.0^,18.0)" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if not defined VSPATH (
	echo.
	echo Visual Studio 2022 with the C++ desktop workload was not found.
	echo CommonLibF4RD needs the v143 toolset; VS2019 only has v142.
	exit /b 1
)

echo Using Visual Studio at: !VSPATH!
call "!VSPATH!\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 (
	echo Failed to initialise the Visual Studio environment.
	exit /b 1
)

if not exist "%SCRIPT_DIR%external\CommonLibF4RD\CommonLibF4\CMakeLists.txt" (
	echo.
	echo CommonLibF4RD is missing. Run this once from %SCRIPT_DIR%:
	echo     git clone https://github.com/Zzyxz/CommonLibF4RD.git external/CommonLibF4RD
	exit /b 1
)

pushd "%SCRIPT_DIR%"

cmake --preset vs2022-windows-vcpkg
if errorlevel 1 goto :failed

cmake --build --preset %BUILD_PRESET%
if errorlevel 1 goto :failed

popd
echo.
echo Built %CONFIG%: %SCRIPT_DIR%build\vs2022\%CONFIG%\AnimatedWorld.dll
exit /b 0

:failed
popd
echo.
echo Build failed.
exit /b 1
