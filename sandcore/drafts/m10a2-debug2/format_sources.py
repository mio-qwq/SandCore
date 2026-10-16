"""仅整理本草稿C分号后的行界，不改变token或主工作树。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent
for folder in ("user", "kernel"):
    for path in (ROOT / folder).rglob("*"):
        if path.suffix not in (".c", ".C", ".inc"):
            continue
        data = path.read_text(encoding="utf-8")
        result = []
        quote = None
        block = line = escaped = False
        depth = 0
        indent = ""
        pos = 0
        for index, ch in enumerate(data):
            nxt = data[index + 1:index + 2]
            if ch == "\n":
                line = False
                pos = index + 1
                end = pos
                while end < len(data) and data[end] in " \t":
                    end += 1
                indent = data[pos:end]
            if line:
                result.append(ch)
                continue
            if block:
                result.append(ch)
                if ch == "/" and index and data[index - 1] == "*":
                    block = False
                continue
            if quote:
                result.append(ch)
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == quote:
                    quote = None
                continue
            if ch == "/" and nxt == "*":
                block = True
            elif ch == "/" and nxt == "/":
                line = True
            elif ch in "\"'":
                quote = ch
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            result.append(ch)
            if ch == ";" and depth == 0 and nxt and nxt != "\n":
                result.append("\n" + indent)
        # 按真实token的花括号层级统一缩进；字符串/中文注释中的
        # 括号不参与，预处理指令保持首列，不改变任何C token。
        formatted = []
        braces = 0
        in_comment = False
        for raw in "".join(result).splitlines():
            body = raw.strip()
            if not body:
                formatted.append("")
                continue
            leading_close = len(body) - len(body.lstrip("}"))
            amount = max(0, braces - leading_close)
            formatted.append(body if body.startswith("#") else "    " * amount + (" " if body.startswith("*") else "") + body)
            mode = None
            escape = False
            j = 0
            while j < len(body):
                ch = body[j]
                nxt = body[j + 1:j + 2]
                if in_comment:
                    if ch == "*" and nxt == "/":
                        in_comment = False
                        j += 2
                        continue
                elif mode:
                    if escape:
                        escape = False
                    elif ch == "\\":
                        escape = True
                    elif ch == mode:
                        mode = None
                elif ch == "/" and nxt == "/":
                    break
                elif ch == "/" and nxt == "*":
                    in_comment = True
                    j += 2
                    continue
                elif ch in "\"'":
                    mode = ch
                elif ch == "{":
                    braces += 1
                elif ch == "}":
                    braces -= 1
                j += 1
        path.write_text("\n".join(formatted) + "\n", encoding="utf-8", newline="\n")
