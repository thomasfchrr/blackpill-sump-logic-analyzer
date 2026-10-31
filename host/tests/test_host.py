"""Host tests. Pure-Python tests always run; the hardware test needs a connected analyzer."""

import io

import pytest

from blackpill_sump import Capture, Sump, actual_rate, find_port
from blackpill_sump.cli import parse_rate, parse_trigger
from blackpill_sump.client import divider_for_rate, timer_divider
from blackpill_sump.export import write_csv, write_vcd


def test_parse_rate():
    assert parse_rate("500k") == 500e3
    assert parse_rate("19.2M") == 19.2e6
    assert parse_rate("1e6") == 1e6


def test_parse_trigger():
    assert parse_trigger(None) is None
    assert parse_trigger("3:rise") == [(8, 0), (8, 8)]
    assert parse_trigger("0:low") == [(1, 0)]
    with pytest.raises(Exception):
        parse_trigger("3:bogus")


@pytest.mark.parametrize("rate,expected", [(1e6, 1e6), (2e6, 2e6), (4e6, 4e6), (100e3, 100e3)])
def test_exact_rates(rate, expected):
    # Multiples of 4 MHz / k map exactly onto the 96 MHz timer.
    assert actual_rate(divider_for_rate(rate)) == expected


def test_rate_is_clamped_to_dma_limit():
    assert actual_rate(divider_for_rate(100e6)) == pytest.approx(19.2e6)
    assert timer_divider(0) == 5


def test_vcd_and_csv():
    cap = Capture(samples=bytes([0, 1, 1, 3, 2]), rate_hz=1e6)
    vcd = io.StringIO()
    write_vcd(cap, vcd)
    text = vcd.getvalue()
    assert "$var wire 1 ! CH0 $end" in text
    assert "#1000000\n1!" in text            # CH0 rises at sample 1 (1 us)
    assert "#3000000" in text and "1\"" in text  # CH1 rises at sample 3
    csv = io.StringIO()
    write_csv(cap, csv)
    assert csv.getvalue().splitlines()[2].startswith("1e-06,1,0")


def test_capture_edges():
    cap = Capture(samples=bytes([0, 0, 1, 1, 0]), rate_hz=1.0)
    assert cap.edges(0) == [2, 4]


@pytest.mark.skipif(find_port() is None, reason="analyzer not connected")
def test_hardware_pattern_and_trigger():
    with Sump() as s:
        cap = s.capture(rate_hz=19.2e6, samples=8192, post_trigger=4096,
                        trigger=[(0x08, 0x00), (0x08, 0x08)], test_pattern=True)
        i = cap.trigger_index
        assert [(b >> 3) & 1 for b in cap.samples[i - 1:i + 1]] == [0, 1]   # rising edge on PB3
        spacing = [b - a for a, b in zip(cap.edges(3), cap.edges(3)[1:])]
        assert set(spacing) <= {95, 96, 97}
