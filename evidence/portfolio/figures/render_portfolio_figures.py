#!/usr/bin/env python3
"""作品集配图：中文主文案、强对比、少免责堆砌。数字来自已有 Orange Pi 证据。"""
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle
import numpy as np

OUT = Path(__file__).resolve().parent

# 深底工程风：避免紫渐变 / 奶油衬线那套 AI 默认
BG = "#0f1419"
PANEL = "#1a222c"
PANEL2 = "#232d3a"
INK = "#e8eef4"
MUTED = "#8b98a8"
LINE = "#3a4656"
ACCENT = "#e8a87c"  # 暖杏，不是紫
GOOD = "#5ecf8e"
BAD = "#e06c75"
IDLE = "#6b8cae"

plt.rcParams.update(
    {
        "font.family": "sans-serif",
        "font.sans-serif": [
            "Noto Sans CJK SC",
            "Noto Sans CJK JP",
            "WenQuanYi Micro Hei",
            "DejaVu Sans",
        ],
        "axes.unicode_minus": False,
        "figure.facecolor": BG,
        "axes.facecolor": PANEL,
        "text.color": INK,
        "axes.labelcolor": INK,
        "xtick.color": MUTED,
        "ytick.color": MUTED,
        "axes.edgecolor": LINE,
    }
)


def save(fig, name: str) -> None:
    path = OUT / name
    fig.savefig(path, dpi=180, bbox_inches="tight", facecolor=BG, pad_inches=0.35)
    plt.close(fig)
    print("wrote", path)


def _panel(ax, x, y, w, h, fc=PANEL):
    ax.add_patch(
        FancyBboxPatch(
            (x, y),
            w,
            h,
            boxstyle="round,pad=0.012,rounding_size=0.06",
            linewidth=1.2,
            edgecolor=LINE,
            facecolor=fc,
        )
    )


def topology() -> None:
    fig, ax = plt.subplots(figsize=(12, 5.8))
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 6)
    ax.axis("off")
    fig.patch.set_facecolor(BG)

    ax.text(0.35, 5.45, "平台分工", fontsize=22, fontweight="bold", color=INK, va="top")
    ax.text(
        0.35,
        4.95,
        "同一仓库 · 两条证据面 · 边界写在图上",
        fontsize=11,
        color=MUTED,
        va="top",
    )

    # ThinkPad
    _panel(ax, 0.35, 0.9, 5.0, 3.6, PANEL)
    ax.add_patch(Rectangle((0.35, 4.15), 0.14, 0.35, color=GOOD, lw=0))
    ax.text(0.7, 4.25, "ThinkPad · 开发与功能对照", fontsize=13, fontweight="bold", color=GOOD, va="center")
    for i, t in enumerate(
        [
            "C++20 构建 / CTest / ASan",
            "vcan + rcrd + 故障矩阵",
            "协议与监督闭环在这里关",
        ]
    ):
        ax.text(0.7, 3.5 - i * 0.55, "·  " + t, fontsize=12, color=INK, va="center")

    # Orange Pi
    _panel(ax, 6.65, 0.9, 5.0, 3.6, PANEL)
    ax.add_patch(Rectangle((6.65, 4.15), 0.14, 0.35, color=ACCENT, lw=0))
    ax.text(7.0, 4.25, "Orange Pi 4 Pro · ARM 实测", fontsize=13, fontweight="bold", color=ACCENT, va="center")
    for i, t in enumerate(
        [
            "原生 aarch64 / systemd 安装",
            "调度与 RT Lab 压力测量",
            "无 CONFIG_CAN → rcrd 未常驻",
        ]
    ):
        ax.text(7.0, 3.5 - i * 0.55, "·  " + t, fontsize=12, color=INK, va="center")

    # bridge
    ax.annotate(
        "",
        xy=(6.55, 2.7),
        xytext=(5.45, 2.7),
        arrowprops=dict(arrowstyle="-|>", color=MUTED, lw=2.0, mutation_scale=14),
    )
    ax.text(6.0, 3.05, "SSH", ha="center", fontsize=10, color=MUTED)

    ax.text(
        0.35,
        0.35,
        "不声称硬实时 · 未装板上 PREEMPT_RT · Orange Pi 无常驻 SocketCAN daemon",
        fontsize=10,
        color=BAD,
    )
    save(fig, "01_platform_topology.png")


