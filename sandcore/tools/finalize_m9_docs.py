#!/usr/bin/env python3
"""只在最后回归通过后更新M9验收状态；保留所有阶段沿革。"""
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
MARK='2026-10-05 M9最终验证记录'

def append(path,text):
    p=ROOT/path;s=p.read_text(encoding='utf-8-sig')
    if MARK in s:raise ValueError('已写入最终记录，请人工审阅后更新')
    p.write_text(s.rstrip()+'\n\n## '+MARK+'\n\n'+text+'\n\n修订：2026-10-05，记录实际范围与证据，待用户验收；前述早期未验证叙述保留为历史。\n',encoding='utf-8')

def main():
    player=json.loads((ROOT/'build/m9-player-20261005-09/matrix.json').read_text(encoding='utf-8'))
    kernel=json.loads((ROOT/'build/m9-final-kernel-20261005-01/matrix.json').read_text(encoding='utf-8'))
    fallback=json.loads((ROOT/'build/m9-desktop-fallback-20261005-02/matrix.json').read_text())
    perf=json.loads((ROOT/'build/m9-performance-20261005-03/matrix.json').read_text())
    build=json.loads((ROOT/'build/m9-build-integrity-08.json').read_text(encoding='utf-8'))
    stress=json.loads((ROOT/'build/m9-player-stress-20261005-02/matrix.json').read_text())
    same=json.loads((ROOT/'build/m9-core-equivalence-01.json').read_text())
    if stress['status']!='PASS' or same['status']!='PASS':raise ValueError('连续生命周期或像素对照未通过')
    if player['status']!='PLAYER_CASES_PASS_REMAINDER_PENDING' or kernel['status']!='INTEGRATION_CASES_PASS_REMAINDER_PENDING' or fallback['status']!='PASS' or perf['status']!='MEASURED_CORRECTNESS_PASS':
        raise ValueError('最终回归尚未全部通过，禁止更新验收状态')
    for r in player['runs']+kernel['disks']:
        if any(c['status']!='PASS' for c in r['cases']):raise ValueError('存在未通过用例')
    pc=sum(len(r['cases']) for r in player['runs']);kc=sum(len(r['cases']) for r in kernel['disks'])
    banner=f'> 当前：M9已完成本轮Windows QEMU验证，**待用户验收**。本地CLI口径由用户确认，140/171项（81.9%）按公开支持子集通过，完整310项/缺项保留。最终播放器{pc}项、内核关键路径{kc}项及三种CORE/VGA原像素回归通过；范围、限制与验收包见[M9-ACCEPTANCE.md](M9-ACCEPTANCE.md)。以下早期“未构建/待验”属于阶段记录。\n\n'
    p=ROOT/'docs/M9-IMPLEMENTATION.md';s=p.read_text(encoding='utf-8-sig');s=s.replace('\n\n','\n\n'+banner,1)
    statuses={
      '双 UART 与本机串口工具':'两外部通道、菜单暂停/继续及会话50项专项通过；物理串口未适配',
      '内核 UID/GID 与可信会话':'权限/登录/改密/SYSTEM门槛118项专项及最终内核回归通过',
      'SandFS 新格式/迁移/权限':'v5双盘1022/978项；结构136项、实际硬终止54项、真实EIO66项通过',
      'SYSTEM 零环即时/常驻工具':'实际原生打包/重定位/常驻及错误/异常/回收通过，无热卸载',
      'Ring0 调试器':'真实INT3/TF/RAM/寄存器/双通道暂停恢复及CPL0 fatal拒绝恢复通过',
      'Shell 与独立 CLI':'用户确认本地140/171个公开支持子集，双盘行为映射与完整缺项保留',
      'SCCC CLI / SandAsm SIMD':'双盘真实自举18项、每盘245种编码对照/6类拒绝通过',
      'nano / scdbg CLI':'完整功能128项及4MiB/超限/8016B背压/EOF/回收34项专项通过',
      'SIMD/扩展状态隔离':'真实整数SSE2/XMM7/MXCSR/x87抢占/复用、无SSE2/FNSAVE/无FPU通过',
      '双向文件传输':'完整SHA/进度17项、坏帧/重发/取消/真实租约/BYE/重连专项通过',
      '指定窗口截图':'完整BGRA与串口取回、8槽/护栏/坏指针/关闭/正常和异常页回收通过',
      'QEMU 音频/默认音效/播放器':f'开关机/六提示波形、WAV80项/MP3独立34项/FLAC及两主题{pc}项通过',
      'M9 构建/发布链':'build31通过；内核与30字节相同，CORE合并区段且完整像素相同；独立验收包',
      '整体兼容/安全/性能验收包':'专项及最终双盘回归通过，真实全尺寸测量已记录，待用户验收',
    }
    for name,status in statuses.items():
        pattern=r'(?m)^\| '+re.escape(name)+r' \| [^\n|]* \|'
        s,n=re.subn(pattern,lambda m:'| '+name+' | '+status+' |',s)
        if n!=1:raise ValueError('状态表行不唯一: '+name)
    p.write_text(s,encoding='utf-8')
    p=ROOT/'docs/M9-ACCEPTANCE.md';s=p.read_text(encoding='utf-8-sig')
    s=s.replace('本轮约定功能已构建并完成专项验证；最终逐图复核发现旧来源盘的原生桌面\n回退残影，当前状态为**修复后截图回归待完成，尚不能验收**。','本轮约定功能已构建并完成Windows QEMU验证，状态为**待用户验收**。')
    s=s.replace('双盘88项PASS，另禁FPU双盘20项PASS',f'最终双盘{pc}项PASS，另禁FPU双盘20项PASS')
    s=s.replace('build/m9-player-20261005-07/matrix.json；','build/m9-player-20261005-09/matrix.json；')
    s=s.replace('163 ELF无未解析符号/动态TLS，内核212856B、BSS末端0x1CA194',f'{build["elf_count"]} ELF无未解析符号/动态TLS，内核{build["kernel_bytes"]}B、BSS末端{build["kernel_bss_end"]}')
    s=s.replace('m9-build-integrity-06.json；m9-compat-static-06.json','m9-build-integrity-08.json；m9-compat-static-08.json')
    start=s.index('当前内核与上述专项所用内核原字节相同');end=s.index('\n\n## 本轮实际修复',start)
    s=s[:start]+f'最终内核SHA256为{build["boot_sha256"]}。build30只追加完整桌面回退修复与新版CORE构建接线；最终双盘重新执行传输、原生编译/扩展现场/音频、身份、零环、窗口和旧程序，共{kc}项通过。build31仅CORE.SKM绘图合并同色区段，内核/CLI字节不变，逐像素与实际连续生命周期另验；独立坏盘/EIO/硬终止、账户/租约等专项保留各自实际构建摘要，不改称在最终内核全部重跑。所有原失败记录保留。\n\n最终图形回归逐行核对迷你窗口外整个工作区，两个分辨率/主题通过；最后CORE版又在每盘连续20次主题切换/启动/大屏封面/迷你桌面恢复/退出及页回收中通过，总82条用例、40个真实生命周期，串口心跳保持。08曾有一次心跳超时，根因未证实，09和最终连续批次未复现，不称已定位该超时。另实际跑新CORE、原M7 CORE、缺CORE三种回退，关闭后完整工作区原像素恢复，VGA 64000像素与旧内核逐字节一致。证据：build/m9-desktop-fallback-20261005-02/matrix.json、build/m9-player-stress-20261005-02/matrix.json。'+s[end:]
    s=s.replace('| id忽略-u/-g参数 |','| 原生桌面回退只画320×200，留下旧窗口 | 原生基底完整覆盖；默认CORE构建接线补齐动态尺寸，缺CORE用完整兜底，旧模块服务坐标/ABI不改；逐行工作区与VGA旧像素通过 |\n| id忽略-u/-g参数 |')
    start=s.index('全尺寸1920×1080/150%');end=s.index('\n\nPUT/GET测速',start)
    rows=[]
    for disk in perf['disks']:
        costs=[f'{m["qemu_cpu_seconds"]/m["wall_seconds"]*100:.2f}%' for m in disk['measurements']]
        latency=disk['serial_command_roundtrip']
        rows.append('四状态单核心CPU '+', '.join(costs)+f'；串口中位{latency["median_seconds"]*1000:.1f}ms/P95 {latency["p95_seconds"]*1000:.1f}ms')
    s=s[:start]+'全尺寸1920×1080/150%与1024×768/100%，最终构建分别测得：\n\n'+''.join('- '+r+'。\n' for r in rows)+'\n四状态依次为桌面空闲、实际播放、暂停大屏、暂停迷你。各4秒（播放2秒），PIT/墙钟、宿主CPU、WM提交、DMA完成、欠载与页数独立记录；退出页数严格回基线。CORE同色区段优化前后两个盘只有CORE.SKM正文变化，完整工作区像素分别覆盖1920×1032及1024×736并全部相同；同脚本观察空闲单核心CPU从7.42%/11.33%降为3.52%/6.25%，仅代表本机短采样，不扩为全组件性能保证。证据：build/m9-core-equivalence-01.json、build/m9-performance-20261005-02/matrix.json与03/matrix.json。修复前01画面有漏检，不作为最终视觉通过证据。'+s[end:]
    p.write_text(s,encoding='utf-8')
    append('docs/M9-IMPLEMENTATION.md',f'本轮完整清单按顶部状态表；最终图形{pc}项、关键内核{kc}项、回退/VGA及实测通过，验收包待用户。')
    append('README.md','本轮M9 Windows QEMU验证通过，待用户验收。140/171个本地参考命令按公开支持子集验收，完整310行/31本地缺项保留；见[验收报告](docs/M9-ACCEPTANCE.md)。原M9/M9-FLAC试玩盘/会话与M8a保留。')
    p=ROOT/'README.md';s=p.read_text(encoding='utf-8');s=s.replace('\n\n','\n\n'+banner.replace('(M9-ACCEPTANCE.md)','(docs/M9-ACCEPTANCE.md)')+'| 版本 | 状态 | 入口 |\n|---|---|---|\n| M9 | 本轮验证通过，待用户验收 | temp miotest/run-m9-acceptance.bat；docs/M9-ACCEPTANCE.md |\n\n',1);p.write_text(s,encoding='utf-8')
    records={
      'M9-VERIFICATION.md':f'完整专项索引见[M9-ACCEPTANCE](M9-ACCEPTANCE.md)。本地171口径获用户确认；最后图形{pc}项、关键内核{kc}项及CORE/VGA回归通过。player07锚点没检测窗外残影，08保留心跳超时失败；修复/09重测与逐行全工作区证据保留，不删失败。',
      'CLI-M9.md':'用户确认本地140/171（81.9%）口径；310行原表完整保留，31个本地缺项及选项限制公开。BEHAVIOR-SUBSET-PASS映射到双盘实际行为，不表示BusyBox所有选项兼容。核心128项、扩展256项、边界34项及账户专项通过。id支持-u/-g/-G/-un/-r，冲突/未知/组名选项拒绝；旧无参数输出保留。HOME/默认cron/日志/波浪号显式卷根化；od -An取消额外结束空行。',
      'FS.md':'v5两来源最终1022/978项完整内容回读。26VM结构/容量136项、8次真实硬终止+8冷启动54项、16VM块层数据/目录/超级块/FLUSH EIO66项通过。超级块失败使运行卷后续写入冻结，冷启动恢复原提交；真实QEMU行为，不是所有物理扇区撕裂。',
      'AUTH.md':'账户/改密/UID/GID/模块边界118项、真实会话/BYE/3000tick租约50项及最终内核身份回归通过。密码新旧检查在普通UID下实际执行；root/SYSTEM能管理普通账户，SYSTEM普通登录始终拒绝。串口资格在内核，客体root不取得MOD或UART。',
      'SERIAL.md':'两外部UART、重复帧同CRC只执行一次/异体拒绝、PUT乱序/BYE取消/旧会话拒绝、实际3100tick租约/220tick半帧恢复通过。progress02宿主真实菜单17项通过。:secret只在客体密码提示后隐藏本机输入并发送密码，不能赋予SYSTEM权限；公开菜单不回显密码。',
      'CAPTURE.md':'真实BGRA/alpha/完整原生尺寸经串口取回精确比较。8份额度/第9拒绝、32B护栏/坏指针/EOF/旧token/槽复用及正常退出/用户UD2处理后退出均验证；五轮页数严格回基线。session-lifecycle02与最终窗口回归证据保留。',
      'SIMD.md':'每盘245种SSE2整数汇编形式与GNU as全部机器码一致，六类非法输入保留旧输出；真实并发XMM7/MXCSR/x87抢占/复用、FXSAVE/FNSAVE/无SSE2/无FPU回退与最后内核重测通过。编码对照不等同全部生成流已执行或全组件加速收益。',
      'ASM.md':'SSE2整数编码两盘各245种与GNU as逐字节相同；寄存器/内存/SIB/位移/0和255立即数覆盖，六类非法指令明确拒绝并保留旧输出。实际执行及状态隔离另有证据，见M9-ACCEPTANCE。',
      'AUDIO.md':f'25有效WAV/11无效容器双盘80项通过；独立MP3四组CBR/VBR/mono/joint stereo共473920个S16样本最大差1LSB、34项通过。FLAC/完整封面/迷你/选曲/两主题最终{pc}项及禁FPU20项通过，开关机与六提示4VM/16段连续波形通过；并有完整客户像素/页回收。最后逐行窗外检查发现并修复兜底320残影，07不作窗外恢复证据。',
      'NANO-M9.md':'实际UTF8/CRLF/撤销重做/搜索/保存失败/只读与编辑→编译→调试链通过。4MiB单行额外插入拒绝后保存原4MiB完全相同；超4MiB/131073行/NUL拒绝，退出页数严格回基线，cli-limits01。',
      'SCDBG-CLI.md':'真实INT3/TF/寄存器/内存/栈/反汇编/重启退出通过。暂停态20次输入500B，16接受4拒绝，提示符保持可用；EOF后拒绝输入，继续目标读取8016B完全相同，目标/调试器退出页回基线，cli-limits01。',
      'MODULE.md':'实际一次性/常驻、8类坏头/ABI/重定位、忘记释放、注册失败及回调归属/常驻重载拒绝通过；CPL0 UD2 vector6/CS8进入fatal并明确拒绝cont。M9产物链补齐动态宽高CORE.SKM；原M7旧模块坐标/28B前缀保持，新/旧/缺CORE的完整桌面恢复另验。M10冻结尚未来临。',
      'BUILD.md':f'build31退出0，内核仍为30原字节，{build["elf_count"]}产品ELF无未解析符号/动态TLS、内核{build["kernel_bytes"]}B/BSS末端{build["kernel_bss_end"]}，225份第三方归档及两盘SCX布局通过。m9-artifacts包含动态尺寸CORE.SKM。原版M7/M8a固定ZIP复用要求与摘要见audit_m9_compat.py，源包/旧盘不覆盖。',
      'PERFORMANCE.md':'最终1920×1080/150%与1024×768/100%桌面/播放/暂停/迷你测量见build/m9-performance-20261005-03/matrix.json。两盘仅CORE.SKM变化，同色区段fill替代每个原生像素的API调用；优化前02与优化后03完整工作区逐字节相同，原生分辨率不变。空闲单核心CPU观察7.42%→3.52%、11.33%→6.25%，仅本机4秒样本。所有PIT/CPU/提交/DMA/欠载/往返分布/页回收实测，不能扩为硬件帧率、全组件或M8游戏达标。',
      'THIRD-PARTY.md':'七个直接代码组件及225份实际引用源码/原文归档核对通过，/SYS/LICENSE完整保留。FFmpeg/GNU as仅用作宿主独立参考，不随产品复制或链接。最终源码/产物未增加第三方运行组件。',
    }
    for name,text in records.items():
        append('docs/'+name,text)
        p=ROOT/'docs'/name;s=p.read_text(encoding='utf-8');s=s.replace('\n\n','\n\n> 当前验证范围已更新，见末尾“'+MARK+'”及[M9验收报告](M9-ACCEPTANCE.md)。早期未验证/待验标记保留阶段背景。\n\n',1);p.write_text(s,encoding='utf-8')
    print('M9 docs updated; pending human acceptance')

if __name__=='__main__':main()
