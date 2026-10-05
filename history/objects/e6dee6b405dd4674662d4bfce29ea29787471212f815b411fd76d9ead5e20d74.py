"""mio：本阶段记录与新增需求同步；只改列明的文档，字体和历史包不变。"""
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
notes={
'README.md':'M8新增SCB2/原尺寸ARGB图标/选定Logo壁纸/64MB盘/SCX bit1和原字节LEGACY，主题切换及冷启动回归通过。用户追加全部组件、极高现代游戏画质、真实光追动画、新Shell/用户名/环境/纯CLI与Settings全配置；完整目标见根M8_GOAL.md，完整M8仍未完成。',
'docs/ROADMAP.md':'M8目标扩充至全部组件、LEGACY原字节工具、全部游戏极高现代画质、Welcome to true color.真实光追动画；配置用户名/环境和纯CLI命令、Settings通用配置编辑，默认路径统一/BIN:/APPS（用户已纠正app）。SCB2/高像素图标/选定壁纸及64MB盘已构建，完整矩阵继续。',
'docs/M8.md':'SCB2/128px Aurora与32px真实像素Classic图标、选定Logo壁纸、SCX bit1/原生SCCC可选图标参数、64MB盘和LEGACY已构建。主题鼠标切换/同盘冷启动/退出回收通过；缓存页单独计数后严格核对应用资源。图片/内置图标/Legacy矩阵继续，压缩格式与新版Shell/游戏/动画未完成。',
'docs/M8-DESIGN.md':'选定Logo图片已按原尺寸无损RGBA导出SCB2，Classic直接来自320x180/15使用色稿；Aurora128px透明物件图标与Classic真实32px固定16色图标已构建，实际主题截图见build/m8-theme。用户追加全部组件与极高现代游戏画质、实时写实光追动画，最终效果仍需完整矩阵。',
'docs/GFX.md':'THEME接口已实现并通过阶段测试；SCB2直通ARGB的格式/图标优先级/壁纸见IMAGE.md。现代图标渐变只混合tokens中已登记端点，Classic实际32px固定16色/无抖动，Logo沿用用户选定生成式资产。游戏重做和真实射线动画仍待实现。',
'docs/SYSCALL.md':'0x2C..2E命名主题接口与G2原生探针/Settings已实测。SCX bit1属于容器加载扩展，不新增图像系统调用。新版Shell/CLI的继承输出、工作目录、用户名/环境与退出码API尚待登记，不把现有EXEC异步语义称为终端管线已实现。',
'docs/WM.md':'原生桌面从主题SCB2加载壁纸和原尺寸ARGB图标，图片IO懒加载于任务0合成阶段，继续接收IRQ；图标保比例/居中透明合成。新缓存页为desktop_pages诊断统计，主题回收测试扣除合法常驻缓存变化，用户窗口/页表仍严格回收。当前VGA仍使用原桌面图标和程序壁纸，全组件/缩放矩阵继续。',
'docs/MEM.md':'SCB2壁纸/图标占连续物理页，不进入低端BSS或应用4MB映像；32项按路径/偏移缓存，重载释放旧资源，失败候选释放。desktop_pages记录常驻资源，PF退出检查扣除此项主题变化，不能将永久缓存变化误报为应用泄漏。',
'docs/FS.md':'M8超级块@24新增可用扇区数，0兼容旧8MB；新默认64MB。ATA读取IDENTIFY第60/61字并以实际容量拒绝越界，声明容量至多131072扇区且不得超过设备。依据QEMU官方core.c：https://github.com/qemu/qemu/blob/master/hw/ide/core.c 。SCB2/SCX bit1详见IMAGE.md。mkfs保存扩容前整盘、保留用户内容；只有逐字节等于已列明旧出厂默认的配置才迁移图标/壁纸，用户改过一字节即保留。LEGACY从不可变验收M7包校验并归档。',
'docs/BUILD.md':'新增image.o/SCB2资源编译；图片资源需要Pillow，WSL缺Pillow时ARTPY使用已安装Windows python.exe，仅资源转换切解释器，GCC/LD仍WSL ELF。preserve_legacy.py核对原M7 ZIP摘要后保存旧SCX/源码。数据盘默认64MB且内核读取真实设备容量，旧盘仍兼容。验证新增verify_images.py；仍需完整原生发布/三代收敛和M8包。',
'docs/C.md':'M8实现可选第三参数：s3c source.c output.scx [icon.scb]。SCB2图标最大128x128，精确校验后在私有高端堆组装SCX bit1容器；代码/数据/BSS/重定位布局不变。普通两参数仍生成无图标旧布局。原生新编译器/图标测试继续，M7三代收敛是不可变历史证据，不沿用到当前修改。',
'docs/THEME.md':'主题iconroot用于@NAME系统图标，显式资源路径与SCX内置身份独立。预设wallpaper指向SYS/WALL的已选SCB2；大资源读取与缓存释放放合成阶段，THEMELOAD不直接读照片。当前主题/Settings鼠标切换、同盘冷启动、扣除常驻缓存的退出资源核对通过，全部图片/缩放矩阵仍待完成。',
'docs/TESTING.md':'M8新增python tools/verify_images.py（SCB2/SCX坏头/原生可选图标/Lens/LEGACY）与python tools/audit_truecolor.py --theme。主题退出检查按desktop_pages作常驻资源差值，应用页仍严格回收。手工还须覆盖全部组件五档显示/三档缩放、LEGACY、新Shell/环境配置、极高现代游戏和实时光追动画；未完成项见M8_GOAL.md。',
'docs/SHELL.md':'用户要求M8重写Shell与用户空间：用户名/默认环境写配置，单用户占位，无密码/登录；提示符用户名@当前目录，默认搜索/BIN和/APPS（仅APPS，用户纠正app笔误）。ls/cat/pwd等纯命令行SCX、终端输出继承/退出码/目录、更多文件命令和Settings全配置编辑均加入正式范围，当前尚未实现。原Shell保存于LEGACY，现有异步EXEC行为继续兼容。',
'docs/IMAGE.md':'SCB2/壁纸/图标及SCX bit1已构建；主题实际切换与同盘冷启动通过。桌面常驻缓存另计desktop_pages，退出验收作资源差值。新增LEGACY、极高现代游戏画质、用户名/环境/纯CLI和全配置Settings成为M8目标；内置图标与Lens矩阵仍在本轮运行。',
}
for relative,note in notes.items():
    p=ROOT/relative;text=p.read_text(encoding='utf-8')
    if note not in text:p.write_text(text.rstrip()+'\n\n2026-10-02：'+note+'\n',encoding='utf-8')
