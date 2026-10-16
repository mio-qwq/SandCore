# 先交：精确 native CORE 与九库 SDK 命令

只读准备，下面命令**尚未执行**。仅在 run03 最终 status=DECLARED_P6_NATIVE_GENERATION_PASS、current_inputs_unchanged/source_disk_unchanged=true 且 changed_input_output_paths=[] 后使用；当前部分目标通过不能替代该门槛。

## 1. 仅 CORE，三项 issuer 全指向 native

在 WSL 的项目 sandcore 目录运行，输出只放新目录，保留 HOST 构建目录和其原报告所有字节：

    make -j4 BUILD=build/m10a2-p6-final-01/core M9_BUILD=1 M10_BUILD=1 SHELL_HOST_IMAGE=build/m10a2-p6-guest-generation-run-03/native-artifacts/SCSHELL.SCX LOGIN2_ISSUE_IMAGE=build/m10a2-p6-guest-generation-run-03/native-artifacts/LOGIN2.SCX LOGIN2_ISSUE_LIBRARIES=build/m10a2-p6-guest-generation-run-03/native-libraries M10_PUBLIC_KEY=assets/trust/M10-OWNER-ED25519.PUB build/m10a2-p6-final-01/core/sandcore.img build/m10a2-p6-final-01/core/fs/SYS/CORE/CORE.SKM

只列 sandcore.img 与 CORE.SKM 两个目标，不加 m10a2-app-artifacts、m10a2-shell-artifacts、session-install 或 m10a1-publish。三个输入变量避免默认回到HOST LOGIN2/桌面/五库：

| 原生输入 | 现有工具/实际结果 |
|---|---|
| SHELL_HOST_IMAGE | mk_shell_issue.py --host N/native-artifacts/SCSHELL.SCX --out CORE/shell_host_issue.h |
| LOGIN2_ISSUE_IMAGE | mk_login_issue.py --login N/native-artifacts/LOGIN2.SCX |
| LOGIN2_ISSUE_LIBRARIES | 同工具 --libraries N/native-libraries；从这里取SCLDR/SC32API/SCCLIB/SCLIB/SCUI五库 |
| Unicode | 同工具 --unicode assets/ui/UAX15_1.SCU；必须与SDK副本同SHA |
| Login输出 | --program-out CORE/login_program_issue.h --resources-out CORE/login_resource_issue.h |

这些是现有make依赖自动调用，不必额外再跑issuer。它们生成公开内容摘要，不涉及用户私钥。M10_PUBLIC_KEY须沿当前经批准HOST CORE使用的公开输入；上面是当前m10.mk正式默认，若Root实际构建显式用了其它公开配置则保持同一配置。这里不生成/读取私钥、不重签扩展。

CORE生成后必须核真实ELF/flat/CORE载荷与实际login_issued_sha256、shell_host_issued_sha256及六条login_issued_resources，不只核头文件。prepare_candidate.py在构造安装计划时做此只读核对。

## 2. 九库 native-preview SDK（Windows Python）

真实generation artifact/stdout记录为Windows绝对路径，故使用Windows Python，不让WSL Path误解C盘来源。

在 sandcore 的PowerShell中设置实际通过的N与实际成功HOST/archive报告；工具metadata补丁后的静态增量报告04由Root明确固定，不能猜最新目录：

    $P6Native = 'build/m10a2-p6-guest-generation-run-03'
    $P6Final = 'build/m10a2-p6-final-01'
    $P6HostReport = 'ROOT填写实际静态增量04报告的仓库相对路径'
    $P6LibProv = "$P6Native/native-libraries/provenance.json"
    $P6AppProv = "$P6Native/native-artifacts/provenance.json"
    python -B tools/make_m10a2_codec_proof.py --build-report $P6HostReport --out "$P6Final/codec-proof.json"
    $P6SdkArgs = @(
      '--profile','native-preview',
      '--out',"$P6Final/sdk",
      '--build-report',$P6HostReport,
      '--compiler-provenance',$P6AppProv,
      '--linker-provenance',$P6AppProv,
      '--host-codec-proof',"$P6Final/codec-proof.json",
      '--native-library',"SCLDR.SCL=$P6LibProv",
      '--native-library',"SC32API.SCL=$P6LibProv",
      '--native-library',"SCCLIB.SCL=$P6LibProv",
      '--native-library',"SCLIB.SCL=$P6LibProv",
      '--native-library',"SCUI.SCL=$P6LibProv",
      '--native-library',"SCSHELL.SCL=$P6LibProv",
      '--native-library',"SCGFX.SCL=$P6LibProv",
      '--native-library',"SCTERM.SCL=$P6LibProv"
    )
    python -B tools/stage_m10a2_sdk.py @P6SdkArgs

codec-proof.json和sdk目录必须不存在；失败记录保留、换新明确输出名，不覆盖。build_report须符合原工具的真实成功/源码守恒/完整inputs-outputs schema；静态增量只能如实复用未变编译产物、固定非编译工具变更，不伪造重新编译。

最终八个普通库均来自同一实际native报告；SCSOUND.SCL只来自明确host codec proof，固定五导出和完整dr_mp3/dr_flac许可闭包。compiler为本轮真实SCCC1.SCX槽，linker为最终SCO/native-SCLD产出的SCLD.SCX，不用SCLD0 seed代替最终工具。

SDK工具已配置九库、公共INC、原生LINK/HOSTLINK、SYS/BIN工具、源码/文档/许可/Unicode/TTF/ResearchDesk。它不安装14应用、LOGIN2、桌面和worker；下一步用同一份extra补齐。无需复制整个build。

## 3. 只准备下一VM的payload，不先做256MiB最终盘

后续由GUI QemuSession将原只读盘复制到本VM唯一sanddata.img，data_prepare回调使用prepare_candidate.Candidate.install。只先产生SDK小文件与本CORE目录；不先mkfs再让QemuSession复制第二张256MiB盘。

原GUI-FLOW保持冻结，不改。GUI阶段复用已有harness类/ScuiInput/owner账，不能把旧P4/P5脚本main接受的旧报告状态硬改成P6 PASS。验收后可直接将本VM已验证的sanddata.img记为后续发行来源，暂不复制大盘。

修订：2026-10-16，只读准备精确命令；native生成、CORE构建、SDK staging及GUI安装执行均由Root负责。
