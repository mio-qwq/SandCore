param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [int]$Samples=512,
    [string]$Python='C:\Users\Administrator\AppData\Local\Programs\Python\Python312\python.exe'
)
$ErrorActionPreference='Stop'
$mathRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$mathOutput=[IO.Path]::GetFullPath((Join-Path $mathRoot $OutputDirectory))
if (Test-Path -LiteralPath $mathOutput) { throw 'Use a new batch directory; existing render source and frames are immutable.' }
if ($Samples -lt 128 -or $Samples -gt 4096) { throw 'Invalid master sampling count.' }
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) { throw 'Python executable not found.' }
# mio：后台实例执行批次内冻结源码。继续修改tools不会改变正在
# 渲染的程序或参数；新工程另起批次，不能向旧目录混入另一版本帧。
New-Item -ItemType Directory -Path $mathOutput | Out-Null
foreach ($mathFile in @('render.py','render.cl','opencl_host.py')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "film_math\$mathFile") -Destination (Join-Path $mathOutput $mathFile)
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'film_io.py') -Destination (Join-Path $mathOutput 'film_io.py')
$mathArguments=@('-u',('"'+(Join-Path $mathOutput 'render.py')+'"'),'--out',('"'+$mathOutput+'"'),
    '--resume','--frames','all','--mode','master','--width','3840','--height','2160','--samples',[string]$Samples)
$mathJob=Start-Process -FilePath $Python -ArgumentList $mathArguments -WorkingDirectory $mathOutput -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $mathOutput 'render.stdout.log') -RedirectStandardError (Join-Path $mathOutput 'render.stderr.log')
$mathRecord=@{author='mio';magic='SCMATHWORK1MIO';version=1;pid=$mathJob.Id;python=$Python;
    created_filetime=$mathJob.StartTime.ToUniversalTime().ToFileTimeUtc();started_local=(Get-Date -Format s);
    command_arguments=$mathArguments;batch=$mathOutput;target_frames=2520;width=3840;height=2160;fps=60;seconds=42;samples=$Samples;
    status='DISPATCHED_VERIFY_PROCESS_AND_PROGRESS';full_movie_complete=$false;guest_player_complete=$false}
[IO.File]::WriteAllText((Join-Path $mathOutput 'worker.json'),($mathRecord | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
$mathRecord | ConvertTo-Json -Depth 5
