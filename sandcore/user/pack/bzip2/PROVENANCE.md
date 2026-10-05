# libbzip2 固定快照

来源：<https://sourceware.org/bzip2/>，官方归档
<https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz>，版本 1.0.8。
原压缩包 SHA256：ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269。
原包另存 third_party/imports/bzip2-1.0.8.tar.gz；摘要是来源固定记录，未声称验证上游签名。

选择原 libbzip2 的七个 C 文件、两个头文件以及完整 LICENSE/README；
不引入上游 bzip2 命令行、测试脚本或宿主平台代码。所有选中文件保持原字节。
许可为 bzip2 宽松许可（许可族标识 bzip2-1.0.6），保留版权、免责声明、
禁止错误来源表示及作者背书等原文要求，不能只写作者姓名。

SandCore 自写 CLI、字节流、SandFS 提交及私有堆适配放在本目录之外，
用 BZ_NO_STDIO 禁用上游 stdio 路径。分配函数映射及断言处理不修改固定快照。
全部整数运算，不依赖标准 C 运行库。源码状态 SOURCE-UNVERIFIED。
