# EchoFrame

EchoFrame is a Windows Geode 5.10.1 mod for Geometry Dash 2.2081. It combines the AutoDeafen `PlayLayer` controller with the Frame Extrapolation `GJBaseGameLayer` render hooks. Left Alt opens the common menu.

The build uses MSVC, Ninja, CMake, and a Geode 5.10.1 SDK. Set `GEODE_SDK` to an SDK checkout or use the source checkout in the adjacent `ИСХОДНИКИ/Autodeafen/vendor` folder. The package target creates `echoframe.geode` in this directory. It normalizes the ZIP after `geode package new`. When updating the installed archive, write its bytes directly to the existing `mods/echoframe.geode` so its modification time advances past Geode's cached extraction in `geode/unzipped`.

The Description includes UTF-8 Russian text and a matching Cyrillic image resource. Geode's bitmap markdown font did not display Cyrillic from the two reference mods reliably, so the image ensures the complete Russian text remains readable in the Description tab.
