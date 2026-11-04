#!/usr/bin/env python3
"""Снимок статистики текущего видео для ручных прогонов AFRd2.

Зачем: «плавно/рвано» зависит и от режима, и от самого контента (криво
пережатые ролики, VFR, телесин). Один взгляд на экран этого не разделяет,
поэтому на каждый прогон снимаем объективные числа и рядом пишем вердикт
владельца. Из накопленной таблицы видно, где виноват режим, а где контент.

Что снимается:
  * режим дисплея (sysfs) и фактическая частота (vinfo);
  * интервалы фактической и запрошенной подачи видео-слоя
    (`dumpsys SurfaceFlinger --latency`): n, среднее, медиана, p10/p90,
    доля интервалов вне ±20 % от медианы, максимальный разрыв;
  * счётчик кадров декодера за 6 с и заявленные драйвером поля.

Использование (на хосте, adb в PATH):
  tools/stand/video_stats.py 192.168.88.42:5555 io.gh.reisxd.tizentube.cobalt "кино 24p" [плавно|рвано|край]
Результат — строка CSV в stdout и (если задан AFRD_STATS) дописывается в файл.
"""

import os
import statistics
import subprocess
import sys
import time


def adb(serial, *args, timeout=30):
    return subprocess.run(["adb", "-s", serial, *args], capture_output=True,
                          text=True, timeout=timeout).stdout


def shell(serial, cmd):
    return adb(serial, "shell", cmd)


def layer_name(serial, package):
    out = shell(serial, "dumpsys SurfaceFlinger --list 2>/dev/null")
    best = ""
    for line in out.splitlines():
        if "SurfaceView" not in line or "Background" in line:
            continue
        if package in line:
            best = line.strip()
            # новые сборки: SurfaceView[...](BLAST); старые: SurfaceView - pkg/act
            if "(BLAST)" in line or "SurfaceView - " in line:
                return line.strip()
    return best


def parse_latency(text):
    rows = []
    for line in text.splitlines():
        p = line.split()
        if len(p) != 3:
            continue
        try:
            d, a, _ = map(int, p)
        except ValueError:
            continue
        if 0 < a < 2 ** 63 - 1:
            rows.append((d, a))
    rows.sort(key=lambda x: x[1])
    return rows


def interval_stats(values):
    d = [values[i + 1] - values[i] for i in range(len(values) - 1)]
    d = [x for x in d if 0 < x < 200_000_000]
    if not d:
        return None
    d.sort()
    n = len(d)
    md = d[n // 2]
    out = sum(1 for x in d if x < 0.8 * md or x > 1.2 * md) * 100.0 / n
    return {
        "n": n,
        "mean": sum(d) / n / 1e6,
        "median": md / 1e6,
        "p10": d[int(n * 0.1)] / 1e6,
        "p90": d[int(n * 0.9)] / 1e6,
        "outside": out,
        "max": d[-1] / 1e6,
    }


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    serial, package = sys.argv[1], sys.argv[2]
    label = sys.argv[3] if len(sys.argv) > 3 else ""
    verdict = sys.argv[4] if len(sys.argv) > 4 else ""

    mode = shell(serial, "cat /sys/class/display/mode").strip()
    vinfo = shell(serial, "cat /sys/class/display/vinfo 2>/dev/null")
    vname = ""
    for line in vinfo.splitlines():
        if "name:" in line:
            vname = line.split("name:")[1].strip()
            break

    vdec = shell(serial, "cat /sys/class/vdec/vdec_status 2>/dev/null")
    dev = ""
    decl = ""
    for line in vdec.splitlines():
        if "device name" in line:
            dev = line.split(":", 1)[1].strip()
        if "frame rate" in line and "fps" in line:
            decl += "fr=" + line.split(":", 1)[1].strip() + " "
        if "frame dur" in line:
            decl += "dur=" + line.split(":", 1)[1].strip() + " "

    layer = layer_name(serial, package)
    lat = shell(serial, f'dumpsys SurfaceFlinger --latency "{layer}" 2>/dev/null') if layer else ""
    rows = parse_latency(lat)
    act = interval_stats([a for _, a in rows])
    des = interval_stats([d for d, _ in rows])

    c0 = shell(serial, "cat /sys/class/vdec/vdec_status | sed -n 's/.*frame count *: *//p'").strip()
    t0 = time.time()
    time.sleep(6)
    c1 = shell(serial, "cat /sys/class/vdec/vdec_status | sed -n 's/.*frame count *: *//p'").strip()
    dt = time.time() - t0
    try:
        fps = (int(c1) - int(c0)) / dt
    except ValueError:
        fps = 0.0

    def f(x, spec=".2f"):
        return format(x, spec) if x is not None else ""

    fields = [
        label, verdict, serial, mode, vname, layer, dev,
        decl.strip().replace(",", ";"),
        f(act["n"] if act else None, "d"), f(act["mean"] if act else None),
        f(act["median"] if act else None), f(act["p10"] if act else None),
        f(act["p90"] if act else None), f(act["outside"] if act else None, ".0f"),
        f(act["max"] if act else None),
        f(des["median"] if des else None), f(fps),
    ]
    header = ("label,verdict,stand,mode,vinfo,layer,decoder,declared,act_n,act_mean_ms,"
              "act_median_ms,act_p10_ms,act_p90_ms,act_outside20_pct,act_max_ms,"
              "desired_median_ms,counter_fps")
    line = ",".join(str(x) for x in fields)
    print(header)
    print(line)
    path = os.environ.get("AFRD_STATS")
    if path:
        new = not os.path.exists(path)
        with open(path, "a") as fh:
            if new:
                fh.write(header + "\n")
            fh.write(line + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
