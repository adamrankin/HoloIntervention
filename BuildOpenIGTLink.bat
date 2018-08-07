@ECHO off

REM Build 32bit debug and release
mkdir .\UWPOpenIGTLink\OpenIGTLink-bin-Win32
pushd .\UWPOpenIGTLink\OpenIGTLink-bin-Win32
cmake -G "Visual Studio 15 2017" ..\OpenIGTLink -DBUILD_TESTING:BOOL=OFF
cmake --build . --config Release
cmake --build . --config Debug
popd

REM Build 64bit debug and release
mkdir .\UWPOpenIGTLink\OpenIGTLink-bin-x64
pushd .\UWPOpenIGTLink\OpenIGTLink-bin-x64
cmake -G "Visual Studio 15 2017 Win64" ..\OpenIGTLink -DBUILD_TESTING:BOOL=OFF
cmake --build . --config Release
cmake --build . --config Debug
popd