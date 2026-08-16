#!/usr/bin/env python3
"""usb-vfs 镜像布局交叉验证 (开发期工具, 无需真机)

用 Python 独立实现与 usb-vfs.ino 相同的 FAT12 构建逻辑, 再自行解析校验:
  - BPB 字段 / 引导签名
  - FAT12 打包与链完整性 (STATUS 链长=8, README 单簇)
  - 根目录项与 FAT 一致性
两套独立实现对同一布局的解读一致 => 布局常量与打包算法自洽。

用法: python verify_image.py [--dump img]
"""
import struct
import sys

SECTORS = 64
SECTOR = 512
STATUS_FIRST_CLUST = 3
STATUS_CLUSTERS = 8
README_CLUSTER = 11
README_SECTOR = README_CLUSTER + 1

README = (b"W96P USB VFS demo\r\n"
          b"STATUS.TXT = live fan snapshot (refresh 500ms)\r\n"
          b"This disk is READ-ONLY.\r\n")

FAIL = []


def fat12_set(fat, idx, v):
    off = idx + (idx >> 1)
    if idx & 1:
        fat[off] = (fat[off] & 0x0F) | ((v << 4) & 0xF0)
        fat[off + 1] = (v >> 4) & 0xFF
    else:
        fat[off] = v & 0xFF
        fat[off + 1] = (fat[off + 1] & 0xF0) | ((v >> 4) & 0x0F)


def fat12_get(fat, idx):
    off = idx + (idx >> 1)
    if idx & 1:
        return ((fat[off] & 0xF0) >> 4) | (fat[off + 1] << 4)
    return fat[off] | ((fat[off + 1] & 0x0F) << 8)


def build():
    disk = bytearray(SECTORS * SECTOR)

    # Boot sector
    b = bytearray(SECTOR)
    b[0:3] = b"\xEB\x3C\x90"
    b[3:11] = b"MSDOS5.0"
    struct.pack_into("<H", b, 11, SECTOR)
    b[13] = 1
    struct.pack_into("<H", b, 14, 1)
    b[16] = 1
    struct.pack_into("<H", b, 17, 16)
    struct.pack_into("<H", b, 19, SECTORS)
    b[21] = 0xF8
    struct.pack_into("<H", b, 22, 1)
    struct.pack_into("<H", b, 24, 1)
    struct.pack_into("<H", b, 26, 1)
    b[38] = 0x29
    struct.pack_into("<I", b, 39, 0x78563412)
    b[43:54] = b"USB VFS   "
    b[54:62] = b"FAT12   "
    b[510:512] = b"\x55\xAA"
    disk[0:SECTOR] = b

    # FAT (1 sector)
    fat = bytearray(SECTOR)
    fat12_set(fat, 0, 0xFF8)
    fat12_set(fat, 1, 0xFFF)
    # 簇 2 空闲 0x000
    for c in range(STATUS_FIRST_CLUST, STATUS_FIRST_CLUST + STATUS_CLUSTERS - 1):
        fat12_set(fat, c, c + 1)
    fat12_set(fat, STATUS_FIRST_CLUST + STATUS_CLUSTERS - 1, 0xFFF)
    fat12_set(fat, README_CLUSTER, 0xFFF)                     # README 单簇 EOC
    disk[SECTOR:2 * SECTOR] = fat

    # Root dir (1 sector, 16 entries)
    root = bytearray(SECTOR)

    def dirent(off, name8, ext3, attr, cluster, size):
        e = bytearray(32)
        # bytearray 切片赋值会按右值长度伸缩切片, 必须 ljust 到精确宽度
        e[0:8] = name8.ljust(8, b" ")
        e[8:11] = ext3.ljust(3, b" ")
        e[11] = attr
        e[13] = 0x3C  # tenths
        e[14:16] = struct.pack("<H", 0x55F2)   # 13:42:30
        e[16:18] = struct.pack("<H", 0x4A6D)   # 2026-08-16
        e[18:20] = struct.pack("<H", 0x4A6D)
        e[22:24] = struct.pack("<H", 0x55F2)
        e[24:26] = struct.pack("<H", 0x4A6D)
        struct.pack_into("<H", e, 26, cluster)
        struct.pack_into("<I", e, 28, size)
        root[off:off + 32] = e

    dirent(0, b"USB VFS", b"", 0x08, 0, 0)
    dirent(32, b"README", b"TXT", 0x20, README_CLUSTER, len(README))
    dirent(64, b"STATUS", b"TXT", 0x20, STATUS_FIRST_CLUST, 0)
    disk[2 * SECTOR:3 * SECTOR] = root

    # README content
    disk[README_SECTOR * SECTOR:(README_SECTOR + 1) * SECTOR] = README

    # 模拟 refreshStatusFile(): 写初始 STATUS 内容 + 更新目录项 size
    # (与 usb-vfs.ino 的 buildStatusText/refreshStatusFile 对应; t 固定为 0)
    status_text = (b"W96P USB VFS MOCK\r\n"
                   b"SPD    0%\r\n"
                   b"GEAR 1\r\n"
                   b"POW  ON\r\n"
                   b"BAT  4.05V\r\n"
                   b"MOT  120mA\r\n"
                   b"TUR  0s\r\n"
                   b"TS   0\r\n")
    status_sector0 = (STATUS_FIRST_CLUST + 1) * SECTOR
    for i in range(STATUS_CLUSTERS):
        off = i * SECTOR
        sec = bytearray(SECTOR)
        chunk = status_text[off:off + SECTOR]
        sec[:len(chunk)] = chunk
        for j in range(len(chunk), SECTOR):
            sec[j] = 0x20
        disk[status_sector0 + i * SECTOR:status_sector0 + (i + 1) * SECTOR] = sec
    struct.pack_into("<I", disk, 2 * SECTOR + 64 + 28, len(status_text))

    return disk


