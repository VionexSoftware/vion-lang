param(
    [string]$Repo = "AlexanderPhan04/vion-lang",
    [string]$Version = "latest",
    [string]$InstallDir = "",
    [string]$StarterDir = "",
    [string]$AssetPattern = "vion-*-Windows.zip",
    [string]$ZipUrl = "",
    [switch]$NoPath,
    [switch]$NoEditor,
    [switch]$NoStarter,
    [switch]$NoOpen,
    [switch]$Yes
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\Vion"
}

if ([string]::IsNullOrWhiteSpace($StarterDir)) {
    $StarterDir = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Vion"
}

$BinDir = Join-Path $InstallDir "bin"
$DocsDir = Join-Path $InstallDir "docs"
$ExamplesDir = Join-Path $InstallDir "examples"
$ManifestPath = Join-Path $InstallDir "install-manifest.json"
$InstallerUserAgent = "VionOnlineInstaller"

function Confirm-Install {
    if ($Yes) {
        return
    }

    Write-Host "Vion will install for the current Windows user only. No administrator permission is required."
    Write-Host "The installer can update user PATH, install VS Code support, and create a starter project."
    $Answer = Read-Host "Allow Vion to make these changes? [Y/n]"

    if (![string]::IsNullOrWhiteSpace($Answer) -and $Answer -notmatch "^(y|yes)$") {
        Write-Host "Install cancelled."
        exit 0
    }
}

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
            return $false
        }
    }

    $NewPath = (($Entries + $ResolvedEntry) -join ";")
    [Environment]::SetEnvironmentVariable("Path", $NewPath, "User")
    $env:Path = $env:Path + ";" + $ResolvedEntry

    Write-Host "Added to user PATH: $ResolvedEntry"
    Write-Host "The vion command is available in this PowerShell session and new terminals."
    return $true
}

