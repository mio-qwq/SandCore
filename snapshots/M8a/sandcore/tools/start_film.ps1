param(
    [Parameter(Mandatory=$true)][string]$BatchDirectory,
    [ValidateSet('master','scout')][string]$Mode='master',
    [ValidatePattern('^(all|[0-9,:]+)$')][string]$Frames='2221',
    [int]$Samples=512
)
$ErrorActionPreference='Stop'
# mio：离线长任务以独立隐藏进程运行，避免对话/PTY观察会话失效时
# 丢失渲染。文件中PID只是索引；跟进时须另查实际进程/命令行和日志。
# 此入口只执行已经冻结的工程源码，不以当前开发源码覆盖旧批次身份。
$projectRoot=Split-Path -Parent $PSScriptRoot
$batchPath=[IO.Path]::GetFullPath((Join-Path $projectRoot $BatchDirectory))
$sourcePath=Join-Path $batchPath 'film_scene.py'
$validationPath=Join-Path $batchPath 'film_io.py'
$blenderPath=Join-Path $projectRoot 'build\host-tools\blender\blender-4.5.3-windows-x64\blender.exe'
foreach($requiredPath in @($sourcePath,$validationPath,$blenderPath,(Join-Path $batchPath 'project.json'))){
    if(!(Test-Path -LiteralPath $requiredPath -PathType Leaf)){throw "Missing frozen input: $requiredPath"}
}
$identity=Get-Content -LiteralPath (Join-Path $batchPath 'project.json') -Raw | ConvertFrom-Json
if((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLower() -ne $identity.source_sha256){throw 'Frozen film source mismatch'}
if((Get-FileHash -LiteralPath $validationPath -Algorithm SHA256).Hash.ToLower() -ne $identity.io_source_sha256){throw 'Frozen PNG validation mismatch'}
if($identity.mode -ne $Mode -or $identity.samples -ne $Samples){throw 'Mode or samples differ from frozen project'}
# 同一工程已有真正存活的Blender时不再开第二份，不把锁文件当存活
# 证据；用户其它Blender工程从不终止。中断批次仍保留完整旧日志。
$existing=Get-CimInstance Win32_Process -Filter "Name='blender.exe'" | Where-Object {
    $_.CommandLine -and ($_.CommandLine.Contains($batchPath) -or $_.CommandLine.Contains($batchPath.Replace('\','/')))
}
if($existing){throw "This batch already has a live Blender process: $($existing.ProcessId)"}
$attempt='attempt-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')
$outputPath=Join-Path $batchPath ($attempt+'.stdout.log')
$errorPath=Join-Path $batchPath ($attempt+'.stderr.log')
$arguments=@('-b','--python',('"'+$sourcePath+'"'),'--','--out',('"'+$batchPath+'"'),'--mode',$Mode,'--samples',"$Samples",'--frames',$Frames)
$worker=Start-Process -FilePath $blenderPath -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -RedirectStandardOutput $outputPath -RedirectStandardError $errorPath -PassThru
$record=[ordered]@{
    author='mio';scope='INDEPENDENT_HOST_RENDER_WORKER';started_local=(Get-Date -Format o)
    pid=$worker.Id;executable=$blenderPath;arguments=$arguments;batch=$batchPath
    stdout=$outputPath;stderr=$errorPath;requested_frames=$Frames
    source_sha256=$identity.source_sha256;io_sha256=$identity.io_source_sha256
    status='STARTED_REQUIRES_LIVE_PROCESS_OBSERVATION'
}
$record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $batchPath ($attempt+'.json')) -Encoding utf8
$record | ConvertTo-Json -Depth 4
