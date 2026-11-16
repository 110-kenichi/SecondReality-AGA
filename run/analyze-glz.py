"""GLENZ のスクリーンショット解析 (画像をモデルに渡さず数値で見る)。

キャプチャは WinUAE ウィンドウ (736x631) で、画面は crop (65,31)-(705,599)。
2 枚を比較するときは同じフレームで撮ったものを使う (M2_GLENZ_DIAG に 4 を
足すとタイムラインが止まる)。

  python run/analyze-glz.py run/g000.png
  python run/analyze-glz.py --truth run/diag1.png --target run/normal.png
"""
import sys
from collections import Counter

import numpy as np
from PIL import Image, ImageDraw

CROP = (65, 31, 705, 599)


def load(path):
    im = Image.open(path).convert('RGB').crop(CROP)
    return np.asarray(im).astype(int)


def masks(im):
    r, g, b = im[:, :, 0], im[:, :, 1], im[:, :, 2]
    return {
        'red':   (r > 20) & (r > g + 8) & (r > b + 8),
        'green': (g > 150) & (g > r + 80) & (g > b + 80),
        'blue':  (b > 20) & (b > r + 8),
        'black': (r < 8) & (g < 8) & (b < 8),
        'lit':   (r + g + b) > 30,
    }


def components(mask, limit=40):
    """mask の連結成分 (area, x0, x1, y0, y1) を大きい順に返す。"""
    img = Image.fromarray((mask * 255).astype(np.uint8), 'L').copy()
    out = []
    for _ in range(limit):
        a = np.asarray(img)
        ys, xs = np.where(a == 255)
        if not len(ys):
            break
        ImageDraw.floodfill(img, (int(xs[0]), int(ys[0])), 128)
        b = np.asarray(img)
        comp = (b == 128) & (a == 255)
        ys, xs = np.where(comp)
        out.append((int(comp.sum()), int(xs.min()), int(xs.max()),
                    int(ys.min()), int(ys.max())))
        img = Image.fromarray(np.where(comp, 0, a).astype(np.uint8), 'L').copy()
    out.sort(reverse=True)
    return out


def game_xy(x, y):
    """キャプチャ座標 -> ゲーム座標 (320x256) の概算。"""
    return x / 2.0, y * 256.0 / 568.0


def summary(path):
    im = load(path)
    m = masks(im)
    print('image', path, 'crop', im.shape[1], 'x', im.shape[0])
    for k in ('red', 'green', 'blue', 'lit', 'black'):
        ys, xs = np.where(m[k])
        if len(xs):
            print('  %-5s %6d px  bbox x %d-%d y %d-%d'
                  % (k, len(xs), xs.min(), xs.max(), ys.min(), ys.max()))
        else:
            print('  %-5s %6d px' % (k, 0))
    # 赤の内側にある黒 (囲まれた穴) を検出
    red = m['red']
    if red.any():
        img = Image.fromarray((red * 255).astype(np.uint8), 'L').copy()
        ImageDraw.floodfill(img, (0, 0), 128)
        holes = (np.asarray(img) == 0) & ~red
        comps = [c for c in components(holes) if c[0] >= 30]
        print('  enclosed holes in red: %d comps >=30px' % len(comps))
        for a, x0, x1, y0, y1 in comps[:8]:
            gx0, gy0 = game_xy(x0, y0)
            print('    area %5d  x %3d-%3d y %3d-%3d  (game %.0f,%.0f)'
                  % (a, x0, x1, y0, y1, gx0, gy0))


def compare(truth_path, target_path):
    truth = masks(load(truth_path))
    target = masks(load(target_path))
    union = truth['green']          # 全三角形を緑で塗った診断画像
    if not union.any():
        print('truth has no green mask; use M2_GLENZ_DIAG=5')
        return
    ys, xs = np.where(union)
    print('truth %s: union %d px bbox x %d-%d y %d-%d'
          % (truth_path, int(union.sum()), xs.min(), xs.max(), ys.min(), ys.max()))
    inside = {
        'red':   int((union & target['red']).sum()),
        'green': int((union & target['green']).sum()),
        'blue':  int((union & target['blue']).sum()),
        'black': int((union & target['black']).sum()),
        'other': int((union & ~(target['red'] | target['green'] |
                                target['blue'] | target['black'])).sum()),
    }
    total = int(union.sum())
    print('target %s inside union (%d px):' % (target_path, total))
    for k, v in inside.items():
        print('  %-5s %6d px (%.1f%%)' % (k, v, 100.0 * v / total))

    missing = union & target['black']
    comps = [c for c in components(missing) if c[0] >= 30]
    print('missing (black inside union): %d px, %d comps >=30px'
          % (int(missing.sum()), len(comps)))
    for a, x0, x1, y0, y1 in comps[:12]:
        gx0, gy0 = game_xy(x0, y0)
        print('  area %5d  x %3d-%3d y %3d-%3d  (game %.0f,%.0f)'
              % (a, x0, x1, y0, y1, gx0, gy0))


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    if args[0] == '--truth':
        compare(args[1], args[3] if len(args) > 3 else args[2])
    else:
        for p in args:
            summary(p)
            print()


if __name__ == '__main__':
    main()
