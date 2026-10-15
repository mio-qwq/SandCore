# SDK公开头与示例编译检查点

真实SDK native-preview暂存完成：79个payload文件、10865491B，五库及G1真实来源、公开头include闭包、许可和SHA前后守恒已检查。源为build13及P3/refresh01逐产物provenance。此处仅保存摘要、实际编译输出与日志，不复制整份SDK、五库、字体、数据盘或VFS树。

`report.json`：9.4045秒、exit0、changed_inputs=[]。仅暂存公开INC，HOST GCC用 `-nostdinc -Werror`，两个示例生成真实动态SCX；随后实际SCCC前端/后端宿主适配器从只含公开INC和独立示例的VFS生成它们。65/36 imports与各自manifest一致，正式SCD/SCX容器解析通过。

| 来源 | EDIT2 | UTILITY | 实际执行边界 |
|---|---:|---:|---|
| HOST_GCC | 17348B | 4999B | 宿主GCC构建；未执行i386应用 |
| SCCC_HOST_ADAPTER | 25613B | 7951B | 真实SCCC后端在宿主生成；未执行i386应用，未在客体编译 |

`first-driver-failure.json`保留最初检查器给HOST产物错误附加文件名后缀、被正式manifest拒绝的失败；只修驱动输出目录，示例正文未改。成功使用独立新输出目录，没有覆盖失败证据。

两个新示例仍须普通用户系统内编译与真实运行，EDIT2的实际双窗/文件/剪贴板/隐藏/DPI和资源回收也未验。不能据staging或宿主编译称SDK/Phase7/M10a2完成。
