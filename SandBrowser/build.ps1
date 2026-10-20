# Copyright (c) 2026 mio. All generated files remain inside SandBrowser.
param()
$ErrorActionPreference = 'Stop'
$sbRoot = $PSScriptRoot
& (Join-Path $sbRoot 'tools/fetch-sources.ps1')
foreach ($sbStep in @('tools/prepare.py', 'tools/make_anchors.py', 'tools/build_native.py', 'tools/package.py')) {
    $sbArguments = @('--cd', $sbRoot, '-e', 'env', 'PYTHONDONTWRITEBYTECODE=1', 'python3', $sbStep)
    if ($sbStep -eq 'tools/build_native.py') { $sbArguments += 'all' }
    & wsl.exe @sbArguments
    if ($LASTEXITCODE -ne 0) { throw "Build failed in $sbStep" }
}