def layers() -> None:
    fig, ax = plt.subplots(figsize=(11.5, 6.4))
    ax.set_xlim(0, 11)
    ax.set_ylim(0, 7.2)
    ax.axis("off")

    ax.text(0.4, 6.7, "Runtime 怎么分层", fontsize=22, fontweight="bold", va="top")
    ax.text(0.4, 6.2, "上测试 · 中决策 · 下 fd —— 不把总线细节灌进 Core", fontsize=11, color=MUTED, va="top")

    rows = [
        (4.9, "测试 / CLI", "benchmark · 故障矩阵 · 验收", ACCENT, "证明行为"),
        (3.7, "Daemon", "rcrd · session / 序号 / deadline", IDLE, "进程边界"),
        (2.5, "Runtime Core", "周期线程 · 状态机 · mailbox · watchdog · trace", GOOD, "控制决策"),
        (1.3, "Linux I/O", "epoll · SocketCAN · eventfd / signalfd", "#7aa2d6", "fd 事件"),
        (0.15, "对端", "ThinkPad: vcan + sim    Orange Pi: 软件 peer（无 CAN）", MUTED, "物理/仿真"),
    ]
    for y, title, body, color, tag in rows:
        _panel(ax, 0.4, y, 10.2, 1.0, PANEL)
        ax.add_patch(Rectangle((0.4, y), 0.12, 1.0, color=color, lw=0))
        ax.text(0.8, y + 0.62, title, fontsize=14, fontweight="bold", color=color, va="center")
        ax.text(0.8, y + 0.28, body, fontsize=11, color=INK, va="center")
        ax.text(9.9, y + 0.5, tag, fontsize=10, color=MUTED, ha="right", va="center")
    save(fig, "02_runtime_layers.png")


def rt1() -> None:
    """英雄数字：miss 对比；副图用线性 µs 并标注数量级。"""
    # 图级标题、坐标轴标题和页脚结论不能交给默认布局猜位置；否则保存时会互相覆盖。
    fig = plt.figure(figsize=(12, 6.1))
    ax0 = fig.add_axes([0.10, 0.20, 0.36, 0.55])
    ax1 = fig.add_axes([0.60, 0.20, 0.36, 0.55])

    fig.text(0.04, 0.95, "同核压力下：OTHER 崩、FIFO 稳住", fontsize=20, fontweight="bold")
    fig.text(
        0.04,
        0.89,
        "Orange Pi · A76 · performance · 1 ms · 60 s · RT1 smoke（dirty，非 formal）",
        fontsize=10,
        color=MUTED,
    )

    # Misses — hero
    labs = ["SCHED_OTHER", "SCHED_FIFO"]
    misses = [43530, 0]
    bars = ax0.bar(labs, misses, color=[BAD, GOOD], width=0.55, zorder=3)
    ax0.set_facecolor(PANEL)
    ax0.set_ylabel("周期失约次数", fontsize=11)
    ax0.spines["top"].set_visible(False)
    ax0.spines["right"].set_visible(False)
    ax0.set_ylim(0, 52000)
    ax0.yaxis.grid(True, color=LINE, lw=0.8, zorder=0)
    ax0.set_axisbelow(True)
    ax0.text(0, 45500, "43,530", ha="center", fontsize=16, fontweight="bold", color=BAD)
    ax0.text(1, 2200, "0", ha="center", fontsize=16, fontweight="bold", color=GOOD)
    ax0.set_title("deadline misses", color=MUTED, fontsize=11, loc="left", pad=10)

    # p99 in microseconds, linear, break annotation
    p99_us = [4000.0, 9.8]
    ax1.bar(labs, p99_us, color=[BAD, GOOD], width=0.55, zorder=3)
    ax1.set_facecolor(PANEL)
    ax1.set_ylabel("唤醒 lateness p99（µs）", fontsize=11)
    ax1.spines["top"].set_visible(False)
    ax1.spines["right"].set_visible(False)
    ax1.set_yscale("log")
    ax1.set_ylim(1, 12000)
    ax1.yaxis.grid(True, color=LINE, lw=0.8, which="both", zorder=0)
    ax1.set_axisbelow(True)
    ax1.text(0, 4800, "4.0 ms", ha="center", fontsize=13, fontweight="bold", color=BAD)
    ax1.text(1, 14, "9.8 µs", ha="center", fontsize=13, fontweight="bold", color=GOOD)
    ax1.set_title("p99 wakeup（对数轴，跨三个数量级）", color=MUTED, fontsize=11, loc="left", pad=10)

    # callout
    fig.text(
        0.53,
        0.07,
        "≈ 400×  p99 差距 · 空 callback · 不是硬实时证明",
        fontsize=11,
        color=ACCENT,
        fontweight="bold",
        ha="center",
    )
    save(fig, "03_rt1_other_vs_fifo.png")


