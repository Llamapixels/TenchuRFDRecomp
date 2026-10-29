$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../..").Path
$downloads=Join-Path $root 'tools/renderer_downloads'
$output=Join-Path $root 'runtime/vulkan'
New-Item -ItemType Directory -Force -Path $downloads,$output | Out-Null
$releases=@(
    @{Name='vkd3d-proton-3.0.1';File='vkd3d-proton-3.0.1.tar.zst';Url='https://github.com/HansKristian-Work/vkd3d-proton/releases/download/v3.0.1/vkd3d-proton-3.0.1.tar.zst';Hash='3cf2315522af5e43605ef6d3c41dad91387040bf97199934f3f7ab76caaa2f0c'},
    @{Name='dxvk-3.1.1';File='dxvk-3.1.1.tar.gz';Url='https://github.com/doitsujin/dxvk/releases/download/v3.1.1/dxvk-3.1.1.tar.gz';Hash='40565b4a724aadc4433fa4e010b4b23916d9b1f1baeee64e17186db94f54e608'}
)
foreach($release in $releases){
    $archive=Join-Path $downloads $release.File
    if(-not (Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $release.Url -OutFile $archive}
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $release.Hash){throw "Official archive checksum mismatch: $($release.File)"}
    & tar -xf $archive -C $downloads
    if($LASTEXITCODE){throw "Cannot extract $archive"}
}
foreach($name in 'd3d12.dll','d3d12core.dll'){
    Copy-Item -LiteralPath (Join-Path $downloads "vkd3d-proton-3.0.1/x64/$name") -Destination $output
}
Copy-Item -LiteralPath (Join-Path $downloads 'dxvk-3.1.1/x64/dxgi.dll') -Destination $output
foreach($name in 'COPYING','LICENSE','AUTHORS'){
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/HansKristian-Work/vkd3d-proton/v3.0.1/$name" -OutFile (Join-Path $output "$name.vkd3d-proton")
}
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/doitsujin/dxvk/v3.1.1/LICENSE' -OutFile (Join-Path $output 'LICENSE.dxvk')
Write-Output 'Verified Vulkan runtime installed. Rebuild the game to package it.'