p=ROOT/'docs/FS.md';text=p.read_text(encoding='utf-8')
rows=text.splitlines()
for i,row in enumerate(rows):
    if '资源重打包会重建目录表' in row:
        rows[i]='M7历史打包器在资源依赖改变时重建目录表；M8已改为合法v4盘合并，保留用户条目，详见本页后文。构建前仍备份sanddata.img。'
p.write_text('\n'.join(rows)+'\n',encoding='utf-8')
p=ROOT.parent/'HANDOFF.md';text=p.read_text(encoding='utf-8')
raw=(ROOT/'build/kernel.bin').stat().st_size
bss=next(row.split()[0] for row in (ROOT/'build/kernel.sym').read_text().splitlines() if row.split()[-1]=='__bss_end')
text=text.replace('本轮内核原始96428B，盘内补齐96768B/189扇区，BSS末端0x1F9FA0',f'本轮内核原始{raw}B，盘内补齐{(raw+511)//512*512}B/{(raw+511)//512}扇区，BSS末端0x{bss.upper()}')
note='M8完整目标见M8_GOAL.md：全部组件/LEGACY/极高现代游戏/真实光追动画、新Shell/用户名/环境与Settings全配置。默认目录统一/BIN和/APPS。SCB2/壁纸/高清透明图标/64MB数据盘/SCX bit1/LEGACY已构建，主题及冷启动回归通过，图片与原生图标矩阵继续。当前开发盘非完整M8交付盘。'
if note not in text:text=text.rstrip()+'\n\n2026-10-02：'+note+'\n'
p.write_text(text,encoding='utf-8')
print('M8 scope / protocols / stage evidence documentation synchronized')
