"""Offline tests for benchmark acceptance; never touches Bluetooth or a probe."""
import copy
import subprocess
import sys
import unittest
from pathlib import Path

from run import assess_case


class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.events = [dict(event='phy_status', operation=operation, status=0,
                            tx_phy=1, rx_phy=1, pending=False)
                       for operation in ('wait_phy', 'verify_phy')]
        self.summary = dict(direction='up', exit_code=0, reason='perf_complete',
                            failures=0, verified_bytes=2440, queued_bytes=2440,
                            transfer_seconds=30, backpressure_events=1,
                            receiver_stats=dict(errors=0, bytes=2440, packets=10,
                                                dle_tx_octets=251, dle_rx_octets=251,
                                                dle_request_attempted=True, dle_request_rc=0))

    def assess(self, summary=None, events=None):
        return assess_case(summary or self.summary, self.events if events is None else events,
                           0, 30, 244, True)

    def test_complete_reference_case(self):
        self.assertTrue(self.assess()['reference_standard_met'])

    def test_corrupt_or_missing_data_never_passes(self):
        for key, value in [('failures', 1), ('verified_bytes', 2439),
                           ('queued_bytes', 2400), ('transfer_seconds', 1)]:
            summary = copy.deepcopy(self.summary)
            summary[key] = value
            self.assertFalse(self.assess(summary)['passed'], key)
            self.assertFalse(self.assess(summary)['reference_standard_met'], key)

    def test_legacy_phy_is_not_reference_pass(self):
        self.events[0]['status'] = 1
        self.assertTrue(self.assess()['data_integrity_passed'])
        self.assertFalse(self.assess()['reference_standard_met'])
        self.assertFalse(self.assess(events=self.events[:1])['radio_parameters_verified'])

    def test_no_backpressure_or_failed_dle_request(self):
        self.summary['backpressure_events'] = 0
        self.assertFalse(self.assess()['passed'])
        self.summary['backpressure_events'] = 1
        self.summary['receiver_stats']['dle_request_rc'] = 513
        self.assertFalse(self.assess()['passed'])

    def test_download_must_finish_production(self):
        self.summary['direction'] = 'down'
        self.summary['receiver_stats']['producer_blocked_cycles'] = 1
        self.assertFalse(self.assess()['passed'])
        self.summary['receiver_stats']['producer_done'] = True
        self.assertTrue(self.assess()['passed'])

    def test_invalid_cli_fails_offline(self):
        runner = Path(__file__).with_name('run.py')
        for option, value in [('--duration', 'nan'), ('--duration', 'inf'),
                              ('--duration', '3601'), ('--repeats', '101')]:
            result = subprocess.run([sys.executable, str(runner), '--mode', 'nimble',
                                     '--name', 'unused', option, value], capture_output=True)
            self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
