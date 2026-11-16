"""4x4 ビット転置 (バイト間隔ニブル) の導出と検証

c2p 高速化用: 4 ピクセル (u32、ピクセル 0 が MSB バイト) を
4 プレーン分のニブル (バイト 0..3 = プレーン 0..3、bit3 = ピクセル 0) に転置する。

Usage: python c2p_transpose_check.py
"""
import random


def transpose_ref(x):
    """ビット単位の参照実装: (r,c) -> (c,r) (r=バイト位置, c=ビット位置)"""
    x &= 0x0F0F0F0F
    out = 0
    for r in range(4):
        for c in range(4):
            if x & (1 << (8 * r + c)):
                out |= 1 << (8 * c + r)
    return out


def transpose_butterfly(x):
    """バタフライ 3 段 (距離 7, 14, 21) による実装"""
    t = (x ^ (x >> 7)) & 0x00080402
    x = x ^ t ^ (t << 7)
    t = (x ^ (x >> 14)) & 0x00000804
    x = x ^ t ^ (t << 14)
    t = (x ^ (x >> 21)) & 0x00000008
    x = x ^ t ^ (t << 21)
    return x


def main():
    # 1) 全単一ビット入力 (ニブル位置のみ)
    fails = 0
    for b in [0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27]:
        x = 1 << b
        if transpose_butterfly(x) != transpose_ref(x):
            fails += 1
            print(f"FAIL single bit {b}: got {transpose_butterfly(x):08X} want {transpose_ref(x):08X}")
    # 2) ランダム (ニブル値)
    random.seed(1234)
    for _ in range(200000):
        x = random.getrandbits(32) & 0x0F0F0F0F
        if transpose_butterfly(x) != transpose_ref(x):
            fails += 1
            print(f"FAIL random {x:08X}: got {transpose_butterfly(x):08X} want {transpose_ref(x):08X}")
            break
    # 3) 全ニブル値の組み合わせ (16^4 = 65536)
    for v in range(1 << 16):
        x = 0
        for i in range(4):
            x |= ((v >> (4 * i)) & 0xF) << (8 * i)
        if transpose_butterfly(x) != transpose_ref(x):
            fails += 1
            print(f"FAIL nibble combo {x:08X}")
            break
    print("FAILS:", fails)
    if fails == 0:
        print("OK: transpose_butterfly は参照実装と完全一致")


if __name__ == '__main__':
    main()
