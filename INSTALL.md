# Install Vion

This guide installs the Vion CLI so you can run Vion programs from any terminal:

```powershell
vion main.vion
```

## Windows User Install

### Online install without cloning

After a GitHub Release is published with `vion-*-Windows.zip`, users can install Vion with one command:

```powershell
irm https://raw.githubusercontent.com/AlexanderPhan04/vion-lang/main/scripts/install-online-windows.ps1 | iex
```

The online installer:

- asks once before changing user-level settings
- downloads the latest GitHub Release
- copies `vion.exe` to `%LOCALAPPDATA%\Programs\Vion\bin`
- copies docs and examples to `%LOCALAPPDATA%\Programs\Vion`
- installs Vion VS Code syntax and file icon support
- creates `Documents\Vion\main.vion`
- opens the starter project in VS Code when the `code` command is available
- adds `%LOCALAPPDATA%\Programs\Vion\bin` to your user `PATH`
- saves an install manifest so uninstall can remove Vion-owned changes later

After installation, verify:

```powershell
vion -v
vion main.vion
```

Run the same command again to update to the latest GitHub Release.

Install without prompts:

```powershell
$installer = "$env:TEMP\install-vion.ps1"
irm https://raw.githubusercontent.com/AlexanderPhan04/vion-lang/main/scripts/install-online-windows.ps1 -OutFile $installer
powershell -ExecutionPolicy Bypass -File $installer -Yes
```

Install without opening VS Code:

```powershell
$installer = "$env:TEMP\install-vion.ps1"
irm https://raw.githubusercontent.com/AlexanderPhan04/vion-lang/main/scripts/install-online-windows.ps1 -OutFile $installer
powershell -ExecutionPolicy Bypass -File $installer -NoOpen
```

Install a specific release tag:

```powershell
$installer = "$env:TEMP\install-vion.ps1"
irm https://raw.githubusercontent.com/AlexanderPhan04/vion-lang/main/scripts/install-online-windows.ps1 -OutFile $installer
powershell -ExecutionPolicy Bypass -File $installer -Version v0.2.0
```

### Local source install

From a cloned repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-windows.ps1
```

The installer:

- builds Vion in `Release` mode
- copies `vion.exe` to `%LOCALAPPDATA%\Programs\Vion\bin`
- copies docs and examples to `%LOCALAPPDATA%\Programs\Vion`
- adds `%LOCALAPPDATA%\Programs\Vion\bin` to your user `PATH`

Open a new terminal after installation, then verify:

```powershell
vion version
```

Run a program:

```powershell
vion .\examples\hello.vion
```

Or create `main.vion`:

```vion
print "Hello from Vion"
```

Then run:

```powershell
vion main.vion
```

## Custom Install Directory

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-windows.ps1 -InstallDir "C:\Tools\Vion"
```

## Install Without Editing PATH

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-windows.ps1 -NoPath
```

You can still run the executable directly:

```powershell
%LOCALAPPDATA%\Programs\Vion\bin\vion.exe main.vion
```

## Uninstall

```powershell
powershell -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\Vion\docs\uninstall-windows.ps1"
```

Uninstall removes:

- the Vion CLI install directory
- the Vion user `PATH` entry
- the Vion VS Code extension folder installed by Vion
- the starter `main.vion` only if it still matches the original template

Uninstall without prompts:

```powershell
powershell -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\Vion\docs\uninstall-windows.ps1" -Yes
```

## CMake Install

You can also install using CMake directly:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix "%LOCALAPPDATA%\Programs\Vion"
```

This copies the executable, docs, and examples, but it does not modify `PATH`.

## Create a ZIP Package

```powershell
cmake --build build --config Release
cpack --config build\CPackConfig.cmake -C Release
```

The generated `.zip` can be uploaded to a GitHub Release. The online installer expects an asset name like:

```text
vion-0.2.0-Windows.zip
```
