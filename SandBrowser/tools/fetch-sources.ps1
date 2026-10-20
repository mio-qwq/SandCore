# Copyright (c) 2026 mio. Download against the checked-in archive hash lock.
param()
$ErrorActionPreference = 'Stop'
$sbRoot = Split-Path -Parent $PSScriptRoot
$sbCache = Join-Path $sbRoot 'vendor/archives'
$sbInputs = Get-Content -LiteralPath (Join-Path $sbRoot 'vendor/SOURCES.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $sbCache -Force | Out-Null
foreach ($sbInput in $sbInputs) {
    $sbTarget = Join-Path $sbCache $sbInput.archive
    if (-not (Test-Path -LiteralPath $sbTarget)) {
        $sbPart = $sbTarget + '.part'
        Invoke-WebRequest -Uri $sbInput.url -OutFile $sbPart -TimeoutSec 90
        $sbActual = (Get-FileHash -LiteralPath $sbPart -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($sbActual -ne $sbInput.sha256) { throw "Archive hash differs: $($sbInput.archive). Keep the pinned source snapshot." }
        Move-Item -LiteralPath $sbPart -Destination $sbTarget
    }
    $sbActual = (Get-FileHash -LiteralPath $sbTarget -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($sbActual -ne $sbInput.sha256) { throw "Archive hash differs: $($sbInput.archive)" }
    Write-Output ($sbInput.archive + ' ' + $sbActual)
}
