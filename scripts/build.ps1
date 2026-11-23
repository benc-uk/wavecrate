[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [Parameter(DontShow)]
    [switch]$DeveloperEnvironment
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$missing = @(
    foreach ($tool in @('cmake', 'ninja', 'cl', 'rc', 'mt'))
    {
        if (-not (Get-Command $tool -CommandType Application -ErrorAction SilentlyContinue))
        {
            $tool
        }
    }
)

$wrongArchitecture = $env:VSCMD_ARG_TGT_ARCH -and $env:VSCMD_ARG_TGT_ARCH -ne 'x64'
if (($missing.Count -gt 0 -or $wrongArchitecture) -and -not $DeveloperEnvironment)
{
    $installerDirectory = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    $vswhere = Join-Path $installerDirectory 'vswhere.exe'
    if (Test-Path -LiteralPath $vswhere)
    {
        # Capture all output so early pipeline termination cannot preserve a stale exit code.
        $installations = @(& $vswhere -latest -products '*' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath)
        $discoveryExitCode = $LASTEXITCODE
        $installation = $installations | Select-Object -First 1

        if ($discoveryExitCode -eq 0 -and $installation)
        {
            $developerCommand = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path -LiteralPath $developerCommand)
            {
                $powerShell = Join-Path $PSHOME 'pwsh.exe'
                if (-not (Test-Path -LiteralPath $powerShell))
                {
                    $powerShell = Join-Path $PSHOME 'powershell.exe'
                }

                # Keep toolchain changes in a child process, not the caller's PATH.
                $command = 'set "PATH={0};%PATH%" && call "{1}" -no_logo -arch=x64 -host_arch=x64 && "{2}" -NoProfile -File "{3}" -Configuration {4} -DeveloperEnvironment' -f `
                    $installerDirectory, $developerCommand, $powerShell, $PSCommandPath, $Configuration
                Write-Host "Using installed x64 Build Tools: $installation"
                & $env:ComSpec /d /s /c $command
                if ($LASTEXITCODE -ne 0)
                {
                    throw "Build failed with exit code $LASTEXITCODE. See the output above."
                }
                return
            }
        }
    }
}

if ($missing.Count -gt 0)
{
    throw "Missing build tools: $($missing -join ', '). Check the installed C++ Build Tools, Windows SDK and CMake components, or use an x64 developer environment. See README.md. No tools have been installed."
}

if ($wrongArchitecture)
{
    throw 'Use an x64 MSVC developer environment, not an x86 or ARM developer environment.'
}

$preset = 'windows-' + $Configuration.ToLowerInvariant()
$projectRoot = Split-Path -Parent $PSScriptRoot

Push-Location -LiteralPath $projectRoot
try
{
    & cmake --preset $preset
    if ($LASTEXITCODE -ne 0)
    {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }

    & cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0)
    {
        throw "CMake build failed with exit code $LASTEXITCODE."
    }

    $executable = Join-Path $projectRoot "out\build\$preset\WaveCrate_artefacts\$Configuration\WaveCrate.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf))
    {
        throw "Build finished but the expected executable was not found: $executable"
    }

    Write-Host "Built: $executable"
}
finally
{
    Pop-Location
}