function Get-FileSha256 {
    param([string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        return ""
    }

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Save-InstallManifest {
    param([hashtable]$Manifest)

    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $Manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 $ManifestPath
    Write-Host "Saved install manifest: $ManifestPath"
}

function Get-ReleaseAssetUrl {
    param(
        [string]$Repository,
        [string]$ReleaseVersion,
        [string]$Pattern
    )

    if ($ReleaseVersion -eq "latest") {
        $ReleaseApiUrl = "https://api.github.com/repos/$Repository/releases/latest"
    } else {
        $ReleaseApiUrl = "https://api.github.com/repos/$Repository/releases/tags/$ReleaseVersion"
    }

    Write-Host "Checking GitHub release: $ReleaseApiUrl"
    $Release = Invoke-RestMethod -Uri $ReleaseApiUrl -Headers @{ "User-Agent" = $InstallerUserAgent }
    $Assets = @($Release.assets)
    $Asset = $Assets | Where-Object { $_.name -like $Pattern } | Select-Object -First 1

    if ($null -eq $Asset) {
        $AvailableAssets = ($Assets | ForEach-Object { $_.name }) -join ", "
        throw "Could not find release asset matching '$Pattern'. Available assets: $AvailableAssets"
    }

    Write-Host "Selected release asset: $($Asset.name)"
    return $Asset.browser_download_url
}

function Copy-FirstMatchingFile {
    param(
        [string]$Root,
        [string]$FileName,
        [string]$Destination
    )

    $File = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $FileName | Select-Object -First 1
    if ($null -ne $File) {
        Copy-Item -LiteralPath $File.FullName -Destination $Destination -Force
    }
}

function Install-VSCodeExtension {
    param([string]$Root)

    $PackageJson = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter "package.json" |
        Where-Object {
            try {
                $Package = Get-Content -Raw $_.FullName | ConvertFrom-Json
                return $Package.name -eq "vion-language"
            } catch {
                return $false
            }
        } |
        Select-Object -First 1

    if ($null -eq $PackageJson) {
        Write-Host "VS Code extension package was not found in this release."
        return ""
    }

    $ExtensionSource = Split-Path -Parent $PackageJson.FullName
    $ExtensionsDir = Join-Path $env:USERPROFILE ".vscode\extensions"
    $ExtensionDir = Join-Path $ExtensionsDir "vionex.vion-language-0.2.0"

    New-Item -ItemType Directory -Force -Path $ExtensionsDir | Out-Null

    if (Test-Path $ExtensionDir) {
        Remove-Item -LiteralPath $ExtensionDir -Recurse -Force
    }

    Copy-Item -LiteralPath $ExtensionSource -Destination $ExtensionDir -Recurse -Force
    Write-Host "Installed VS Code support: $ExtensionDir"
    return $ExtensionDir
}

function New-StarterProject {
    param([string]$Directory)

    New-Item -ItemType Directory -Force -Path $Directory | Out-Null

    $MainFile = Join-Path $Directory "main.vion"
    $Created = $false
    if (!(Test-Path $MainFile)) {
        $StarterSource = @'
fn greet(name) {
  return "Hello, " + name
}

print greet("Vion")
'@
        $Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($MainFile, $StarterSource, $Utf8NoBom)
        $Created = $true
        Write-Host "Created starter file: $MainFile"
    } else {
        Write-Host "Starter file already exists: $MainFile"
    }

    return @{
        File = $MainFile
        Created = $Created
        Sha256 = Get-FileSha256 -Path $MainFile
    }
}

function Open-StarterProject {
    param([string]$Directory)

    $CodeCommand = Get-Command "code" -ErrorAction SilentlyContinue
    if ($null -eq $CodeCommand) {
        Write-Host "VS Code command 'code' was not found. Open this folder manually: $Directory"
        return
    }

    Write-Host "Opening starter project in VS Code: $Directory"
    & code $Directory
}

$TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("vion-install-" + [System.Guid]::NewGuid().ToString("N"))
$ZipPath = Join-Path $TempRoot "vion.zip"
$ExtractDir = Join-Path $TempRoot "extract"

try {
    Confirm-Install

    New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $ExtractDir | Out-Null

    if ([string]::IsNullOrWhiteSpace($ZipUrl)) {
        $ZipUrl = Get-ReleaseAssetUrl -Repository $Repo -ReleaseVersion $Version -Pattern $AssetPattern
    }

    if (Test-Path -LiteralPath $ZipUrl) {
        Write-Host "Using local package $ZipUrl"
        Copy-Item -LiteralPath $ZipUrl -Destination $ZipPath -Force
    } else {
        Write-Host "Downloading Vion from $ZipUrl"
        Invoke-WebRequest -Uri $ZipUrl -OutFile $ZipPath -UseBasicParsing -Headers @{ "User-Agent" = $InstallerUserAgent }
    }

    Write-Host "Extracting package"
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $ExtractDir -Force

    $VionExe = Get-ChildItem -LiteralPath $ExtractDir -Recurse -File -Filter "vion.exe" | Select-Object -First 1
    if ($null -eq $VionExe) {
        throw "The downloaded package did not contain vion.exe."
    }

    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    New-Item -ItemType Directory -Force -Path $DocsDir | Out-Null
    New-Item -ItemType Directory -Force -Path $ExamplesDir | Out-Null

    Copy-Item -LiteralPath $VionExe.FullName -Destination (Join-Path $BinDir "vion.exe") -Force

    foreach ($DocName in @("LICENSE", "README.md", "INSTALL.md", "LANGUAGE_SPEC.md", "ROADMAP.md", "uninstall-windows.ps1")) {
        Copy-FirstMatchingFile -Root $ExtractDir -FileName $DocName -Destination $DocsDir
    }

    $ExampleFiles = Get-ChildItem -LiteralPath $ExtractDir -Recurse -File -Filter "*.vion" |
        Where-Object { $_.FullName -match "[/\\]examples[/\\]" }

    foreach ($ExampleFile in $ExampleFiles) {
        Copy-Item -LiteralPath $ExampleFile.FullName -Destination $ExamplesDir -Force
    }

    if (!$NoEditor) {
        $VSCodeExtensionDir = Install-VSCodeExtension -Root $ExtractDir
    } else {
        $VSCodeExtensionDir = ""
    }

    $Starter = $null
    if (!$NoStarter) {
        $Starter = New-StarterProject -Directory $StarterDir
    }

    $PathAdded = $false
    if (!$NoPath) {
        $PathAdded = Add-UserPathEntry -PathEntry $BinDir
    }

    Save-InstallManifest -Manifest @{
        Version = "0.2.0"
        InstalledAt = (Get-Date).ToString("o")
        InstallDir = [System.IO.Path]::GetFullPath($InstallDir)
        BinDir = [System.IO.Path]::GetFullPath($BinDir)
        PathEntry = [System.IO.Path]::GetFullPath($BinDir).TrimEnd('\')
        PathAdded = [bool]$PathAdded
        DocsDir = [System.IO.Path]::GetFullPath($DocsDir)
        ExamplesDir = [System.IO.Path]::GetFullPath($ExamplesDir)
        VSCodeExtensionDir = $VSCodeExtensionDir
        VSCodeExtensionInstalled = ![string]::IsNullOrWhiteSpace($VSCodeExtensionDir)
        StarterDir = if ($null -ne $Starter) { [System.IO.Path]::GetFullPath($StarterDir) } else { "" }
        StarterFile = if ($null -ne $Starter) { [System.IO.Path]::GetFullPath($Starter.File) } else { "" }
        StarterCreated = if ($null -ne $Starter) { [bool]$Starter.Created } else { $false }
        StarterSha256 = if ($null -ne $Starter) { $Starter.Sha256 } else { "" }
    }

    Write-Host "Installed Vion to $InstallDir"
    & (Join-Path $BinDir "vion.exe") version
    if ($null -ne $Starter) {
        Write-Host "Try: vion $($Starter.File)"
    } else {
        Write-Host "Try: vion $ExamplesDir\hello.vion"
    }

    if (!$NoOpen -and !$NoStarter) {
        Open-StarterProject -Directory $StarterDir
    }
} finally {
    if (Test-Path $TempRoot) {
        Remove-Item -LiteralPath $TempRoot -Recurse -Force
    }
}
