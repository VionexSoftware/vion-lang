param(
    [string]$Repo          = "AlexanderPhan04/vion-lang",
    [string]$Version       = "latest",
    [string]$InstallDir    = "",
    [string]$StarterDir    = "",
    [string]$AssetPattern  = "vion-*-Windows.zip",
    [string]$ZipUrl        = "",
    [switch]$NoPath,
    [switch]$NoEditor,
    [switch]$NoStarter,
    [switch]$NoOpen,
    [switch]$Yes
)

$ErrorActionPreference = "Stop"

# ─────────────────────────────────────────────────────────────────────────────
#  Color helpers
# ─────────────────────────────────────────────────────────────────────────────

function Write-Color {
    param([string]$Text, [ConsoleColor]$Color = "White", [switch]$NoNewline)
    $prev = [Console]::ForegroundColor
    [Console]::ForegroundColor = $Color
    if ($NoNewline) { Write-Host $Text -NoNewline } else { Write-Host $Text }
    [Console]::ForegroundColor = $prev
}

function Write-Dim   { param([string]$T) Write-Color $T -Color DarkGray }
function Write-Cyan  { param([string]$T) Write-Color $T -Color Cyan }
function Write-Green { param([string]$T) Write-Color $T -Color Green }
function Write-Red   { param([string]$T) Write-Color $T -Color Red }
function Write-Yellow{ param([string]$T) Write-Color $T -Color Yellow }

function Write-Step {
    param([int]$N, [int]$Total, [string]$Label)
    Write-Host ""
    Write-Color " [$N/$Total] " -Color DarkCyan -NoNewline
    Write-Color $Label -Color White
}

function Write-OK   { param([string]$Msg) Write-Color "   ✔  $Msg" -Color Green }
function Write-Info { param([string]$Msg) Write-Color "   →  $Msg" -Color DarkGray }
function Write-Warn { param([string]$Msg) Write-Color "   ⚠  $Msg" -Color Yellow }
function Write-Fail { param([string]$Msg) Write-Color "   ✘  $Msg" -Color Red }

# ─────────────────────────────────────────────────────────────────────────────
#  Spinner
# ─────────────────────────────────────────────────────────────────────────────

$script:SpinnerJob   = $null
$script:SpinnerLabel = ""
$SpinFrames = @("⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏")

function Start-Spinner {
    param([string]$Label)
    $script:SpinnerLabel = $Label
    $script:SpinnerJob = [System.Threading.Tasks.Task]::Run([System.Action]{
        $i = 0
        while ($true) {
            $frame = ("⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏")[$i % 10]
            [Console]::Write("`r   $frame  $using:SpinnerLabel ")
            Start-Sleep -Milliseconds 80
            $i++
        }
    })
}

# Simpler spinner using a background runspace
$script:SpinnerRunspace = $null
$script:SpinnerStop     = $false

function Start-Spin {
    param([string]$Label)
    $script:SpinnerStop = $false
    $rs = [runspacefactory]::CreateRunspace()
    $rs.Open()
    $rs.SessionStateProxy.SetVariable("Label", $Label)
    $rs.SessionStateProxy.SetVariable("StopRef", [ref]$script:SpinnerStop)
    $ps = [powershell]::Create()
    $ps.Runspace = $rs
    [void]$ps.AddScript({
        $frames = [char[]]@(0x280B, 0x2819, 0x2839, 0x2838, 0x283C, 0x2834, 0x2826, 0x2827, 0x2807, 0x280F)
        $i = 0
        while (-not $StopRef.Value) {
            $f = $frames[$i % $frames.Length]
            [Console]::Write("`r   $f  $Label  ")
            Start-Sleep -Milliseconds 80
            $i++
        }
    })
    $script:SpinnerRunspace = @{ PS = $ps; RS = $rs; Handle = $ps.BeginInvoke() }
}

function Stop-Spin {
    param([string]$DoneMsg = "", [switch]$Fail)
    $script:SpinnerStop = $true
    Start-Sleep -Milliseconds 120
    [Console]::Write("`r" + " " * 60 + "`r")
    if ($DoneMsg) {
        if ($Fail) { Write-Fail $DoneMsg } else { Write-OK $DoneMsg }
    }
    if ($script:SpinnerRunspace) {
        try { $script:SpinnerRunspace.PS.Stop() } catch {}
        $script:SpinnerRunspace.RS.Close()
        $script:SpinnerRunspace = $null
    }
}

