del /q dist\*
docker build -t xf16cam-build . && ^
docker create --name xf16cam-extract xf16cam-build && ^
docker cp xf16cam-extract:/workspace/dist ./dist && ^
docker rm xf16cam-extract

pause
