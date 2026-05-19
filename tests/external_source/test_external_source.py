"""PCMFlow::setInputSource(OpusDecoder) integration test.

SCAFFOLDING: passes as long as the harness completes.
"""

import re


def test_external_source(dut):
    dut.expect("TEST start", timeout=10)
    match = dut.expect(re.compile(rb"TEST done (\d+)/(\d+)"), timeout=60)
    passed, total = int(match.group(1)), int(match.group(2))
    assert passed == total, f"{total - passed} of {total} assertions failed"