def rt6() -> None:
    """瀑布：wakeup + queue 主导；callback 几乎看不见 —— 这才是论点。"""
    fig = plt.figure(figsize=(12, 5.8))
    # 同 RT1：给图级标题/副标题留出顶部，避免压到 120 µs 刻度。
    ax = fig.add_axes([0.12, 0.18, 0.82, 0.58])
    ax.set_facecolor(PANEL)

    fig.text(0.04, 0.95, "空 callback 只是整条路径里的一小截", fontsize=20, fontweight="bold")
    fig.text(
        0.04,
        0.89,
        "Orange Pi RT6 · 软件 peer（eventfd）· baseline p50 · 非 CAN 端到端",
        fontsize=10,
        color=MUTED,
    )

    # values in µs
    names = ["wakeup", "callback", "queue", "io_ack"]
    vals = np.array([60.3, 0.25, 34.4, 0.38])
    e2e = 96.5
    colors = [IDLE, MUTED, IDLE, MUTED]

    starts = np.cumsum(np.concatenate([[0], vals[:-1]]))
    ax.bar(names, vals, bottom=starts, color=colors, width=0.55, edgecolor=BG, linewidth=1.5, zorder=3)

    # e2e marker bar
    ax.bar(["e2e"], [e2e], color=ACCENT, width=0.55, zorder=3)

    for i, (n, v, s) in enumerate(zip(names, vals, starts)):
        label = f"{v:.1f}" if v >= 1 else f"{v:.2f}"
        ax.text(i, s + v + 2.2, label + " µs", ha="center", fontsize=11, color=INK, fontweight="bold")
    ax.text(4, e2e + 2.2, f"{e2e:.1f} µs", ha="center", fontsize=12, color=ACCENT, fontweight="bold")

    ax.set_ylabel("累计时间（µs）", fontsize=11)
    ax.set_ylim(0, 120)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.yaxis.grid(True, color=LINE, lw=0.8, zorder=0)
    ax.set_axisbelow(True)
    ax.set_xlim(-0.6, 4.6)

    ax.annotate(
        "callback ≈ 0.25 µs\n却常被当成「控制延迟」",
        xy=(1, 60.4),
        xytext=(1.85, 78),
        fontsize=10,
        color=ACCENT,
        arrowprops=dict(arrowstyle="->", color=ACCENT, lw=1.2),
    )
    save(fig, "04_rt6_segments_p50.png")


if __name__ == "__main__":
    topology()
    layers()
    rt1()
    rt6()
