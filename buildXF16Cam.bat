@echo off
set "BUILD_VARIANT=%~1"
if "%BUILD_VARIANT%"=="" set "BUILD_VARIANT=ptz"
if /I not "%BUILD_VARIANT%"=="ptz" if /I not "%BUILD_VARIANT%"=="no_ptz" (
	echo Usage: %~nx0 [ptz^|no_ptz]
	exit /b 1
)

del /q dist\* 2>nul
docker build --build-arg BUILD_VARIANT=%BUILD_VARIANT% -t xf16cam-build . && ^
docker create --name xf16cam-extract xf16cam-build && ^
docker cp xf16cam-extract:/workspace/dist ./dist && ^
docker rm xf16cam-extract

pause
