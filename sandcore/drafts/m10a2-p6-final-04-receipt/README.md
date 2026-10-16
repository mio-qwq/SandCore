# final04：原生生成与新运行 CORE 来源分层

准备稿冻结。Root 亲自执行下面命令；本代理只生成计划、核 Python AST 和完整分层只读输入，没有执行 codec proof、SDK staging、CORE/make/WSL/VM，没有复制盘或修改主源/旧草稿。

从 sandcore 目录运行 Windows Python：

```powershell
python -B drafts/m10a2-p6-final-04-receipt/finalize.py --execute --runtime-host-report-sha256 67709f3aacb72c6f18ded7be61235d5e9811faf4c41671b7084f46eec254c2ca
python -B drafts/m10a2-p6-final-04-receipt/inspect_final.py
```

第一条内部顺序仍是 codec proof、严格 native-preview SDK、WSL make 的两个 CORE 目标；完整 argv、退出、日志与源守恒写入全新 `build/m10a2-p6-final-04/finalization.json`。输出目录必须不存在，失败保留且不覆盖。第二条只读核验 actual ELF/flat/CORE、boot/loader、真实 `.d` 闭包、发行 Desktop/LOGIN2/五库/UAX、公开key及SDK字节，生成本目录 receipts。默认不带 `--execute` 只写计划，已实际退出0。

固定事实分开保存：

- **原生生成 HOST09**：`879ceb99215972f60d1dfc76f0102d1a2255c8616685dd980eb4e8d5aba77ae4`。native04 inner `1ac7e951434d5465a665844019a731609a08c35c4a22a5f42bb6c43fac2b5191`；outer04 `226d9d99ae250c247744430b0247f7c7f30cc315ca1687e2a8e6b604f7317175`。均为真实原报告，不改写其 HOST09/旧 preflight 事实，也不对已经被后续构建覆盖的 HOST09 输出重新做 current verified_build。
- **运行构建 HOST10**：`67709f3aacb72c6f18ded7be61235d5e9811faf4c41671b7084f46eec254c2ca`。用原 SDK verified_build 完整核当前1327输入/41输出。H09/H10 输入路径集合必须相同，且差异精确为 `kernel/fs_lease.h`、`kernel/fs_lease_index.inc`、`kernel/fs_lease_open.inc`；其前后摘要必须等于已审 `stable-read-integration.json`。该记录的 `docs/SC32-FILE.md` 后像也独立固定，不能把 docs 误称已编译的 kernel 输入。其余输入全部相等。
- **布局02复用**：实际 SHA `6b828bbab032c4bb8ceb00ee6a94f7b55cdf0671ec18d2cb807d73240260e80a`。它真实属于 HOST09/native04 的新 UI/Desktop。五结构头在 H09、H10、原生 closure 与当前字节完全相同，TU/object/bin/JSON/原成功提取日志均核相等，所以复用，不为无关 FS lease 实现变化重编偏移。proof 任何字段均不修改；新 receipts 单列 actual_compilation_binding 与 runtime_host_report、复用依据。

原生358份用户态源、formal manifests、compiler/include closures、新公开头 map 仍逐项同时匹配 HOST09、HOST10、实际 source_plan 与文件字节；八库、21 artifacts、27 SCO 的完整集合与严格 reader 保留。普通八库继续要求 ACTUAL_SCCC_NATIVE，SCSOUND 是唯一固定 HOST codec 例外；SCCC/SCLD 保留真实原生成/执行来源，不重新生成12目标。新 codec proof 绑定当前 HOST10，未把它冒充 native04 的生成 HOST。

CORE 发行绑定直接取 native04 Desktop/LOGIN2/五库；SDK副本与其逐项同字节。make 仍只构建 `build/m10a2-p6-final-04/core/sandcore.img` 和 `build/m10a2-p6-final-04/core/fs/SYS/CORE/CORE.SKM`，公钥只用 `assets/trust/M10-OWNER-ED25519.PUB`，不涉及私钥。新 `.d` 和构建前快照核 HOST10；native_generation_binding、runtime_build_binding、layout_reuse_evidence 三表保持分开。

已完成且仅完成：三份脚本 AST、默认计划退出0、`READ-ONLY-AUDIT.json` 的全层来源核验 PASS。H10实际只变上述三 kernel 输入；五布局头一致。没有本代理构建或guest执行。后续只有实际 `CORE_AND_STRICT_SDK_VERIFIED_NOT_GUEST_BOOT` 才表示三步骤及产物核验通过，仍不等于 GUI03/重播回归或整个M10a2通过。

各 `*-from-final03.patch` 只供审阅，不应用到冻结 final03。final03 只曾由 Root 运行 layout，未生成 CORE/SDK；本组所有新运行输出归 final04，历史报告、layout02 与旧产物保持原样。
