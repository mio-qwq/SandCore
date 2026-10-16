"""复用现成i386布局提取器，只补task名字字段；不执行客体/启动VM。"""
from pathlib import Path
import argparse
import sys
import model

sys.path.insert(0,str(model.ROOT/"tools"))
import m10a2_shell_inspect as inspect


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args();out=args.out.resolve()
    if not out.is_relative_to(model.ROOT/"build") or not out.name.startswith("m10a2-p6-guest-generation") or out.exists():
        raise ValueError("需要本任务build前缀内新输出目录")
    saved=dict(inspect.FIELDS)
    try:
        inspect.FIELDS.update(record_name="O(task_record_t,task)+O(task_t,name)",record_name_bytes="sizeof(((task_t *)0)->name)")
        inspect.build_layout(model.ROOT,out)
    finally:
        inspect.FIELDS.clear();inspect.FIELDS.update(saved)


if __name__=="__main__":main()
