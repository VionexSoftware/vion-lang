param(
    [string]$InstallDir = "",
    [switch]$Yes
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\Vion"
}

$ManifestPath = Join-Path $InstallDir "install-manifest.json"
$BinDir = Join-Path $InstallDir "bin"

function Confirm-Uninstall {
    if ($Yes) {
        return
    }

    Write-Host "Vion uninstall will remove the CLI, user PATH entry, VS Code support, and Vion-owned starter files."
    Write-Host "Starter code is removed only if it still matches the original template."
    $Answer = Read-Host "Uninstall Vion now? [Y/n]"

    if (![string]::IsNullOrWhiteSpace($Answer) -and $Answer -notmatch "^(y|yes)$") {
        Write-Host "Uninstall cancelled."
        exit 0
    }
}

function Get-FileSha256 {
    param([string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        return ""
    }

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Remove-UserPathEntry {
    param([string]$PathEntry)

    if ([string]::IsNullOrWhiteSpace($PathEntry)) {
        return
    }

    $ResolvedPathEntry = [System.IO.Path]::GetFullPath($PathEntry).TrimEnd('\')
    $CurrentPath = [Environment]::GetEnvironmentVariable("Path", "User")

    if (![string]::IsNullOrWhiteSpace($CurrentPath)) {
        $Entries = @()

        foreach ($Entry in ($CurrentPath -split ";")) {
            if ([string]::IsNullOrWhiteSpace($Entry)) {
                continue
            }

            $ExpandedEntry = [Environment]::ExpandEnvironmentVariables($Entry)
            if (![System.IO.Path]::GetFullPath($ExpandedEntry).TrimEnd('\').Equals($ResolvedPathEntry, [System.StringComparison]::OrdinalIgnoreCase)) {
                $Entries += $Entry
            }
        }

        [Environment]::SetEnvironmentVariable("Path", ($Entries -join ";"), "User")
    }

    $ProcessEntries = $env:Path -split ";" | Where-Object {
        if ([string]::IsNullOrWhiteSpace($_)) {
            return $false
        }

        $ExpandedEntry = [Environment]::ExpandEnvironmentVariables($_)
        return ![System.IO.Path]::GetFullPath($ExpandedEntry).TrimEnd('\').Equals($ResolvedPathEntry, [System.StringComparison]::OrdinalIgnoreCase)
    }
    $env:Path = $ProcessEntries -join ";"

    Write-Host "Removed from user PATH: $ResolvedPathEntry"
}

function Remove-DirectoryIfSafe {
    param([string]$Directory)

    if ([string]::IsNullOrWhiteSpace($Directory) -or !(Test-Path -LiteralPath $Directory)) {
        return
    }

    Remove-Item -LiteralPath $Directory -Recurse -Force
    Write-Host "Removed directory: $Directory"
}

function Remove-StarterIfUnchanged {
    param(
        [string]$StarterFile,
        [string]$StarterDir,
        [string]$ExpectedSha256,
        [bool]$StarterCreated
    )

    if (!$StarterCreated -or [string]::IsNullOrWhiteSpace($StarterFile) -or !(Test-Path -LiteralPath $StarterFile)) {
        return
    }

    $ActualSha256 = Get-FileSha256 -Path $StarterFile
    if ($ActualSha256 -ne $ExpectedSha256) {
        Write-Host "Keeping starter file because it was changed: $StarterFile"
        return
    }

    Remove-Item -LiteralPath $StarterFile -Force
    Write-Host "Removed starter file: $StarterFile"

    if (![string]::IsNullOrWhiteSpace($StarterDir) -and (Test-Path -LiteralPath $StarterDir)) {
        $RemainingItems = Get-ChildItem -LiteralPath $StarterDir -Force
        if ($RemainingItems.Count -eq 0) {
            Remove-Item -LiteralPath $StarterDir -Force
            Write-Host "Removed empty starter directory: $StarterDir"
        }
    }
}

Confirm-Uninstall

$Manifest = $null
if (Test-Path -LiteralPath $ManifestPath) {
    $Manifest = Get-Content -Raw $ManifestPath | ConvertFrom-Json
}

if ($null -ne $Manifest) {
    Remove-UserPathEntry -PathEntry $Manifest.PathEntry

    if ($Manifest.VSCodeExtensionInstalled) {
        Remove-DirectoryIfSafe -Directory $Manifest.VSCodeExtensionDir
    }

    Remove-StarterIfUnchanged `
        -StarterFile $Manifest.StarterFile `
        -StarterDir $Manifest.StarterDir `
        -ExpectedSha256 $Manifest.StarterSha256 `
        -StarterCreated ([bool]$Manifest.StarterCreated)
} else {
    Remove-UserPathEntry -PathEntry $BinDir
}

if (Test-Path -LiteralPath $InstallDir) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
    Write-Host "Removed Vion installation: $InstallDir"
} else {
    Write-Host "Vion installation was not found: $InstallDir"
}
