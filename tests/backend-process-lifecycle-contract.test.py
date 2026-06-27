#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BackendProcessLifecycleContractTest(unittest.TestCase):
    def test_http_descriptors_do_not_leak_into_validation_exec(self):
        source = (ROOT / "GUI/backend/src/http_server.cpp").read_text()

        self.assertIn("FD_CLOEXEC", source)
        self.assertIn("setCloseOnExec(server_fd)", source)
        self.assertIn("setCloseOnExec(fd)", source)

    def test_signal_shutdown_reaches_orchestrator_cleanup(self):
        source = (ROOT / "GUI/backend/src/http_server.cpp").read_text()
        header = (ROOT / "GUI/backend/include/audio_studio.hpp").read_text()
        orchestration = (ROOT / "GUI/backend/src/project_orchestration.cpp").read_text()

        self.assertIn("SIGINT", source)
        self.assertIn("SIGTERM", source)
        self.assertIn("~BuildOrchestrator", header)
        self.assertIn("BuildOrchestrator::~BuildOrchestrator", orchestration)
        self.assertIn("validation_runner_->stop", orchestration)

    def test_disconnected_socket_cannot_terminate_backend_with_sigpipe(self):
        http_source = (ROOT / "GUI/backend/src/http_server.cpp").read_text()
        orchestration = (ROOT / "GUI/backend/src/project_orchestration.cpp").read_text()

        self.assertIn("MSG_NOSIGNAL", http_source)
        self.assertIn("MSG_NOSIGNAL", orchestration)


if __name__ == "__main__":
    unittest.main()
