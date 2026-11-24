param(
    [string]$QtDir = $env:QT_ROOT_DIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param([string]$FilePath, [string[]]$Arguments)
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath exited with code $LASTEXITCODE"
    }
}

if (-not $QtDir -or -not $VcpkgRoot) {
    throw 'Set QtDir (Qt 6.11.1 msvc2022_64) and VcpkgRoot.'
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$qt = (Resolve-Path $QtDir).Path
$vcpkg = (Resolve-Path $VcpkgRoot).Path
$deploy = Join-Path $qt 'bin/windeployqt.exe'
$toolchain = Join-Path $vcpkg 'scripts/buildsystems/vcpkg.cmake'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
foreach ($file in @($deploy, $toolchain, $vswhere)) {
    if (-not (Test-Path $file -PathType Leaf)) {
        throw "Required tool not found: $file"
    }
}

$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs -or $LASTEXITCODE -ne 0) {
    throw 'Install Visual Studio 2022 or its Build Tools with Desktop development with C++.'
}
$redistRoot = Join-Path $vs 'VC/Redist/MSVC'
$runtime = Get-ChildItem $redistRoot -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'x64/Microsoft.VC143.CRT') } |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1
if (-not $runtime) {
    throw "Release x64 VC143 runtime files not found in $redistRoot."
}

$triplet = 'x64-windows-static-md-release'
$build = Join-Path $root 'build-win64'
$installed = Join-Path $build 'vcpkg_installed'
$stage = Join-Path $build 'portable'
$out = Join-Path $root 'out'
$zip = Join-Path $out 'MeetingClient-Windows-x64.zip'

$env:PATH = "$(Join-Path $qt 'bin');$env:PATH"
Invoke-Checked -FilePath cmake -Arguments @(
    '-S', $root, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DCMAKE_GENERATOR_INSTANCE=$vs",
    "-DCMAKE_PREFIX_PATH=$qt",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_MANIFEST_DIR=$PSScriptRoot",
    "-DVCPKG_INSTALLED_DIR=$installed",
    "-DVCPKG_TARGET_TRIPLET=$triplet",
    '-DVCPKG_HOST_TRIPLET=x64-windows',
    '-DBUILD_TESTING=ON'
)
Invoke-Checked -FilePath cmake -Arguments @('--build', $build, '--config', 'Release', '--parallel', '4')
Invoke-Checked -FilePath ctest -Arguments @('--test-dir', $build, '-C', 'Release', '--output-on-failure')

if (Test-Path $stage) {
    Remove-Item $stage -Recurse -Force
}
New-Item -ItemType Directory -Force $stage, $out | Out-Null
$exe = Join-Path $stage 'MeetingClient.exe'
Copy-Item (Join-Path $build 'Release/MeetingClient.exe') $exe
Invoke-Checked -FilePath $deploy -Arguments @('--release', '--no-compiler-runtime', $exe)
Copy-Item (Join-Path $runtime.FullName 'x64/Microsoft.VC143.CRT/*.dll') $stage
Copy-Item (Join-Path $PSScriptRoot 'README.txt') $stage

$licenses = Join-Path $stage 'third-party-notices'
New-Item -ItemType Directory -Path $licenses | Out-Null
Get-ChildItem (Join-Path $installed "$triplet/share") -Directory | ForEach-Object {
    $notice = Join-Path $_.FullName 'copyright'
    if (Test-Path $notice -PathType Leaf) {
        Copy-Item $notice (Join-Path $licenses "$($_.Name).txt")
    }
}

foreach ($file in @('MeetingClient.exe', 'Qt6Core.dll', 'Qt6Multimedia.dll', 'platforms/qwindows.dll', 'vcruntime140.dll', 'msvcp140.dll')) {
    if (-not (Test-Path (Join-Path $stage $file) -PathType Leaf)) {
        throw "Missing from package: $file"
    }
}
$multimediaPlugins = @(Get-ChildItem (Join-Path $stage 'multimedia') -Filter '*.dll')
if ($multimediaPlugins.Count -eq 0) {
    throw 'No Qt Multimedia plugin was deployed.'
}

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
Get-FileHash -Algorithm SHA256 $zip