# ─────────────────────────────────────────────────────────────────────────────
#  Progress bar for download
# ─────────────────────────────────────────────────────────────────────────────

function Show-ProgressBar {
    param([long]$Current, [long]$Total, [int]$Width = 36)
    if ($Total -le 0) { return }
    $pct   = [int](($Current / $Total) * 100)
    $filled= [int](($Current / $Total) * $Width)
    $bar   = ("█" * $filled) + ("░" * ($Width - $filled))
    $kb    = [math]::Round($Current / 1024)
    $totalKb = [math]::Round($Total / 1024)
    [Console]::Write("`r   [$bar] $pct% ($kb/$totalKb KB) ")
}

function Invoke-WebRequestWithProgress {
    param([string]$Uri, [string]$OutFile)

    $req = [System.Net.HttpWebRequest]::Create($Uri)
    $req.UserAgent = "VionOnlineInstaller"
    $req.AllowAutoRedirect = $true
    $res = $req.GetResponse()
    $total = $res.ContentLength

    $stream = $res.GetResponseStream()
    $fs     = [System.IO.File]::Create($OutFile)
    $buf    = New-Object byte[] 65536
    $downloaded = 0

    Write-Host ""
    try {
        while (($read = $stream.Read($buf, 0, $buf.Length)) -gt 0) {
            $fs.Write($buf, 0, $read)
            $downloaded += $read
            Show-ProgressBar -Current $downloaded -Total $total
        }
    } finally {
        $fs.Close()
        $stream.Close()
        $res.Close()
    }
    [Console]::Write("`r" + " " * 60 + "`r")
}

# ─────────────────────────────────────────────────────────────────────────────
#  Banner
# ─────────────────────────────────────────────────────────────────────────────

function Show-Banner {
    Write-Host ""
    Write-Color "  ██╗   ██╗██╗ ██████╗ ███╗  ██╗" -Color Cyan
    Write-Color "  ██║   ██║██║██╔═══██╗████╗ ██║" -Color Cyan
    Write-Color "  ╚██╗ ██╔╝██║██║   ██║██╔██╗██║" -Color Cyan
    Write-Color "   ╚████╔╝ ██║██║   ██║██║╚████║" -Color Cyan
    Write-Color "    ╚██╔╝  ██║╚██████╔╝██║ ╚███║" -Color Cyan
    Write-Color "     ╚═╝   ╚═╝ ╚═════╝ ╚═╝  ╚══╝" -Color DarkCyan
    Write-Host ""
    Write-Color "  Vion Language Installer" -Color White
    Write-Dim   "  A modern scripting language built in C++17"
    Write-Host ""
    Write-Color "  ─────────────────────────────────────────" -Color DarkGray
    Write-Host ""
}

# ─────────────────────────────────────────────────────────────────────────────
#  Confirm prompt
# ─────────────────────────────────────────────────────────────────────────────

function Confirm-Install {
    if ($Yes) { return }

    Write-Dim   "  This installer will:"
    Write-Dim   "    • Install vion.exe to your user profile (no admin required)"
    Write-Dim   "    • Add vion to your PATH"
    Write-Dim   "    • Install the VS Code extension"
    Write-Dim   "    • Create a starter project in Documents\Vion"
    Write-Host ""
    Write-Color "  Allow Vion to make these changes? " -Color White -NoNewline
    Write-Color "[Y/n] " -Color DarkCyan -NoNewline
    $Answer = Read-Host

    if (![string]::IsNullOrWhiteSpace($Answer) -and $Answer -notmatch "^(y|yes)$") {
        Write-Host ""
        Write-Warn "Install cancelled."
        Write-Host ""
        exit 0
    }
    Write-Host ""
}

# ─────────────────────────────────────────────────────────────────────────────
#  Utility functions (unchanged logic, better messages)
# ─────────────────────────────────────────────────────────────────────────────

