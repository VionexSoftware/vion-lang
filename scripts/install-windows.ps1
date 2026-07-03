param(
    [string]$InstallDir = "",
    [string]$Configuration = "Release",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Architecture = "x64",
    [switch]$SkipBuild,
    [switch]$NoPath
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\Vion"
}

$BuildDir = Join-Path $ProjectRoot "build"
$BinDir = Join-Path $InstallDir "bin"
$DocsDir = Join-Path $InstallDir "docs"
$ExamplesDir = Join-Path $InstallDir "examples"

function Add-UserPathEntry {
    param([string]$PathEntry)

    $ResolvedEntry = [System.IO.Path]::GetFullPath($PathEntry).TrimEnd('\')
    $CurrentPath = [Environment]::GetEnvironmentVariable("Path", "User")

    if ([string]::IsNullOrWhiteSpace($CurrentPath)) {
        $Entries = @()
    } else {
        $Entries = $CurrentPath -split ";" | Where-Object { ![string]::IsNullOrWhiteSpace($_) }
    }

    foreach ($Entry in $Entries) {
        $ExpandedEntry = [Environment]::ExpandEnvironmentVariables($Entry)
        if ([System.IO.Path]::GetFullPath($ExpandedEntry).TrimEnd('\').Equals($ResolvedEntry, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "PATH already contains $ResolvedEntry"
            return
        }
    }

    $NewPath = (($Entries + $ResolvedEntry) -join ";")
    [Environment]::SetEnvironmentVariable("Path", $NewPath, "User")
    $env:Path = $env:Path + ";" + $ResolvedEntry

    Write-Host "Added to user PATH: $ResolvedEntry"
    Write-Host "Open a new terminal before running vion from anywhere."
}

function Find-BuiltExecutable {
    param(
        [string]$Directory,
        [string]$BuildConfiguration
    )

    $Candidates = @(
        (Join-Path $Directory "$BuildConfiguration\vion.exe"),
        (Join-Path $Directory "vion.exe")
    )

    foreach ($Candidate in $Candidates) {
        if (Test-Path $Candidate) {
            return $Candidate
        }
    }

    throw "Could not find vion.exe in $Directory. Build may have failed."
}

if (!$SkipBuild) {
    if (!(Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
        cmake -S $ProjectRoot -B $BuildDir -G $Generator -A $Architecture
    } else {
        cmake -S $ProjectRoot -B $BuildDir
    }

    cmake --build $BuildDir --config $Configuration
}

$ExePath = Find-BuiltExecutable -Directory $BuildDir -BuildConfiguration $Configuration

New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
New-Item -ItemType Directory -Force -Path $DocsDir | Out-Null
New-Item -ItemType Directory -Force -Path $ExamplesDir | Out-Null

Copy-Item -LiteralPath $ExePath -Destination (Join-Path $BinDir "vion.exe") -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot "LICENSE") -Destination $DocsDir -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot "README.md") -Destination $DocsDir -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot "INSTALL.md") -Destination $DocsDir -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot "LANGUAGE_SPEC.md") -Destination $DocsDir -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot "ROADMAP.md") -Destination $DocsDir -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot "scripts\uninstall-windows.ps1") -Destination $DocsDir -Force
Copy-Item -Path (Join-Path $ProjectRoot "examples\*.vion") -Destination $ExamplesDir -Force

if (!$NoPath) {
    Add-UserPathEntry -PathEntry $BinDir
}

Write-Host "Installed Vion to $InstallDir"
& (Join-Path $BinDir "vion.exe") version
Write-Host "Try: vion $ExamplesDir\hello.vion"
