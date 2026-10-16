"""复用既有 VM/Harness 的短暂停只读快照；没有 VM 启动、客体调用或写内存入口。"""
from __future__ import annotations

import hashlib
from pathlib import Path
import re
import struct
import time

PRIVATE_SOURCE_PINS = {
    "kernel/fs.c": "e0e916990807587df741ee023a3d36741bce85f4c15fb5c0523f52351bc2fb52",
    "kernel/objpool.c": "4f94afcb2723a4f90a71c049a2be1314abc827dfab42c5eb0101318f5710ea2d",
    "kernel/paging.c": "e8ad73ac5ae3a84f69b7dfae7c3ad74b4f7da9d404cc50e480a9189e0e2a6d36",
    "kernel/sc32_debug.c": "c714171345e4b41bced4f9d4c17c73200256fc713126c4d853267e86344e2f7e",
    "kernel/task.c": "40495fad1e36bdc1e83e2787e6e18a4da710eea1553959a8ba3ca6f543ece2ef",
    "user/SC32DEBUG.H": "61edbdf3fb76e422032bb68669656b4950807601e2edadf9e7b558c5ecda2a68",
}


def digest(body):
    return hashlib.sha256(body).hexdigest()


def elf_objects(path):
    """沿既有 FILE/OBJECT 解码办法保留完整作用域，不能合并两个 native_pool。"""
    body = Path(path).read_bytes()
    if body[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("需要当前 ELF32 LE")
    start = struct.unpack_from("<I", body, 32)[0]
    width, count = struct.unpack_from("<HH", body, 46)
    if width != 40 or start + width * count > len(body):
        raise ValueError("ELF 段表错误")
    sections = [struct.unpack_from("<10I", body, start + i * width) for i in range(count)]
    out = {}
    for s in sections:
        if s[1] != 2:
            continue
        if s[6] >= count or s[9] != 16 or s[5] % 16 or s[4] + s[5] > len(body):
            raise ValueError("ELF 符号表错误")
        st = sections[s[6]]
        names = body[st[4]:st[4] + st[5]]
        scope = ""
        for at in range(s[4], s[4] + s[5], 16):
            name, address, size, info, _, index = struct.unpack_from("<IIIBBH", body, at)
            if name >= len(names) or names.find(b"\0", name) < 0:
                raise ValueError("ELF 字符串错误")
            text = names[name:names.index(0, name)].decode("ascii")
            if info & 15 == 4:
                scope = text.replace("\\", "/").rsplit("/", 1)[-1]
            elif info & 15 == 1 and scope and index:
                key = scope + "::" + text
                if key in out:
                    raise ValueError("ELF 作用域重复：" + key)
                out[key] = dict(address=address, bytes=size)
    return out


def desktop_binding(image, image_sha256, symbols, symbols_sha256, layout):
    """正式 SCDB 或本批 SCLD map；完整文件摘要都由真实生成报告传入。"""
    image, symbols = Path(image), Path(symbols)
    image_body, body = image.read_bytes(), symbols.read_bytes()
    if digest(image_body) != image_sha256 or digest(body) != symbols_sha256:
        raise ValueError("桌面完整产物或调试文件摘要不匹配")
    if image_body[:8] != b"SCX1MIO\0":
        raise ValueError("桌面必须是实际 SCX/SCD 容器")
    matches = []
    if body[:8] == b"SCDB1MIO":
        if len(body) < 96 or struct.unpack_from("<4I", body, 8) != (1, 96, 3, 1):
            raise ValueError("SCDB 版本、机器或地址域不匹配")
        if struct.unpack_from("<I", body, 24)[0] != len(image_body) or body[32:64].hex() != image_sha256:
            raise ValueError("SCDB 没有绑定这份完整 SCX")
        off, count = struct.unpack_from("<II", body, 64)
        strings, size = struct.unpack_from("<II", body, 88)
        if off < 96 or count > (len(body) - off) // 24 or strings > len(body) or size > len(body) - strings:
            raise ValueError("SCDB 表越界")
        names = body[strings:strings + size]
        for i in range(count):
            key, address, width, kind, binding, obj = struct.unpack_from("<6I", body, off + i * 24)
            if key >= size or names.find(b"\0", key) < 0:
                raise ValueError("SCDB 名字越界")
            if names[key:names.index(0, key)] == b"desk" and kind == 2:
                matches.append((address, width, obj, binding))
        source_format = "SCDB1MIO_V1_SCX_ABSOLUTE_VA"
    else:
        text = body.decode("utf-8").replace("\r\n", "\n")
        if not text.startswith("SCDBMAP1MIO VA\n"):
            raise ValueError("只支持正式 SCDB 或本批 SCLD map")
        matches = [(int(a, 16), int(n), int(o), 0 if b == "local" else 1) for a, n, o, b in
                   re.findall(r"^OBJECT ([0-9A-Fa-f]+) ([0-9]+) desk object=([0-9]+) binding=(local|global)$", text, re.M)]
        source_format = "SCDBMAP1MIO_VA_SIZE_NAME_OBJECT_BINDING"
    if len(matches) != 1 or matches[0][1] != layout["values"]["desk_size"]:
        raise ValueError("没有唯一且尺寸匹配的真实 Desktop 对象")
    return dict(image_sha256=image_sha256, debug_sha256=symbols_sha256, image_bytes=len(image_body),
                debug_format=source_format, desk=matches[0][0], desk_size=matches[0][1],
                object_index=matches[0][2])


class PausedReader:
    def __init__(self, vm, seconds):
        self.vm, self.deadline, self.cache = vm, time.monotonic() + seconds, {}

    def words(self, address, count=1):
        if address & 3:
            raise ValueError("物理 word 地址必须对齐")
        out = []
        for i in range(count):
            current = address + i * 4
            page = current & ~4095
            if page not in self.cache:
                if time.monotonic() >= self.deadline:
                    raise TimeoutError("短暂停预算到达，必须恢复 VM")
                answer = self.vm.hmp("xp /1024wx 0x%08x" % page)
                values = re.findall(r"0x([0-9a-fA-F]{8})", answer)
                if len(values) != 1024:
                    raise ValueError("物理页快照长度改变")
                self.cache[page] = tuple(int(v, 16) for v in values)
            out.append(self.cache[page][(current & 4095) // 4])
        return out

    def physical(self, address, size):
        first, end = address & ~3, (address + size + 3) & ~3
        data = struct.pack("<" + "I" * ((end - first) // 4), *self.words(first, (end - first) // 4))
        return data[address - first:address - first + size]

    def virtual(self, pd, address, size):
        # 本批 SCD 的 BSS 不保证 word 对齐；绝不能把 desk 指针向下取整。
        out = bytearray()
        while size:
            pde = self.words(pd + (address >> 22) * 4)[0]
            if pde & 0x85 != 5:
                raise ValueError("目标 PDE 不是 4KiB 用户页表")
            pte = self.words((pde & ~4095) + ((address >> 12) & 1023) * 4)[0]
            if pte & 5 != 5:
                raise ValueError("目标 PTE 不是用户页")
            take = min(size, 4096 - (address & 4095))
            out += self.physical((pte & ~4095) + (address & 4095), take)
            address += take
            size -= take
        return bytes(out)

    def pool(self, address):
        # 只遍历真实指针与 used 位，不扫描历史高水位，也不设进程数上限。
        stride = self.words(address + 1088)[0]
        if not stride or stride & 15:
            return []
        out = []
        for ri, a in enumerate(self.words(address, 256)):
            if not a:
                continue
            for mi, b in enumerate(self.words(a, 512)):
                if not b:
                    continue
                for li, block in enumerate(self.words(b, 512)):
                    if not block:
                        continue
                    used = self.words(block)[0]
                    while used:
                        bit = (used & -used).bit_length() - 1
                        key = (ri << 23) | (mi << 14) | (li << 5) | bit
                        out.append((key, block + 64 + bit * stride, stride))
                        used &= used - 1
        if len(out) != self.words(address + 1096)[0]:
            raise ValueError("对象池真实 used 位与 objects 不一致")
        return out

    def page_census(self, pd):
        ranges = {"image_stack": (0x400000, 0x800000), "heap": (0x40000000, 0x44000000),
                  "modules": (0x50000000, 0x54000000), "shared_window": (0x60000000, 0x64000000)}
        directory = self.words(pd, 1024)
        private, shared, tables, blocks = set(), set(), set(), []
        result = {}
        for name, (lo, hi) in ranges.items():
            mapped = []
            for di in range(lo >> 22, hi >> 22):
                pde = directory[di]
                if not pde & 1:
                    continue
                if pde & 0x85 != 5:
                    raise ValueError("私有区出现非用户或大页映射")
                table = pde & ~4095
                tables.add(table)
                for ti, pte in enumerate(self.words(table, 1024)):
                    if not pte & 1:
                        continue
                    if pte & 5 != 5:
                        raise ValueError("私有区 PTE 权限异常")
                    va, frame = (di << 22) | (ti << 12), pte & ~4095
                    mapped.append(frame)
                    (shared if pte & 0x800 else private).add(frame)
                    if pte & 0x200:
                        blocks.append(dict(address=va, region=name))
            result[name] = dict(mapped_pages=len(mapped), distinct_frames=len(set(mapped)))
        result.update(private_frames=sorted(private), shared_frames=sorted(shared), page_tables=sorted(tables),
                      private_pages=len(private), shared_pages=len(shared), page_table_pages=len(tables),
                      directory_pages=1, allocated_block_starts=blocks)
        return result


def snapshot(harness, core_elf, layout, binding, session, *, expected_pid=None, expected_generation=None,
             owner_census=True, pause_budget=5.0):
    """一次暂停同时取得 PF/resources/全部活 owner。只由外层已拥有的 VM 调用。"""
    elf = Path(core_elf)
    elf_sha = digest(elf.read_bytes())
    if harness.report.get("core_binding", {}).get("elf_sha256") != elf_sha:
        raise ValueError("core ELF 不是这次 VM 已 pin 的实际映像")
    objects = elf_objects(elf)
    offsets = layout["values"]
    if (offsets["record_size"], offsets["record_generation"], offsets["record_pd"], offsets["record_state"]) != (320, 168, 0, 20):
        raise ValueError("本批 task_record 布局已改变")
    if layout["headers"].get("kernel/task_store.h") != "75b81aca73f6803ed5ceded6d562f8d270b615e95e8c0b5b28e2e3a2ea197587":
        raise ValueError("session/waiting 私有尾部不是本批已审源")
    for relative, expected in dict(layout["headers"], **PRIVATE_SOURCE_PINS).items():
        pins = [v for k, v in harness.report["input_sha256"].items() if k.replace("\\", "/").endswith("/" + relative)]
        if not pins or any(v != expected for v in pins):
            raise ValueError("本批结构头不在 VM 来源 pins 中：" + relative)
    for name in ("wait_pages", "wait_count", "wait_capacity", "wait_heap"):
        if objects.get("task.c::" + name) != dict(address=harness.symbols[name], bytes=4):
            raise ValueError("wait 字段并非当前 task.c 的唯一 4B 符号")
    if harness.vm.qmp("query-status").get("running") is not True:
        raise ValueError("只接受运行中的 self-owned VM，不能恢复别人的暂停")
    result = dict(status="UNVERIFIED", guest_memory_writes=False, core_elf_sha256=elf_sha,
                  session=session, binding=binding, owners=[], original_205_pages_exempted=False,
                  owner_census_complete=False, scope="OWNER_MAPPINGS_AND_MODULE_BUFFERS_NOT_ALL_KERNEL_ALLOCATIONS")
    result["scoped_symbols"] = {k: objects[k] for k in ("task.c::wait_pages", "task_store.c::records",
        "fs.c::native_pool", "streams.c::native_pool", "sc32_debug.c::d2_states", "sc32_debug.c::d2_modules")}
    began = time.monotonic()
    try:
        harness.vm.qmp("stop")
        read = PausedReader(harness.vm, pause_budget)
        def scoped(name):
            return objects[name]["address"]
        result["physical_pages"] = read.words(harness.symbols["pf_used"])[0]
        states = {pid: (at, stride) for pid, at, stride in read.pool(scoped("sc32_debug.c::d2_states"))}
        modules = []
        for key, at, stride in read.pool(scoped("sc32_debug.c::d2_modules")):
            if stride != 384:
                raise ValueError("模块登记记录布局改变")
            words = read.words(at, 18)
            info = read.words(at + 180, 16)
            if info[:2] != [204, 1]:
                raise ValueError("公开 ScDebugModule 版本或尺寸改变")
            modules.append(dict(id=key + 1, owner=words[0], generation=words[1], metadata_pages=words[4],
                                file_pages=words[6], source_object=words[7], phase=words[9], kind=info[4],
                                image_bytes=info[6], flags=info[11], source_bytes=info[14],
                                image_sha256=read.physical(at + 244, 32).hex()))
        result["debug_modules"] = modules
        for pid, at, stride in read.pool(scoped("task_store.c::records")):
            if stride != 320:
                raise ValueError("真实 task_record 步长改变")
            generation = read.words(at + 168)[0]
            owner = dict(pid=pid, generation=generation, session=read.words(at + 216)[0],
                         state=read.words(at + 20)[0], pd=read.words(at)[0],
                         name=read.physical(at + 28, 12).split(b"\0", 1)[0].decode("ascii"),
                         waiting=read.words(at + 264)[0], wait_mask=read.words(at + 248)[0])
            if pid and owner_census:
                owner["pages"] = read.page_census(owner["pd"])
                owner["kernel_stack_pages"] = 1
                owner["kernel_stack_frame"] = read.words(at + 4)[0] - 4096
            own_modules = [v for v in modules if v["owner"] == pid and v["generation"] == generation]
            owner["registered_module_pages"] = sum(v["metadata_pages"] + v["file_pages"] for v in own_modules)
            owner["debug_loading"] = 0
            if pid in states:
                p, stride = states[pid]
                if stride != 64 or read.words(p)[0] != generation:
                    raise ValueError("debug owner 代数或布局改变")
                owner["debug_loading"] = read.words(p + 40)[0]
            result["owners"].append(owner)
        hosts = [v for v in result["owners"] if v["session"] == session and v["name"] == "SCSHELL.SCX" and v["state"] == 1]
        if len(hosts) != 1:
            raise ValueError("原 SID 没有唯一活 native 桌面 owner")
        host = hosts[0]
        if expected_pid is not None and host["pid"] != expected_pid:
            raise ValueError("原 SID 的 host PID 已改变")
        if expected_generation is not None and host["generation"] != expected_generation:
            raise ValueError("原 host PID 已被复用")
        identity = [v for v in modules if v["owner"] == host["pid"] and v["generation"] == host["generation"] and v["kind"] == 1]
        if len(identity) != 1 or identity[0]["image_sha256"] != binding["image_sha256"] or identity[0]["source_bytes"] != binding["image_bytes"] or identity[0]["flags"] & 7 != 5:
            raise ValueError("实际 host 的可信 MAIN 登记没有绑定这份完整产物")
        model = {name: struct.unpack("<I", read.virtual(host["pd"], binding["desk"] + offsets["desk_" + name], 4))[0]
                 for name in ("count", "visible", "active", "icons_pending", "icon_errors", "refresh_pending", "store", "save_pending", "layout_unsaved", "gesture")}
        result["host"] = dict(pid=host["pid"], generation=host["generation"], waiting=host["waiting"],
                              debug_loading=host["debug_loading"], model=model)
        ui = struct.unpack("<I", read.virtual(host["pd"], binding["desk"] + offsets["desk_ui"], 4))[0]
        context = {k: struct.unpack("<I", read.virtual(host["pd"], ui + offsets["context_" + k], 4))[0]
                   for k in ("stopped", "dispatching", "walking", "batch")} if ui else {"not_created": 1}
        node = struct.unpack("<I", read.virtual(host["pd"], ui + offsets["context_roots"], 4))[0] if ui else 0
        roots, seen = [], set()
        while node:
            if node in seen:
                raise ValueError("原 host 的 SCUI root 链成环")
            seen.add(node)
            root = struct.unpack("<I", read.virtual(host["pd"], node + offsets["node_window"], 4))[0]
            if root:
                roots.append({k: struct.unpack("<I", read.virtual(host["pd"], root + offsets["root_" + k], 4))[0]
                              for k in ("window", "visible", "pending", "failures")})
            node = struct.unpack("<I", read.virtual(host["pd"], node + offsets["node_next"], 4))[0]
        result["host"]["context"] = context
        result["host"]["roots"] = roots
        # icons_pending 在一批开始时会先清零，所以还要求 owner 已回到等待，
        # 防止暂停在 ScShellIconLoad 的同步读循环里就误报 ready。
        result["host_ready"] = bool(host["waiting"] and not host["debug_loading"] and model["active"] and model["visible"]
                                    and not any(model[k] for k in ("icons_pending", "refresh_pending", "store", "save_pending", "layout_unsaved", "gesture"))
                                    and not any(context.values()) and roots and all(not r["pending"] and not r["failures"] for r in roots))
        result["fs_native_files"] = []
        for key, at, stride in read.pool(scoped("fs.c::native_pool")):
            if stride != 448:
                raise ValueError("FS native 记录布局改变")
            values = read.words(at + 352, 8)
            result["fs_native_files"].append(dict(object_id=key, path=read.physical(at, 64).split(b"\0", 1)[0].decode("utf-8"),
                id=values[0], refs=values[1], ready=values[2], pending=values[4], writer=values[5],
                mutation_owner=values[6], mutation_owner_generation=values[7],
                registration_owners=[dict(pid=v["owner"], generation=v["generation"], module=v["id"]) for v in modules if v["source_object"] == key]))
        result["pending"] = {name: read.words(harness.symbols[name])[0] for name in
            ("br_garbage_first", "br_notify_first", "gc_first", "session_first", "cleanup_head", "zombie_head", "cleanup_revoke_pending")}
        if owner_census:
            # 既有完整账也在这次暂停中采集。PF和这份resources必须成对使用，
            # 不能把quiet先前取得的页数配给一份之后继续变化的资源记录。
            result["resources"] = harness.resources()
            if result["resources"].get("wait_pages") != result["resources"]["finite_wait_heap"]["pages"]:
                raise ValueError("暂停后同一 wait_pages 仍不一致，观察器接线有误")
            result["owner_census_complete"] = True
            result["extra_debug_pools"] = {k: dict(zip(("flags", "objects", "pages", "peak_pages", "high_water"), read.words(scoped(k) + 1092, 5)))
                                           for k in ("sc32_debug.c::d2_states", "sc32_debug.c::d2_modules")}
        result["status"] = "ACTUAL_COHERENT_READ_ONLY_SNAPSHOT"
        result["physical_pages_end"] = read.words(harness.symbols["pf_used"])[0]
        if time.monotonic() > read.deadline:
            raise TimeoutError("暂停读取超出预算，不能宣称完整 owner 快照")
    except Exception as error:
        result.update(status="UNVERIFIED_PARTIAL_SNAPSHOT", error=repr(error), host_ready=False, owner_census_complete=False)
    finally:
        try:
            harness.vm.qmp("cont")
            result["resume_confirmed"] = harness.vm.qmp("query-status").get("running") is True
            if not result["resume_confirmed"]:
                result.update(status="UNVERIFIED_RESUME", host_ready=False)
        finally:
            result["paused_wall_seconds"] = time.monotonic() - began
    return result


def wait_original_host(harness, core_elf, layout, binding, session, *, timeout=90, pause_budget=5.0):
    """轮询实际终态；轮询间隔只让出，不作为 ready 的替代条件。"""
    began, observed, owner = time.monotonic(), [], None
    while time.monotonic() - began < timeout:
        value = snapshot(harness, core_elf, layout, binding, session, expected_pid=owner[0] if owner else None,
                         expected_generation=owner[1] if owner else None, owner_census=False, pause_budget=pause_budget)
        observed.append(value)
        if value["status"] != "ACTUAL_COHERENT_READ_ONLY_SNAPSHOT":
            return dict(status="UNVERIFIED_OBSERVER", observations=observed)
        pending = value["pending"]
        quiet = all(v == (0xffffffff if k in ("cleanup_head", "zombie_head") else 0) for k, v in pending.items())
        if owner is None:
            owner = (value["host"]["pid"], value["host"]["generation"])
        boot_io_complete = not value["fs_native_files"] and all(not v["debug_loading"] for v in value["owners"])
        if value["host_ready"] and quiet and boot_io_complete:
            full = snapshot(harness, core_elf, layout, binding, session, expected_pid=owner[0], expected_generation=owner[1], pause_budget=pause_budget)
            observed.append(full)
            if full["status"] != "ACTUAL_COHERENT_READ_ONLY_SNAPSHOT":
                return dict(status="UNVERIFIED_OBSERVER", observations=observed)
            pending = full["pending"]
            quiet = all(v == (0xffffffff if k in ("cleanup_head", "zombie_head") else 0) for k, v in pending.items())
            if full["host_ready"] and quiet and not full["fs_native_files"] and all(not v["debug_loading"] for v in full["owners"]):
                return dict(status="ACTUAL_ORIGINAL_HOST_READY", snapshot=full, observations=observed)
        if harness.guest.errors or harness.vm.process.poll() is not None:
            break
        harness.guest.stop.wait(.2)
    return dict(status="UNVERIFIED_READY_TIMEOUT", observations=observed)