function Add-UserPathEntry {
    param([string]$PathEntry)
    $ResolvedEntry = [System.IO.Path]::GetFullPath($PathEntry).TrimEnd('\')
    $CurrentPath   = [Environment]::GetEnvironmentVariable("Path", "User")
    $Entries       = if ([string]::IsNullOrWhiteSpace($CurrentPath)) { @() }
                     else { $CurrentPath -split ";" | Where-Object { ![string]::IsNullOrWhiteSpace($_) } }

    foreach ($Entry in $Entries) {
        $Expanded = [Environment]::ExpandEnvironmentVariables($Entry)
        if ([System.IO.Path]::GetFullPath($Expanded).TrimEnd('\').Equals($ResolvedEntry, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Info "PATH already contains $ResolvedEntry"
            return $false
        }
    }

    $NewPath = (($Entries + $ResolvedEntry) -join ";")
    [Environment]::SetEnvironmentVariable("Path", $NewPath, "User")
    $env:Path = $env:Path + ";" + $ResolvedEntry
    Write-OK "Added to user PATH: $ResolvedEntry"
    return $true
}

function Get-FileSha256 {
    param([string]$Path)
    if (!(Test-Path -LiteralPath $Path)) { return "" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Save-InstallManifest {
    param([hashtable]$Manifest)
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $Manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 $ManifestPath
    Write-OK "Saved manifest: $ManifestPath"
}

function Get-ReleaseAssetUrl {
    param([string]$Repository, [string]$ReleaseVersion, [string]$Pattern)
    $ApiUrl  = if ($ReleaseVersion -eq "latest") {
        "https://api.github.com/repos/$Repository/releases/latest"
    } else {
        "https://api.github.com/repos/$Repository/releases/tags/$ReleaseVersion"
    }
    Write-Info "Fetching release info..."
    $Release = Invoke-RestMethod -Uri $ApiUrl -Headers @{ "User-Agent" = "VionOnlineInstaller" }
    $Asset   = @($Release.assets) | Where-Object { $_.name -like $Pattern } | Select-Object -First 1
    if ($null -eq $Asset) {
        $Available = (@($Release.assets) | ForEach-Object { $_.name }) -join ", "
        throw "Could not find asset matching '$Pattern'. Available: $Available"
    }
    Write-OK "Found release asset: $($Asset.name)"
    return $Asset.browser_download_url
}

function Copy-FirstMatchingFile {
    param([string]$Root, [string]$FileName, [string]$Destination)
    $File = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $FileName | Select-Object -First 1
    if ($null -ne $File) { Copy-Item -LiteralPath $File.FullName -Destination $Destination -Force }
}

function Install-VSCodeExtension {
    param([string]$Root)
    $PackageJson = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter "package.json" |
        Where-Object {
            try { $p = Get-Content -Raw $_.FullName | ConvertFrom-Json; return $p.name -eq "vion-language" }
            catch { return $false }
        } | Select-Object -First 1

    if ($null -eq $PackageJson) { Write-Warn "VS Code extension not found in release."; return "" }

    $ExtSrc = Split-Path -Parent $PackageJson.FullName
    $ExtDir = Join-Path $env:USERPROFILE ".vscode\extensions\vionex.vion-language-0.2.0"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ExtDir) | Out-Null
    if (Test-Path $ExtDir) { Remove-Item -LiteralPath $ExtDir -Recurse -Force }
    Copy-Item -LiteralPath $ExtSrc -Destination $ExtDir -Recurse -Force
    Write-OK "Installed VS Code extension → $ExtDir"
    return $ExtDir
}

function New-StarterProject {
    param([string]$Directory)
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    $MainFile = Join-Path $Directory "main.vion"
    $Created  = $false
    if (!(Test-Path $MainFile)) {
        $Src = @'
fn greet(name) {
  return "Hello, " + name
}

print greet("Vion")
'@
        $Enc = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($MainFile, $Src, $Enc)
        $Created = $true
        Write-OK "Created starter: $MainFile"
    } else {
        Write-Info "Starter already exists: $MainFile"
    }
    return @{ File = $MainFile; Created = $Created; Sha256 = (Get-FileSha256 $MainFile) }
}

function Open-StarterProject {
    param([string]$Directory)
    $CodeCmd = Get-Command "code" -ErrorAction SilentlyContinue
    if ($null -eq $CodeCmd) { Write-Warn "VS Code 'code' not found. Open manually: $Directory"; return }
    Write-OK "Opening in VS Code: $Directory"
    & code $Directory
}

# ─────────────────────────────────────────────────────────────────────────────
#  Defaults
# ─────────────────────────────────────────────────────────────────────────────

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\Vion"
}
if ([string]::IsNullOrWhiteSpace($StarterDir)) {
    $StarterDir = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Vion"
}

$BinDir       = Join-Path $InstallDir "bin"
$DocsDir      = Join-Path $InstallDir "docs"
$ExamplesDir  = Join-Path $InstallDir "examples"
$ManifestPath = Join-Path $InstallDir "install-manifest.json"
$TempRoot     = Join-Path ([System.IO.Path]::GetTempPath()) ("vion-install-" + [System.Guid]::NewGuid().ToString("N"))
$ZipPath      = Join-Path $TempRoot "vion.zip"
$ExtractDir   = Join-Path $TempRoot "extract"
$TotalSteps   = 6

# ─────────────────────────────────────────────────────────────────────────────
#  MAIN
# ─────────────────────────────────────────────────────────────────────────────

try {
    Show-Banner
    Confirm-Install

    New-Item -ItemType Directory -Force -Path $TempRoot   | Out-Null
    New-Item -ItemType Directory -Force -Path $ExtractDir | Out-Null

    # ── Step 1: Resolve download URL ─────────────────────────────────────────
    Write-Step 1 $TotalSteps "Resolving release"
    if ([string]::IsNullOrWhiteSpace($ZipUrl)) {
        $ZipUrl = Get-ReleaseAssetUrl -Repository $Repo -ReleaseVersion $Version -Pattern $AssetPattern
    } else {
        Write-Info "Using provided URL"
    }

    # ── Step 2: Download ─────────────────────────────────────────────────────
    Write-Step 2 $TotalSteps "Downloading Vion"
    if (Test-Path -LiteralPath $ZipUrl) {
        Write-Info "Using local package: $ZipUrl"
        Copy-Item -LiteralPath $ZipUrl -Destination $ZipPath -Force
        Write-OK "Package ready"
    } else {
        Write-Info "From: $ZipUrl"
        Invoke-WebRequestWithProgress -Uri $ZipUrl -OutFile $ZipPath
        Write-OK "Download complete"
    }

    # ── Step 3: Extract & install binary ─────────────────────────────────────
    Write-Step 3 $TotalSteps "Installing binary"
    Start-Spin "Extracting package..."
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $ExtractDir -Force
    Stop-Spin "Package extracted"

    $VionExe = Get-ChildItem -LiteralPath $ExtractDir -Recurse -File -Filter "vion.exe" | Select-Object -First 1
    if ($null -eq $VionExe) { throw "The downloaded package did not contain vion.exe." }

    New-Item -ItemType Directory -Force -Path $BinDir      | Out-Null
    New-Item -ItemType Directory -Force -Path $DocsDir     | Out-Null
    New-Item -ItemType Directory -Force -Path $ExamplesDir | Out-Null

    $DestExe = Join-Path $BinDir "vion.exe"
    $TempExe = Join-Path $BinDir "vion_new.exe"
    $OldExe  = Join-Path $BinDir "vion_old.exe"

    Copy-Item -LiteralPath $VionExe.FullName -Destination $TempExe -Force

    $IsLocked = $false
    if (Test-Path $DestExe) {
        try {
            $fs = [System.IO.File]::Open($DestExe, 'Open', 'ReadWrite', 'None')
            $fs.Close()
        } catch { $IsLocked = $true }
    }

    if ($IsLocked) {
        $BatchContent = "@echo off`r`ntimeout /t 1 /nobreak > nul`r`nmove /y `"$TempExe`" `"$DestExe`"`r`ndel `"$OldExe`" 2>nul`r`n"
        $BatchPath = Join-Path $env:TEMP "vion_update.bat"
        [System.IO.File]::WriteAllText($BatchPath, $BatchContent, [System.Text.Encoding]::ASCII)
        Start-Process -FilePath "cmd.exe" -ArgumentList "/c `"$BatchPath`"" -WindowStyle Hidden
        Write-Warn "Binary in use — will be replaced in 1 second after installer exits"
    } else {
        Move-Item -LiteralPath $TempExe -Destination $DestExe -Force
        Write-OK "Installed vion.exe → $DestExe"
    }

    foreach ($DocName in @("LICENSE","README.md","INSTALL.md","LANGUAGE_SPEC.md","ROADMAP.md","uninstall-windows.ps1")) {
        Copy-FirstMatchingFile -Root $ExtractDir -FileName $DocName -Destination $DocsDir
    }

    $ExampleFiles = Get-ChildItem -LiteralPath $ExtractDir -Recurse -File -Filter "*.vion" |
        Where-Object { $_.FullName -match "[/\\]examples[/\\]" }
    foreach ($f in $ExampleFiles) { Copy-Item -LiteralPath $f.FullName -Destination $ExamplesDir -Force }

    # ── Step 4: VS Code extension ─────────────────────────────────────────────
    Write-Step 4 $TotalSteps "Installing VS Code extension"
    $VSCodeExtensionDir = ""
    if (!$NoEditor) {
        Start-Spin "Installing extension..."
        $VSCodeExtensionDir = Install-VSCodeExtension -Root $ExtractDir
        Stop-Spin
    } else {
        Write-Info "Skipped (--NoEditor)"
    }

    # ── Step 5: Starter project + PATH ───────────────────────────────────────
    Write-Step 5 $TotalSteps "Setting up environment"
    $Starter = $null
    if (!$NoStarter) { $Starter = New-StarterProject -Directory $StarterDir }
    $PathAdded = $false
    if (!$NoPath)    { $PathAdded = Add-UserPathEntry -PathEntry $BinDir }

    Save-InstallManifest -Manifest @{
        Version                  = "1.0.0"
        InstalledAt              = (Get-Date).ToString("o")
        InstallDir               = [System.IO.Path]::GetFullPath($InstallDir)
        BinDir                   = [System.IO.Path]::GetFullPath($BinDir)
        PathEntry                = [System.IO.Path]::GetFullPath($BinDir).TrimEnd('\')
        PathAdded                = [bool]$PathAdded
        DocsDir                  = [System.IO.Path]::GetFullPath($DocsDir)
        ExamplesDir              = [System.IO.Path]::GetFullPath($ExamplesDir)
        VSCodeExtensionDir       = $VSCodeExtensionDir
        VSCodeExtensionInstalled = ![string]::IsNullOrWhiteSpace($VSCodeExtensionDir)
        StarterDir               = if ($null -ne $Starter) { [System.IO.Path]::GetFullPath($StarterDir) } else { "" }
        StarterFile              = if ($null -ne $Starter) { [System.IO.Path]::GetFullPath($Starter.File) } else { "" }
        StarterCreated           = if ($null -ne $Starter) { [bool]$Starter.Created } else { $false }
        StarterSha256            = if ($null -ne $Starter) { $Starter.Sha256 } else { "" }
    }

    # ── Step 6: Done ─────────────────────────────────────────────────────────
    Write-Step 6 $TotalSteps "Verifying installation"
    $InstalledVersion = & (Join-Path $BinDir "vion.exe") version 2>&1
    Write-OK "Verified: $InstalledVersion"

    # ── Summary box ──────────────────────────────────────────────────────────
    Write-Host ""
    Write-Color "  ─────────────────────────────────────────" -Color DarkGray
    Write-Host ""
    Write-Color "   ✔  Vion installed successfully!" -Color Green
    Write-Host ""
    Write-Dim   "   Location : $BinDir"
    if ($null -ne $Starter) {
        Write-Dim "   Starter  : $($Starter.File)"
    }
    Write-Host ""
    Write-Color "  ─────────────────────────────────────────" -Color DarkGray
    Write-Host ""
    Write-Color "  Get started:" -Color White
    Write-Host ""
    Write-Color "    " -NoNewline; Write-Color "vion" -Color Cyan -NoNewline; Write-Color " --version" -Color DarkGray
    if ($null -ne $Starter) {
        Write-Color "    " -NoNewline; Write-Color "vion" -Color Cyan -NoNewline
        Write-Color " $($Starter.File)" -Color DarkGray
    }
    Write-Color "    " -NoNewline; Write-Color "vion" -Color Cyan -NoNewline; Write-Color " repl" -Color DarkGray
    Write-Host ""
    Write-Color "  Docs  →  https://vion.vionex.software" -Color DarkGray
    Write-Color "  Repo  →  https://github.com/AlexanderPhan04/vion-lang" -Color DarkGray
    Write-Host ""

    # Removed auto-opening in VS Code to avoid interrupting user workflow
} catch {
    Stop-Spin -Fail -DoneMsg "Error: $_" 2>$null
    Write-Host ""
    Write-Fail "Installation failed: $_"
    Write-Host ""
    exit 1
} finally {
    if (Test-Path $TempRoot) {
        Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
