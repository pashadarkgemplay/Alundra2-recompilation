# Gets an embeddable Python if there is none, then lets a2pc.py build and start the launcher.
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$pyDir = Join-Path $root 'tools\python'
$py = Join-Path $pyDir 'python.exe'

function Works($exe) {
    try { & $exe -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" 2>$null; return ($LASTEXITCODE -eq 0) }
    catch { return $false }
}

if (-not ((Test-Path $py) -and (Works $py))) {
    $py = $null
    foreach ($c in 'py', 'python') {
        $cmd = Get-Command $c -ErrorAction SilentlyContinue
        if ($cmd -and (Works $cmd.Source)) { $py = $cmd.Source; break }
    }
}

if (-not $py) {
    Write-Host '[a2pc] [1/3] Python was not found on this PC, getting it (one time, about 10 MB)'
    $zip = Join-Path $root 'tools\python-embed.zip'
    New-Item -ItemType Directory -Force -Path $pyDir | Out-Null
    Invoke-WebRequest -UseBasicParsing -Uri 'https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-amd64.zip' -OutFile $zip
    Write-Host '[a2pc] unpacking Python ...'
    Expand-Archive -Force -LiteralPath $zip -DestinationPath $pyDir
    Remove-Item $zip
    # the embeddable build only looks at what its ._pth file lists
    $pth = Get-ChildItem $pyDir -Filter 'python*._pth' | Select-Object -First 1
    $base = [IO.Path]::GetFileNameWithoutExtension($pth.Name)
    Set-Content -Path $pth.FullName -Value "$base.zip`r`n.`r`n..\..`r`n" -NoNewline -Encoding ASCII
    $py = Join-Path $pyDir 'python.exe'
    Write-Host '[a2pc] Python is ready'
}

Write-Host '[a2pc] [2/3] Building the launcher window (the compiler is downloaded first if needed)'
& $py (Join-Path $root 'a2pc.py') launcher
exit $LASTEXITCODE
