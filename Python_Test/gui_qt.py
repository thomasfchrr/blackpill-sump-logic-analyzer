#!/usr/bin/env python3
"""\
File: gui_qt.py
Description: Qt6 GUI for STM32 BlackPill USB CDC protocol.
Author: Thomas Faucherre
Created: 2026-02-10
"""

from __future__ import annotations

import struct
import sys
from typing import Optional

from main import (
    CMD_HELLO_REQ,
    CMD_LED_REQ,
    CMD_LOGIC_SNAPSHOT_REQ,
    CMD_PING_REQ,
    CMD_RESET_REQ,
    CMD_STATS_REQ,
    RSP_HELLO,
    RSP_LED,
    RSP_LOGIC_SNAPSHOT,
    RSP_PING,
    RSP_RESET_ACK,
    RSP_STATS,
    UsbProtoClient,
    parse_hello,
    parse_logic_snapshot,
    parse_stats,
)

try:
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required: pip install pyserial") from exc

try:
    from PySide6.QtCore import QDateTime, QObject, QThread, QTimer, Qt, Signal, Slot
    from PySide6.QtWidgets import (
        QApplication,
        QComboBox,
        QFormLayout,
        QGridLayout,
        QGroupBox,
        QHBoxLayout,
        QLabel,
        QLineEdit,
        QMainWindow,
        QPlainTextEdit,
        QPushButton,
        QSpinBox,
        QSplitter,
        QVBoxLayout,
        QWidget,
    )
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PySide6 is required: pip install pyside6") from exc


class UsbWorker(QObject):
    connected = Signal(str)
    disconnected = Signal()
    hello_ready = Signal(dict)
    stats_ready = Signal(dict)
    logic_ready = Signal(dict)
    log = Signal(str)
    error = Signal(str)

    def __init__(self) -> None:
        super().__init__()
        self.client: Optional[UsbProtoClient] = None
        self.monitor_timer = QTimer(self)
        self.monitor_timer.timeout.connect(self._monitor_tick)
        self.monitor_samples = 128

    def _ensure_client(self) -> UsbProtoClient:
        if self.client is None:
            raise RuntimeError("Not connected to STM32 USB CDC")
        return self.client

    @Slot(str, int, int)
    def connect_device(self, port: str, vid: int, pid: int) -> None:
        try:
            if self.client is not None:
                self.client.close()
                self.client = None

            client = UsbProtoClient(port=(port or None), vid=vid, pid=pid, timeout_s=0.0)
            client.connect()
            self.client = client
            self.log.emit(f"Connected to {client.port}")
            self.connected.emit(client.port or "")
        except Exception as exc:
            self.error.emit(str(exc))

    @Slot()
    def disconnect_device(self) -> None:
        self.monitor_timer.stop()
        if self.client is not None:
            try:
                self.client.close()
            except Exception:
                pass
            self.client = None
        self.log.emit("Disconnected")
        self.disconnected.emit()

    @Slot()
    def hello(self) -> None:
        try:
            client = self._ensure_client()
            _seq, rsp = client.request(CMD_HELLO_REQ, RSP_HELLO, timeout_s=1.0, retries=1)
            info = parse_hello(rsp.payload)
            self.hello_ready.emit(info)
            self.log.emit(
                f"HELLO fw=0x{info['fw']:08X} sysclk={info['sysclk']} caps=0x{info['caps']:08X} "
                f"rx_ring={info['rx_ring']} tx_depth={info['tx_depth']}"
            )
        except Exception as exc:
            self.error.emit(str(exc))

    @Slot(str)
    def ping(self, text: str) -> None:
        try:
            client = self._ensure_client()
            payload = text.encode("utf-8")
            seq, rsp = client.request(CMD_PING_REQ, RSP_PING, payload, timeout_s=1.0, retries=1)
            self.log.emit(f"PONG seq={seq}: {rsp.payload.decode(errors='replace')}")
        except Exception as exc:
            self.error.emit(str(exc))

    @Slot()
    def stats(self) -> None:
        try:
            client = self._ensure_client()
            _seq, rsp = client.request(CMD_STATS_REQ, RSP_STATS, timeout_s=1.0, retries=1)
            info = parse_stats(rsp.payload)
            self.stats_ready.emit(info)
            self.log.emit(
                "STATS rx_ok={rx_ok} tx_overflow={tx_queue_overflow} "
                "dma_ok={dma_copy_ok} usb_cfg={usb_configured}".format(**info)
            )
        except Exception as exc:
            self.error.emit(str(exc))

    @Slot(int)
    def set_led_mode(self, mode: int) -> None:
        try:
            client = self._ensure_client()
            _seq, rsp = client.request(CMD_LED_REQ, RSP_LED, bytes([mode & 0xFF]), timeout_s=1.0, retries=1)
            led_mode = rsp.payload[0] if rsp.payload else 255
            self.log.emit(f"LED mode set to {led_mode}")
        except Exception as exc:
            self.error.emit(str(exc))

    @Slot(int)
    def request_reset(self, reason: int) -> None:
        try:
            client = self._ensure_client()
            payload = struct.pack("<H", reason & 0xFFFF)
            _seq, rsp = client.request(CMD_RESET_REQ, RSP_RESET_ACK, payload, timeout_s=0.8, retries=0)
            if len(rsp.payload) >= 8:
                ack_reason, marker, tick = struct.unpack("<HHI", rsp.payload[:8])
                self.log.emit(
                    f"RESET_ACK reason=0x{ack_reason:04X} marker=0x{marker:04X} tick={tick}. "
                    "Board will reboot."
                )
            else:
                self.log.emit("RESET_ACK received (short payload). Board will reboot.")
        except TimeoutError:
            self.log.emit("Reset requested (no ACK before reboot).")
        except Exception as exc:
            self.error.emit(str(exc))

    @Slot(int)
    def logic_snapshot(self, sample_count: int) -> None:
        try:
            client = self._ensure_client()
            clamped = max(1, min(sample_count, 500))
            payload = struct.pack("<H", clamped)
            _seq, rsp = client.request(
                CMD_LOGIC_SNAPSHOT_REQ,
                RSP_LOGIC_SNAPSHOT,
                payload,
                timeout_s=1.0,
                retries=1,
            )
            info = parse_logic_snapshot(rsp.payload)
            self.logic_ready.emit(info)
            self.log.emit(
                "LOGIC samples={sample_count} period_us={sample_period_us} tick={tick} probes={probes}".format(
                    **info
                )
            )
        except Exception as exc:
            self.error.emit(str(exc))

    @Slot(int, int)
    def start_monitor(self, interval_ms: int, sample_count: int) -> None:
        if self.client is None:
            self.error.emit("Connect first before starting logic monitor")
            return
        self.monitor_samples = max(1, min(sample_count, 500))
        self.monitor_timer.setInterval(max(40, interval_ms))
        self.monitor_timer.start()
        self.log.emit(
            f"Logic monitor started: interval={self.monitor_timer.interval()} ms samples={self.monitor_samples}"
        )

    @Slot()
    def stop_monitor(self) -> None:
        if self.monitor_timer.isActive():
            self.monitor_timer.stop()
            self.log.emit("Logic monitor stopped")

    @Slot()
    def _monitor_tick(self) -> None:
        self.logic_snapshot(self.monitor_samples)