def read_file(disk, fat, first_cluster, size):
    """模拟主机: 从起始簇沿 FAT 链读 size 字节。"""
    out = bytearray()
    c = first_cluster
    while len(out) < size:
        sec = (c + 1) * SECTOR
        chunk = disk[sec:sec + SECTOR]
        out += chunk[: min(SECTOR, size - len(out))]
        nxt = fat12_get(fat, c)
        if nxt >= 0xFF8:
            break
        c = nxt
    return bytes(out[:size])


def check(name, cond, detail=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {detail}")
    if not cond:
        FAIL.append(name)


def verify(disk):
    print("== BPB ==")
    b = disk[0:SECTOR]
    check("bytes_per_sector", struct.unpack_from("<H", b, 11)[0] == 512)
    check("sectors_per_cluster", b[13] == 1)
    check("reserved", struct.unpack_from("<H", b, 14)[0] == 1)
    check("num_fats", b[16] == 1)
    check("root_entries", struct.unpack_from("<H", b, 17)[0] == 16)
    check("total_sectors", struct.unpack_from("<H", b, 19)[0] == SECTORS)
    check("boot_signature", b[510] == 0x55 and b[511] == 0xAA)

    print("== FAT ==")
    fat = disk[SECTOR:2 * SECTOR]
    check("cluster0=0xFF8", fat12_get(fat, 0) == 0xFF8)
    check("cluster1=0xFFF", fat12_get(fat, 1) == 0xFFF)
    check("cluster2=0x000(free)", fat12_get(fat, 2) == 0x000)
    chain = []
    c = STATUS_FIRST_CLUST
    while c < 0xFF8 and len(chain) <= STATUS_CLUSTERS + 2:
        chain.append(c)
        c = fat12_get(fat, c)
    check("STATUS chain length==8", len(chain) == STATUS_CLUSTERS,
          f"got {len(chain)}: {chain}")
    check("STATUS chain ends 0xFFF", c == 0xFFF, f"end={c:#x}")
    check("cluster11=0xFFF(README EOC)", fat12_get(fat, README_CLUSTER) == 0xFFF)
    free = [fat12_get(fat, i) for i in range(12, SECTORS - 3)]
    check("clusters 12-61 free", all(v == 0 for v in free))

    print("== Root dir ==")
    root = disk[2 * SECTOR:3 * SECTOR]
    e0, e1, e2 = root[0:32], root[32:64], root[64:96]
    check("entry0 volume label", e0[0:8] == b"USB VFS".ljust(8) and e0[11] == 0x08)
    check("entry1 README name", e1[0:11] == b"README".ljust(8) + b"TXT")
    r_cluster, r_size = struct.unpack_from("<HI", e1, 26)
    check("entry1 README cluster", r_cluster == README_CLUSTER, f"{r_cluster}")
    check("entry1 README size", r_size == len(README), f"{r_size} vs {len(README)}")
    check("entry2 STATUS name", e2[0:11] == b"STATUS".ljust(8) + b"TXT")
    s_cluster = struct.unpack_from("<H", e2, 26)[0]
    check("entry2 STATUS cluster", s_cluster == STATUS_FIRST_CLUST, f"{s_cluster}")

    print("== Read-back (主机视角完整读文件) ==")
    fat = disk[SECTOR:2 * SECTOR]
    readme = read_file(disk, fat, README_CLUSTER, len(README))
    check("README content exact", readme == README)
    status_size = struct.unpack_from("<I", e2, 28)[0]
    check("STATUS size>0 and <=cap", 0 < status_size <= STATUS_CLUSTERS * SECTOR,
          f"{status_size}")
    status = read_file(disk, fat, STATUS_FIRST_CLUST, status_size)
    check("STATUS content exact (fixed t)",
          status == b"W96P USB VFS MOCK\r\nSPD    0%\r\nGEAR 1\r\nPOW  ON\r\n"
                     b"BAT  4.05V\r\nMOT  120mA\r\nTUR  0s\r\nTS   0\r\n")

    print(f"== RESULT: {'PASS' if not FAIL else 'FAIL(%d)' % len(FAIL)} ==")
    return not FAIL


def main():
    disk = build()
    ok = verify(disk)
    if "--dump" in sys.argv:
        with open("usb-vfs.img", "wb") as f:
            f.write(disk)
        print("dumped usb-vfs.img (%d B)" % len(disk))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
