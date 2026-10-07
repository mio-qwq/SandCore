"""及时持久化已收齐的验收报告，保留重启前的完整检查点。"""
import json
import os


def checkpoint(path, report):
    # 宿主重启可能跳过finally；每项完成就刷入独立临时文件，再原子
    # 替换报告，避免已完成结果只留在内存或把上个完整报告截成空文件。
    # RUNNING始终表示矩阵未收齐，不能据单项检查点宣布整个矩阵通过。
    temporary = path.with_name(path.name + '.pending')
    with temporary.open('w', encoding='utf-8', newline='\n') as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)
