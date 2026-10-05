param(
    [Parameter(Mandatory=$true)][string]$BatchDirectory,
    [int]$AdoptPid=0,
    [int]$AdoptFrame=2221
)
$ErrorActionPreference='Stop'
# mio：用户已授权整片离线渲染。调度器独立隐藏运行，在现有试帧
# 完成后继续同源all，避免两个12线程Blender相互争抢CPU和同名文件。
# 不更改系统字体、用户数据盘或另一个工程；计划只指向已冻结批次。
$projectRoot=Split-Path -Parent $PSScriptRoot
$batchPath=[IO.Path]::GetFullPath((Join-Path $projectRoot $BatchDirectory))
$pythonPath='C:\Users\Administrator\AppData\Local\Programs\Python\Python312\python.exe'
$blenderPath=Join-Path $projectRoot 'build\host-tools\blender\blender-4.5.3-windows-x64\blender.exe'
$queueSource=Join-Path $PSScriptRoot 'film_queue.py'
$queueFrozen=Join-Path $batchPath 'film_queue.py'
$manifestPath=Join-Path $batchPath 'project.json'
foreach($requiredPath in @($pythonPath,$blenderPath,$queueSource,$manifestPath)){
    if(!(Test-Path -LiteralPath $requiredPath -PathType Leaf)){throw "Missing frozen input: $requiredPath"}
}
$liveQueues=Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq 'python.exe' -and $_.CommandLine -and $_.CommandLine.Contains('film_queue.py') -and $_.CommandLine.Contains($batchPath)
}
if($liveQueues){throw "Live queue already exists: $($liveQueues.ProcessId)"}
if(Test-Path -LiteralPath (Join-Path $batchPath 'queue-plan.json')){
    throw 'Existing plan retained. Resume its frozen film_queue.py rather than overwrite its identity.'
}
$identity=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach($entry in @(@('film_scene.py','source_sha256'),@('film_io.py','io_source_sha256'))){
    if((Get-FileHash -LiteralPath (Join-Path $batchPath $entry[0]) -Algorithm SHA256).Hash.ToLower() -ne $identity.($entry[1])){
        throw "Frozen input mismatch: $($entry[0])"
    }
}
$adoption=$null
if($AdoptPid -gt 0){
    $live=Get-CimInstance Win32_Process -Filter "ProcessId=$AdoptPid"
    if(!$live -or $live.Name -ne 'blender.exe' -or !$live.CommandLine -or !$live.CommandLine.Contains($batchPath)){
        throw 'AdoptPid does not belong to this frozen Blender batch'
    }
    # CIM日期只保留微秒；GetProcessTimes是100ns。用实际进程StartTime
    # 取得同一内核FILETIME，否则无辜的末位舍入会被误报为PID复用。
    $createdFileTime=(Get-Process -Id $AdoptPid).StartTime.ToUniversalTime().ToFileTimeUtc()
    $adoption=[ordered]@{pid=$AdoptPid;frame=$AdoptFrame;command=$live.CommandLine;created_filetime=$createdFileTime}
}
Copy-Item -LiteralPath $queueSource -Destination $queueFrozen
$plan=[ordered]@{
    author='mio';magic='SCFQUEUE1MIO';target_frames=2520;requested_frames='all'
    batch=$batchPath;blender=$blenderPath;created_local=(Get-Date -Format o)
    identity_sha256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLower()
    queue_source_sha256=(Get-FileHash -LiteralPath $queueFrozen -Algorithm SHA256).Hash.ToLower()
    adopt_process=$adoption
}
$plan | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $batchPath 'queue-plan.json') -Encoding utf8
$env:PYTHONIOENCODING='utf-8'
$queue=Start-Process -FilePath $pythonPath -ArgumentList @('-u',('"'+$queueFrozen+'"'),'--batch',('"'+$batchPath+'"')) -WorkingDirectory $projectRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $batchPath 'queue.stdout.log') -RedirectStandardError (Join-Path $batchPath 'queue.stderr.log') -PassThru
[ordered]@{status='QUEUE_STARTED_REQUIRES_LIVE_OBSERVATION';queue_pid=$queue.Id;batch=$batchPath;adopted_blender_pid=$AdoptPid;requested_frames='all';target_frames=2520} | ConvertTo-Json
