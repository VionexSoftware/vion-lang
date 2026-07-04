param(
    [string]$ExePath = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Join-Path $ProjectRoot "build\Debug\vion.exe"
}

if (!(Test-Path $ExePath)) {
    throw "Vion executable not found: $ExePath"
}

function Normalize-Output {
    param([string]$Value)

    return ($Value -replace "`r`n", "`n").TrimEnd()
}

function ConvertTo-CommandArgument {
    param([string]$Argument)

    return '"' + ($Argument -replace '"', '\"') + '"'
}

function Invoke-VionProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    $StartInfo = New-Object System.Diagnostics.ProcessStartInfo
    $StartInfo.FileName = $FilePath
    $StartInfo.Arguments = ($Arguments | ForEach-Object { ConvertTo-CommandArgument $_ }) -join " "
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true

    $Process = [System.Diagnostics.Process]::Start($StartInfo)
    $Stdout = $Process.StandardOutput.ReadToEnd()
    $Stderr = $Process.StandardError.ReadToEnd()
    $Process.WaitForExit()

    return @{
        ExitCode = $Process.ExitCode
        Output = $Stdout + $Stderr
    }
}

$Cases = @(
    @{
        Name = "short version"
        Args = @("-v")
        ExitCode = 0
        Output = "Vion v0.4.0"
    },
    @{
        Name = "hello"
        Args = @("run", (Join-Path $ProjectRoot "examples\hello.vion"))
        ExitCode = 0
        Output = "Vion`n30`ntotal is valid"
    },
    @{
        Name = "short run"
        Args = @("-r", (Join-Path $ProjectRoot "examples\hello.vion"))
        ExitCode = 0
        Output = "Vion`n30`ntotal is valid"
    },
    @{
        Name = "direct file run"
        Args = @((Join-Path $ProjectRoot "examples\hello.vion"))
        ExitCode = 0
        Output = "Vion`n30`ntotal is valid"
    },
    @{
        Name = "short check"
        Args = @("-c", (Join-Path $ProjectRoot "examples\hello.vion"))
        ExitCode = 0
        Output = "OK"
    },
    @{
        Name = "inline eval"
        Args = @("-e", "print 1 + 2")
        ExitCode = 0
        Output = "3"
    },
    @{
        Name = "features"
        Args = @("run", (Join-Path $ProjectRoot "examples\features.vion"))
        ExitCode = 0
        Output = "10`n120`n7`nlogic ok"
    },
    @{
        Name = "scope"
        Args = @("run", (Join-Path $ProjectRoot "examples\scope.vion"))
        ExitCode = 0
        Output = "block`nglobal"
    },
    @{
        Name = "runtime error"
        Args = @("run", (Join-Path $ProjectRoot "tests\cases\runtime-error.vion"))
        ExitCode = 1
        Output = "Runtime Error: division by zero."
    },
    @{
        Name = "arrays"
        Args = @("run", (Join-Path $ProjectRoot "examples\arrays.vion"))
        ExitCode = 0
        Output = "10`n50`n50`n99`n5`n6`n60`n5`n--- for loop ---`n99`n20`n30`n40`n50`n6`n4`n15`narray`nstring`nnumber`n5`n0"
    },
    @{
        Name = "for-in loop"
        Args = @("run", (Join-Path $ProjectRoot "examples\arrays.vion"))
        ExitCode = 0
        Output = "10`n50`n50`n99`n5`n6`n60`n5`n--- for loop ---`n99`n20`n30`n40`n50`n6`n4`n15`narray`nstring`nnumber`n5`n0"
    },
    @{
        Name = "modulo"
        Args = @("-e", "print 10 % 3")
        ExitCode = 0
        Output = "1"
    },
    @{
        Name = "builtins-len"
        Args = @("-e", "print len([1, 2, 3])")
        ExitCode = 0
        Output = "3"
    },
    @{
        Name = "builtins-type"
        Args = @("-e", "print type([])")
        ExitCode = 0
        Output = "array"
    },
    @{
        Name = "number-formatting"
        Args = @("-e", "print 10 + 0")
        ExitCode = 0
        Output = "10"
    },
    @{
        Name = "top-level return"
        Args = @("run", (Join-Path $ProjectRoot "tests\cases\top-level-return.vion"))
        ExitCode = 1
        Output = "Error: Runtime Error: return outside function."
    }
)

$Failures = 0

foreach ($Case in $Cases) {
    $ArgsList = [string[]]$Case["Args"]
    $Result = Invoke-VionProcess -FilePath $ExePath -Arguments $ArgsList
    $Output = $Result["Output"]
    $ActualExitCode = $Result["ExitCode"]

    $ExpectedOutput = Normalize-Output $Case["Output"]
    $ActualOutput = Normalize-Output $Output

    if ($ActualExitCode -ne $Case["ExitCode"] -or $ActualOutput -ne $ExpectedOutput) {
        $Failures += 1
        Write-Host "FAIL: $($Case["Name"])" -ForegroundColor Red
        Write-Host "  Expected exit: $($Case["ExitCode"])"
        Write-Host "  Actual exit:   $ActualExitCode"
        Write-Host "  Expected output:"
        Write-Host $ExpectedOutput
        Write-Host "  Actual output:"
        Write-Host $ActualOutput
    } else {
        Write-Host "PASS: $($Case["Name"])" -ForegroundColor Green
    }
}

if ($Failures -gt 0) {
    Write-Host "$Failures Vion test(s) failed." -ForegroundColor Red
    exit 1
}

Write-Host "All Vion tests passed." -ForegroundColor Green
