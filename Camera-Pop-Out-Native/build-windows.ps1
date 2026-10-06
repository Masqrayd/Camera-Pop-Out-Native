# One-time Windows compiler prerequisites are documented in README.md.
param([string]$TemplateRef = '3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Start-Transcript -Path (Join-Path $root 'build-log.txt') -Force
try {
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'Use 64-bit Windows.' }
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmake) { throw 'Install CMake 3.30 or newer and reopen this command window.' }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Install Visual Studio 2022 Build Tools with Desktop development with C++.' }
    $vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio 2022 C++ tools are missing. Add the Desktop development with C++ workload.' }
    $work = Join-Path $root 'build-work'
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    $archive = Join-Path $work 'official-template.zip'
    $url = 'https://github.com/obsproject/obs-plugintemplate/archive/' + [Uri]::EscapeDataString($TemplateRef) + '.zip'
    Write-Host 'Downloading the official OBS plugin build template...'
    Invoke-WebRequest -Uri $url -OutFile $archive
    $expand = Join-Path $work 'template-download'
    if (Test-Path $expand) { Remove-Item -Recurse -Force $expand }
    Expand-Archive -Path $archive -DestinationPath $expand
    $template = (Get-ChildItem -Directory $expand | Select-Object -First 1).FullName
    if (-not $template) { throw 'The OBS build template archive is empty.' }
    Copy-Item (Join-Path $root 'CMakeLists.txt') $template -Force
    Copy-Item (Join-Path $root 'src\*') (Join-Path $template 'src') -Force
    $specPath = Join-Path $template 'buildspec.json'
    $spec = Get-Content $specPath -Raw | ConvertFrom-Json
    $spec.name = 'camera-popout-native'
    $spec.displayName = 'Camera Pop-Out Native'
    $spec.version = '0.2.0'
    $spec.author = 'Masqrayd'
    $spec | ConvertTo-Json -Depth 20 | Set-Content -Path $specPath -Encoding UTF8
    $sdkVersion = $spec.dependencies.'obs-studio'.version
    if (-not $sdkVersion -or [version]$sdkVersion -gt [version]'32.2.2') {
        throw "Template SDK $sdkVersion is newer than OBS 32.2.2. Choose a compatible TemplateRef; refusing an unverified newer SDK."
    }
    Write-Host "Building with OBS SDK $sdkVersion for target OBS 32.2.2"
    # The ordinary Windows preset is an MSVC x64 build. Avoid CI-only warnings-as-errors.
    Push-Location $template
    try {
        & cmake --preset windows-x64 -DENABLE_FRONTEND_API=ON -DENABLE_QT=OFF -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF
        if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed. See build-log.txt.' }
        & cmake --build --preset windows-x64 --parallel
        if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed. See build-log.txt.' }
    } finally { Pop-Location }
    $dll = Get-ChildItem (Join-Path $template 'build_x64') -Filter 'camera-popout-native.dll' -Recurse | Where-Object { $_.FullName -match '[\\/]RelWithDebInfo[\\/]|[\\/]Release[\\/]' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $dll) { throw 'Build completed without producing the expected DLL.' }
    $package = Join-Path $root 'package\camera-popout-native\bin\64bit'
    if (Test-Path (Join-Path $root 'package')) { Remove-Item (Join-Path $root 'package') -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $package | Out-Null
    Copy-Item $dll.FullName (Join-Path $package 'camera-popout-native.dll') -Force
    Compress-Archive -Path (Join-Path $root 'package\camera-popout-native') -DestinationPath (Join-Path $root 'Camera-Pop-Out-Native-Windows.zip') -Force
    Write-Host 'Build complete. The DLL is in package\camera-popout-native\bin\64bit.'
    Write-Host 'Keep OBS closed while installing. See README.md for the destination.'
} finally { Stop-Transcript }
