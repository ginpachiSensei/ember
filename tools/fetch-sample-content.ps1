# Downloads sample assets used by the sandbox into content/. They're not committed to git (size + licenses).
#   .\tools\fetch-sample-content.ps1
#
#   DamagedHelmet.glb  Khronos glTF sample. CC BY 4.0 (ctxwing) based on CC BY-NC 4.0 (theblueturtle_):
#                      fine for learning, not for a commercial product.
#   sky.hdr            "Kloofendal 48d Partly Cloudy (Pure Sky)" from Poly Haven, CC0.
$ErrorActionPreference = "Stop"
$content = Join-Path $PSScriptRoot "..\content"

$files = @(
    @{ Url = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb";
       Path = "models\DamagedHelmet\DamagedHelmet.glb" },
    @{ Url = "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/kloofendal_48d_partly_cloudy_puresky_1k.hdr";
       Path = "hdri\sky.hdr" }
)

foreach ($file in $files) {
    $target = Join-Path $content $file.Path
    if (Test-Path $target) {
        Write-Host "exists   $($file.Path)"
        continue
    }
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Write-Host "download $($file.Path)"
    Invoke-WebRequest -Uri $file.Url -OutFile $target -UseBasicParsing
}