class MainWindow(QMainWindow):
    sig_connect = Signal(str, int, int)
    sig_disconnect = Signal()
    sig_hello = Signal()
    sig_ping = Signal(str)
    sig_stats = Signal()
    sig_led = Signal(int)
    sig_reset = Signal(int)
    sig_logic_snapshot = Signal(int)
    sig_monitor_start = Signal(int, int)
    sig_monitor_stop = Signal()

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("STM32 USB CDC Control Panel (Qt6)")
        self.resize(1200, 760)

        self._build_ui()
        self._setup_worker()
        self._connect_signals()
        self.refresh_ports()
        self._set_connected(False)

    def _build_ui(self) -> None:
        root = QWidget(self)
        self.setCentralWidget(root)
        main_layout = QHBoxLayout(root)

        left_col = QVBoxLayout()
        main_layout.addLayout(left_col, 0)

        conn_group = QGroupBox("Connection")
        conn_form = QFormLayout(conn_group)
        self.port_combo = QComboBox()
        self.port_combo.setEditable(True)
        self.vid_spin = QSpinBox()
        self.vid_spin.setRange(0, 0xFFFF)
        self.vid_spin.setDisplayIntegerBase(16)
        self.vid_spin.setPrefix("0x")
        self.vid_spin.setValue(0x0483)
        self.pid_spin = QSpinBox()
        self.pid_spin.setRange(0, 0xFFFF)
        self.pid_spin.setDisplayIntegerBase(16)
        self.pid_spin.setPrefix("0x")
        self.pid_spin.setValue(0x5740)
        conn_buttons = QHBoxLayout()
        self.refresh_btn = QPushButton("Refresh")
        self.connect_btn = QPushButton("Connect")
        self.disconnect_btn = QPushButton("Disconnect")
        conn_buttons.addWidget(self.refresh_btn)
        conn_buttons.addWidget(self.connect_btn)
        conn_buttons.addWidget(self.disconnect_btn)
        conn_form.addRow("Port", self.port_combo)
        conn_form.addRow("VID", self.vid_spin)
        conn_form.addRow("PID", self.pid_spin)
        conn_form.addRow(conn_buttons)
        left_col.addWidget(conn_group)

        ctrl_group = QGroupBox("Device Actions")
        ctrl_grid = QGridLayout(ctrl_group)
        self.hello_btn = QPushButton("HELLO")
        self.ping_btn = QPushButton("Ping")
        self.stats_btn = QPushButton("Stats")
        self.reset_btn = QPushButton("Reset STM32")
        self.ping_edit = QLineEdit("PING_GUI")
        self.reset_reason_spin = QSpinBox()
        self.reset_reason_spin.setRange(0, 0xFFFF)
        self.reset_reason_spin.setDisplayIntegerBase(16)
        self.reset_reason_spin.setPrefix("0x")
        self.led_auto_btn = QPushButton("LED Auto")
        self.led_off_btn = QPushButton("LED Off")
        self.led_on_btn = QPushButton("LED On")

        ctrl_grid.addWidget(self.hello_btn, 0, 0)
        ctrl_grid.addWidget(self.stats_btn, 0, 1)
        ctrl_grid.addWidget(self.ping_edit, 1, 0)
        ctrl_grid.addWidget(self.ping_btn, 1, 1)
        ctrl_grid.addWidget(self.reset_reason_spin, 2, 0)
        ctrl_grid.addWidget(self.reset_btn, 2, 1)
        ctrl_grid.addWidget(self.led_auto_btn, 3, 0)
        ctrl_grid.addWidget(self.led_off_btn, 3, 1)
        ctrl_grid.addWidget(self.led_on_btn, 3, 2)
        left_col.addWidget(ctrl_group)

        logic_group = QGroupBox("Logic Snapshot")
        logic_form = QFormLayout(logic_group)
        self.samples_spin = QSpinBox()
        self.samples_spin.setRange(1, 500)
        self.samples_spin.setValue(160)
        self.monitor_interval_spin = QSpinBox()
        self.monitor_interval_spin.setRange(40, 5000)
        self.monitor_interval_spin.setSuffix(" ms")
        self.monitor_interval_spin.setValue(150)
        logic_buttons_1 = QHBoxLayout()
        self.snapshot_btn = QPushButton("Snapshot")
        self.monitor_start_btn = QPushButton("Start Monitor")
        self.monitor_stop_btn = QPushButton("Stop Monitor")
        logic_buttons_1.addWidget(self.snapshot_btn)
        logic_buttons_1.addWidget(self.monitor_start_btn)
        logic_buttons_1.addWidget(self.monitor_stop_btn)
        logic_form.addRow("Samples", self.samples_spin)
        logic_form.addRow("Monitor", self.monitor_interval_spin)
        logic_form.addRow(logic_buttons_1)
        left_col.addWidget(logic_group)

        self.hello_label = QLabel("FW: -, SYSCLK: -, CAPS: -")
        self.stats_label = QLabel("RX_OK: -, TX_OVF: -, DMA_OK: -, USB_CFG: -")
        left_col.addWidget(self.hello_label)
        left_col.addWidget(self.stats_label)
        left_col.addStretch(1)

        right_split = QSplitter()
        right_split.setOrientation(Qt.Vertical)

        self.terminal = QPlainTextEdit()
        self.terminal.setReadOnly(True)
        self.terminal.setPlaceholderText("Protocol terminal output")
        self.logic_view = QPlainTextEdit()
        self.logic_view.setReadOnly(True)
        self.logic_view.setPlaceholderText("Logic samples preview")
        right_split.addWidget(self.terminal)
        right_split.addWidget(self.logic_view)
        right_split.setSizes([520, 220])

        main_layout.addWidget(right_split, 1)

    def _setup_worker(self) -> None:
        self.thread = QThread(self)
        self.worker = UsbWorker()
        self.worker.moveToThread(self.thread)
        self.thread.start()

    def _connect_signals(self) -> None:
        self.sig_connect.connect(self.worker.connect_device)
        self.sig_disconnect.connect(self.worker.disconnect_device)
        self.sig_hello.connect(self.worker.hello)
        self.sig_ping.connect(self.worker.ping)
        self.sig_stats.connect(self.worker.stats)
        self.sig_led.connect(self.worker.set_led_mode)
        self.sig_reset.connect(self.worker.request_reset)
        self.sig_logic_snapshot.connect(self.worker.logic_snapshot)
        self.sig_monitor_start.connect(self.worker.start_monitor)
        self.sig_monitor_stop.connect(self.worker.stop_monitor)

        self.worker.log.connect(self.append_log)
        self.worker.error.connect(self.on_error)
        self.worker.connected.connect(self.on_connected)
        self.worker.disconnected.connect(self.on_disconnected)
        self.worker.hello_ready.connect(self.on_hello_ready)
        self.worker.stats_ready.connect(self.on_stats_ready)
        self.worker.logic_ready.connect(self.on_logic_ready)

        self.refresh_btn.clicked.connect(self.refresh_ports)
        self.connect_btn.clicked.connect(self.connect_clicked)
        self.disconnect_btn.clicked.connect(self.disconnect_clicked)
        self.hello_btn.clicked.connect(lambda: self.sig_hello.emit())
        self.ping_btn.clicked.connect(lambda: self.sig_ping.emit(self.ping_edit.text().strip() or "PING_GUI"))
        self.stats_btn.clicked.connect(lambda: self.sig_stats.emit())
        self.led_auto_btn.clicked.connect(lambda: self.sig_led.emit(0))
        self.led_off_btn.clicked.connect(lambda: self.sig_led.emit(1))
        self.led_on_btn.clicked.connect(lambda: self.sig_led.emit(2))
        self.reset_btn.clicked.connect(lambda: self.sig_reset.emit(self.reset_reason_spin.value()))
        self.snapshot_btn.clicked.connect(lambda: self.sig_logic_snapshot.emit(self.samples_spin.value()))
        self.monitor_start_btn.clicked.connect(
            lambda: self.sig_monitor_start.emit(self.monitor_interval_spin.value(), self.samples_spin.value())
        )
        self.monitor_stop_btn.clicked.connect(lambda: self.sig_monitor_stop.emit())

    def _set_connected(self, connected: bool) -> None:
        self.connect_btn.setEnabled(not connected)
        self.disconnect_btn.setEnabled(connected)
        self.hello_btn.setEnabled(connected)
        self.ping_btn.setEnabled(connected)
        self.stats_btn.setEnabled(connected)
        self.led_auto_btn.setEnabled(connected)
        self.led_off_btn.setEnabled(connected)
        self.led_on_btn.setEnabled(connected)
        self.reset_btn.setEnabled(connected)
        self.snapshot_btn.setEnabled(connected)
        self.monitor_start_btn.setEnabled(connected)
        self.monitor_stop_btn.setEnabled(connected)

    @Slot()
    def refresh_ports(self) -> None:
        current = self.port_combo.currentText().strip()
        self.port_combo.clear()
        ports = sorted(list_ports.comports(), key=lambda p: p.device)
        for p in ports:
            self.port_combo.addItem(p.device)
        if current:
            idx = self.port_combo.findText(current)
            if idx >= 0:
                self.port_combo.setCurrentIndex(idx)
            else:
                self.port_combo.setEditText(current)

    @Slot()
    def connect_clicked(self) -> None:
        port = self.port_combo.currentText().strip()
        self.sig_connect.emit(port, self.vid_spin.value(), self.pid_spin.value())

    @Slot()
    def disconnect_clicked(self) -> None:
        self.sig_disconnect.emit()

    @Slot(str)
    def append_log(self, message: str) -> None:
        ts = QDateTime.currentDateTime().toString("HH:mm:ss.zzz")
        self.terminal.appendPlainText(f"[{ts}] {message}")

    @Slot(str)
    def on_error(self, message: str) -> None:
        self.append_log(f"ERROR: {message}")

    @Slot(str)
    def on_connected(self, port: str) -> None:
        self._set_connected(True)
        self.append_log(f"UI connected on {port}")

    @Slot()
    def on_disconnected(self) -> None:
        self._set_connected(False)
        self.append_log("UI disconnected")

    @Slot(dict)
    def on_hello_ready(self, info: dict) -> None:
        self.hello_label.setText(
            f"FW: 0x{info['fw']:08X}, SYSCLK: {info['sysclk']} Hz, CAPS: 0x{info['caps']:08X}"
        )

    @Slot(dict)
    def on_stats_ready(self, info: dict) -> None:
        self.stats_label.setText(
            f"RX_OK: {info['rx_ok']}, TX_OVF: {info['tx_queue_overflow']}, "
            f"DMA_OK: {info['dma_copy_ok']}, USB_CFG: {info['usb_configured']}"
        )

    @Slot(dict)
    def on_logic_ready(self, info: dict) -> None:
        samples: bytes = info["samples"]
        preview = min(64, len(samples))
        lines = [
            (
                f"format={info['format']} probes={info['probes']} "
                f"period_us={info['sample_period_us']} samples={info['sample_count']} tick={info['tick']}"
            )
        ]
        for idx in range(preview):
            val = samples[idx]
            lines.append(f"S{idx:03d}: 0b{val:08b}  0x{val:02X}")
        self.logic_view.setPlainText("\n".join(lines))

    def closeEvent(self, event) -> None:  # type: ignore[override]
        self.sig_monitor_stop.emit()
        self.sig_disconnect.emit()
        self.thread.quit()
        self.thread.wait(1000)
        super().closeEvent(event)


def main() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
