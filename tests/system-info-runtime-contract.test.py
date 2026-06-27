#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SystemInfoRuntimeContractTest(unittest.TestCase):
    def test_backend_treats_consumed_bytes_as_interval_activity(self):
        source = (ROOT / "GUI/backend/src/project_orchestration.cpp").read_text()

        self.assertIn("const bool has_pending_data = avail > 0 || produced > 0;", source)
        self.assertIn("if (consumed > 0 || (!has_pending_data && !expecting_system_progress))", source)
        self.assertIn("now - progress.last_progress_ms", source)
        self.assertNotIn("progress.consumed_bytes != consumed", source)
        self.assertRegex(source, r"no_consumption\s*=\s*\(has_pending_data\s*\|\|\s*expecting_system_progress\)")
        self.assertIn("queued_bytes > 0 && queue_age_ms >= 100", source)
        self.assertIn("if (!status.stalled) return system_status;", source)

    def test_playback_stream_does_not_reset_system_info_blockage_window(self):
        source = (ROOT / "GUI/backend/src/project_orchestration.cpp").read_text()

        self.assertEqual(source.count("system_buffer_progress_.clear();"), 2)

    def test_frontend_marks_only_backend_reported_blocked_edge(self):
        frontend = (ROOT / "GUI/frontend/index.html").read_text()

        self.assertIn("blocked_edge_key", frontend)
        self.assertIn("isPlaybackBlockedEdgeV104", frontend)
        self.assertIn("playback-blocked-v104", frontend)

    def test_algorithm_cost_requests_all_dsp_nodes_before_pipeline_filtering(self):
        frontend = (ROOT / "GUI/frontend/index.html").read_text()

        self.assertIn("function algorithmCostDspNodesV50()", frontend)
        self.assertIn("return [...g.nodes].filter(n => !isDebugFileIoNode(n));", frontend)
        self.assertIn("const requestedNodes = algorithmCostDspNodesV50();", frontend)


if __name__ == "__main__":
    unittest.main()
