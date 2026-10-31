"""Tkinter waveform viewer (no third-party GUI dependency)."""

from __future__ import annotations

import argparse
import bisect
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from typing import List, Optional

from .cli import parse_rate, parse_trigger
from .client import Capture, Sump, SumpError, find_port
from .export import write_csv, write_vcd

BG, FG, GRID = "#14161a", "#d8dde6", "#2a2e36"
COLORS = ["#ffd166", "#06d6a0", "#4cc9f0", "#ef476f", "#c77dff", "#f8961e", "#90be6d", "#f9c74f"]
LABEL_W, ROW_H, RULER_H = 64, 38, 26
RATES = ["100k", "500k", "1M", "2M", "5M", "7M", "10M"]
SIZES = ["1024", "4096", "16384", "61440"]
TRIGGERS = ["none", "rise", "fall", "high", "low"]


def fmt_time(seconds: float) -> str:
    a = abs(seconds)
    for unit, scale in (("s", 1.0), ("ms", 1e-3), ("us", 1e-6), ("ns", 1e-9)):
        if a >= scale or unit == "ns":
            return f"{seconds / scale:.3f} {unit}"
    return "0"


class Viewer(tk.Tk):
    def __init__(self, port: Optional[str]):
        super().__init__()
        self.title("BlackPill SUMP logic analyzer")
        self.configure(bg=BG)
        self.geometry("1100x520")
        self.port = port
        self.sump: Optional[Sump] = None
        self.cap: Optional[Capture] = None
        self.edges: List[List[int]] = []
        self.first: List[int] = []
        self.t0 = 0.0          # left edge of the view, in samples
        self.span = 1.0        # visible samples
        self.cursors: List[Optional[float]] = [None, None]
        self.events: "queue.Queue[tuple]" = queue.Queue()
        self.busy = False
        self._build()
        self.after(100, self._poll_events)
        self.after(50, self._connect)

    # -- layout ----------------------------------------------------------------
    def _build(self) -> None:
        bar = ttk.Frame(self, padding=6)
        bar.pack(fill="x")
        self.rate = tk.StringVar(value="1M")
        self.size = tk.StringVar(value="4096")
        self.trig_ch = tk.StringVar(value="0")
        self.trig_kind = tk.StringVar(value="none")
        self.post = tk.StringVar(value="50")
        self.pattern = tk.BooleanVar(value=False)
        for text, var, values, w in (("Rate", self.rate, RATES, 7), ("Samples", self.size, SIZES, 7)):
            ttk.Label(bar, text=text).pack(side="left", padx=(8, 2))
            ttk.Combobox(bar, textvariable=var, values=values, width=w).pack(side="left")
        ttk.Label(bar, text="Trigger").pack(side="left", padx=(12, 2))
        ttk.Combobox(bar, textvariable=self.trig_ch, values=[str(i) for i in range(8)], width=2,
                     state="readonly").pack(side="left")
        ttk.Combobox(bar, textvariable=self.trig_kind, values=TRIGGERS, width=6,
                     state="readonly").pack(side="left", padx=2)
        ttk.Label(bar, text="Post %").pack(side="left", padx=(8, 2))
        ttk.Spinbox(bar, textvariable=self.post, from_=0, to=100, width=4).pack(side="left")
        ttk.Checkbutton(bar, text="Test pattern (drives pins!)", variable=self.pattern).pack(side="left", padx=10)
        self.btn = ttk.Button(bar, text="Capture", command=self.on_capture)
        self.btn.pack(side="left", padx=4)
        ttk.Button(bar, text="Export...", command=self.on_export).pack(side="left")
        ttk.Button(bar, text="Fit", command=self.fit).pack(side="left", padx=4)

        self.status = tk.StringVar(value="connecting...")
        ttk.Label(self, textvariable=self.status, padding=(8, 2)).pack(side="bottom", fill="x")
        self.canvas = tk.Canvas(self, bg=BG, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        c = self.canvas
        c.bind("<Configure>", lambda _e: self.redraw())
        c.bind("<MouseWheel>", self.on_wheel)
        c.bind("<Button-4>", lambda e: self.on_wheel(e, 120))
        c.bind("<Button-5>", lambda e: self.on_wheel(e, -120))
        c.bind("<ButtonPress-1>", self.on_press)
        c.bind("<B1-Motion>", self.on_drag)
        c.bind("<Button-3>", lambda e: self.set_cursor(1, e.x))
        c.bind("<Double-Button-1>", lambda e: self.set_cursor(0, e.x))
        self._drag_x = 0

    # -- device ------------------------------------------------------------------
    def _connect(self) -> None:
        try:
            self.sump = Sump(self.port or find_port())
            i = self.sump.info
            self.status.set(f"{i.name} fw {i.firmware} on {self.sump.port} - "
                            f"{i.sample_memory} samples, up to {i.max_rate_hz / 1e6:.1f} MS/s")
        except (SumpError, OSError) as exc:
            self.status.set(f"not connected: {exc}")

    def on_capture(self) -> None:
        if self.busy:
            if self.sump:
                self.sump.abort()
            return
        if self.sump is None:
            self._connect()
            if self.sump is None:
                return
        if self.pattern.get() and not messagebox.askokcancel(
                "Test pattern", "PB0..PB7 will be driven as outputs. Disconnect your target first."):
            return
        try:
            samples = int(self.size.get())
            samples -= samples % 4
            trig = None
            if self.trig_kind.get() != "none":
                trig = parse_trigger(f"{self.trig_ch.get()}:{self.trig_kind.get()}")
            post = max(4, (samples * int(self.post.get()) // 100) // 4 * 4)
            kw = dict(rate_hz=parse_rate(self.rate.get()), samples=samples, post_trigger=post,
                      trigger=trig, test_pattern=self.pattern.get(), timeout=3600.0)
        except (ValueError, argparse.ArgumentTypeError) as exc:
            messagebox.showerror("Invalid setting", str(exc))
            return
        self.busy = True
        self.btn.configure(text="Stop")
        self.status.set("armed, waiting for trigger..." if trig else "capturing...")
        sump = self.sump
        assert sump is not None

        def work() -> None:
            try:
                self.events.put(("done", sump.capture(**kw)))
            except (SumpError, OSError, ValueError) as exc:
                self.events.put(("error", str(exc)))

        threading.Thread(target=work, daemon=True).start()

    def _poll_events(self) -> None:
        try:
            while True:
                kind, payload = self.events.get_nowait()
                self.busy = False
                self.btn.configure(text="Capture")
                if kind == "done":
                    self.show(payload)
                elif payload != "cancelled":
                    self.status.set(f"error: {payload}")
                else:
                    self.status.set("stopped")
        except queue.Empty:
            pass
        self.after(100, self._poll_events)

    # -- data ----------------------------------------------------------------------
    def show(self, cap: Capture) -> None:
        self.cap = cap
        s = cap.samples
        self.edges = [[] for _ in range(8)]
        self.first = [(s[0] >> ch) & 1 for ch in range(8)]
        for i in range(1, len(s)):
            diff = s[i] ^ s[i - 1]
            if diff:
                for ch in range(8):
                    if (diff >> ch) & 1:
                        self.edges[ch].append(i)
        self.cursors = [None, None]
        self.fit()
        self.status.set(f"{len(s)} samples at {cap.rate_hz / 1e6:.4f} MS/s "
                        f"(host asked {cap.requested_rate_hz / 1e6:.4f}) - wheel: zoom, drag: pan, "
                        f"double-click / right-click: cursors")

    def on_export(self) -> None:
        if not self.cap:
            return
        path = filedialog.asksaveasfilename(defaultextension=".vcd",
                                            filetypes=[("VCD", "*.vcd"), ("CSV", "*.csv")])
        if path:
            with open(path, "w", encoding="ascii") as f:
                (write_csv if path.lower().endswith(".csv") else write_vcd)(self.cap, f)

    # -- view ----------------------------------------------------------------------
    def fit(self) -> None:
        n = len(self.cap.samples) if self.cap else 1
        self.t0, self.span = 0.0, float(n)
        self.redraw()

    def _x_to_sample(self, x: float) -> float:
        w = max(1, self.canvas.winfo_width() - LABEL_W)
        return self.t0 + (x - LABEL_W) / w * self.span

    def _sample_to_x(self, s: float) -> float:
        w = max(1, self.canvas.winfo_width() - LABEL_W)
        return LABEL_W + (s - self.t0) / self.span * w

    def on_wheel(self, e: tk.Event, delta: Optional[int] = None) -> None:
        if not self.cap:
            return
        d = delta if delta is not None else e.delta
        anchor = self._x_to_sample(e.x)
        factor = 0.8 if d > 0 else 1.25
        n = len(self.cap.samples)
        self.span = min(float(n), max(8.0, self.span * factor))
        w = max(1, self.canvas.winfo_width() - LABEL_W)
        self.t0 = anchor - (e.x - LABEL_W) / w * self.span
        self._clamp()
        self.redraw()

    def on_press(self, e: tk.Event) -> None:
        self._drag_x = e.x

    def on_drag(self, e: tk.Event) -> None:
        if not self.cap:
            return
        w = max(1, self.canvas.winfo_width() - LABEL_W)
        self.t0 -= (e.x - self._drag_x) / w * self.span
        self._drag_x = e.x
        self._clamp()
        self.redraw()

    def _clamp(self) -> None:
        n = len(self.cap.samples) if self.cap else 1
        self.t0 = max(-0.1 * self.span, min(self.t0, n - 0.9 * self.span))

    def set_cursor(self, idx: int, x: float) -> None:
        if self.cap:
            self.cursors[idx] = self._x_to_sample(x)
            self.redraw()

    # -- drawing -------------------------------------------------------------------
    def redraw(self) -> None:
        c = self.canvas
        c.delete("all")
        width = c.winfo_width()
        if not self.cap:
            c.create_text(width // 2, 80, fill=FG, text="Press Capture")
            return
        rate = self.cap.rate_hz
        # Ruler: pick a "nice" tick step so ticks are ~120 px apart.
        per_px = self.span / max(1, width - LABEL_W) / rate
        raw = per_px * 120
        step = next((m * 10.0 ** e for e in range(-9, 3) for m in (1, 2, 5) if m * 10.0 ** e >= raw), 1.0)
        t = (int(self.t0 / rate / step) - 1) * step
        while t < (self.t0 + self.span) / rate:
            x = self._sample_to_x(t * rate)
            if x >= LABEL_W:
                c.create_line(x, RULER_H, x, RULER_H + 8 * ROW_H, fill=GRID)
                c.create_text(x + 3, 4, anchor="nw", fill=FG, text=fmt_time(t), font=("Segoe UI", 8))
            t += step
        for ch in range(8):
            y0 = RULER_H + ch * ROW_H
            c.create_text(8, y0 + ROW_H / 2, anchor="w", fill=COLORS[ch], text=f"CH{ch}", font=("Segoe UI", 9, "bold"))
            self._draw_channel(ch, y0 + 6, y0 + ROW_H - 6, COLORS[ch], width)
        if self.cap.trigger_index:
            x = self._sample_to_x(self.cap.trigger_index)
            c.create_line(x, RULER_H, x, RULER_H + 8 * ROW_H, fill="#ff5d5d", dash=(4, 3))
        labels = []
        for i, cur in enumerate(self.cursors):
            if cur is not None:
                x = self._sample_to_x(cur)
                c.create_line(x, RULER_H, x, RULER_H + 8 * ROW_H, fill="#ffffff")
                c.create_text(x + 3, RULER_H + 8 * ROW_H + 2, anchor="nw", fill=FG, text="AB"[i])
                labels.append(cur / rate)
        if len(labels) == 2:
            dt = abs(labels[1] - labels[0])
            freq = f", {1 / dt / 1e3:.3f} kHz" if dt else ""
            c.create_text(width - 8, RULER_H + 8 * ROW_H + 2, anchor="ne", fill=FG,
                          text=f"A-B: {fmt_time(dt)}{freq}")

    def _draw_channel(self, ch: int, y_hi: float, y_lo: float, color: str, width: int) -> None:
        edges = self.edges[ch]
        lvl0 = self.first[ch]
        a, b = max(0, int(self.t0)), min(len(self.cap.samples), int(self.t0 + self.span) + 2)

        def level(sample: int) -> int:     # level of the sample at index `sample`
            return lvl0 ^ (bisect.bisect_right(edges, sample) & 1)

        x_left = max(LABEL_W, self._sample_to_x(a))
        x_right = min(width, self._sample_to_x(b))
        cur = level(a)
        pts: List[float] = [x_left, y_hi if cur else y_lo]
        if self.span / max(1, width - LABEL_W) > 2.0:
            # Dense: one pixel column at a time; any edge in the column becomes a full-height bar.
            for px in range(int(x_left), int(x_right)):
                s0 = int(self._x_to_sample(px))
                s1 = int(self._x_to_sample(px + 1))
                n = bisect.bisect_right(edges, s1 - 1) - bisect.bisect_right(edges, s0 - 1)
                if n:
                    pts += [px, y_hi if cur else y_lo, px, y_lo if cur else y_hi]
                    cur = level(s1)
                    pts += [px, y_hi if cur else y_lo]
            pts += [x_right, y_hi if cur else y_lo]
        else:
            for e in edges[bisect.bisect_left(edges, a):bisect.bisect_right(edges, b)]:
                x = self._sample_to_x(e)
                pts += [x, y_hi if cur else y_lo]
                cur ^= 1
                pts += [x, y_hi if cur else y_lo]
            pts += [x_right, y_hi if cur else y_lo]
        self.canvas.create_line(*pts, fill=color, width=1.5)


def run(port: Optional[str] = None) -> int:
    Viewer(port).mainloop()
    return 0
