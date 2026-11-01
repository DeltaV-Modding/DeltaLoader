param(
    [string]$Config = 'Release',
    [string]$Generator = 'Visual Studio 17 2022'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root 'build-mod'

$cfg = @('-S', $root, '-B', $buildDir)
if ($Generator) { $cfg += @('-G', $Generator) }
if ($Generator -like 'Visual Studio*') { $cfg += @('-A', 'x64') }
& cmake @cfg
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }
& cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "Mod build failed ($LASTEXITCODE)" }
Write-Host "Built mod: $(Join-Path $buildDir "$Config\delta_example.dll")"