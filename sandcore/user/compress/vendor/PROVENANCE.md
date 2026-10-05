# miniz 来源

上游：https://github.com/richgel999/miniz

固定提交：`b9dd683c42c965a5e98bdb9d4eb322defd966a7c`，2026-10-04下载。
保留完整MIT许可证及各源文件版权声明。除本地生成的`miniz_export.h`外，
下列文件逐字保留上游内容。仅使用低层tdefl/tinfl；GZIP/ZIP格式、
命令行、SandFS事务、路径校验与流式调度由SandCore自己实现。

| 文件 | SHA256 |
|---|---|
| LICENSE | 0115478d567121238cf6cc1c0c361926cf07a49d9e4c9e66da97fac6a01646b3 |
| miniz.h | 82a729d609c3006bf7949225a8d8b290fda249b68e4440d0d4980a7805af0598 |
| miniz_common.h | d0770cc478923e33bf22820fc51bbe22b6b99c3230ce4f5fc10eea3debc75623 |
| miniz_tdef.h | 18d4da29bd70a9395404e60b35b25e690fbc6bbc540bade2581639be05d5534b |
| miniz_tdef.c | 989e124126377e9e892469749822e3582fa11afc6ad2ec6f53c0f0fba20869a7 |
| miniz_tinfl.h | 2c89615a4cbc5de4eac5b861051849392ec698264ee7dd399ea587c592cf68d1 |
| miniz_tinfl.c | 7113a08a7722d671ce89b8de61fe6d02d0cadca0ff274ba357bf2a9dc85745ca |
| miniz_zip.h | 43d09867f79993a9ba1627cb1f3bbd78d3bf5ee0c91cd6e7527aa9ca1e3aed44 |

私有include/仅库适配，不向`SYS/INC`发布标准C运行库。
第一阶段未编译、未运行，不能由上游成熟度推断移植通过。
