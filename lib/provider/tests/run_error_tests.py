"""Run deterministic provider tests against a local HTTP/SSE fixture."""
import contextlib
import http.server
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

HTTP_BODY = b'{"error":{"code":"rate_limit","message":"slow down"}}'
USAGE = {"usage": {"completion_tokens": 2, "prompt_tokens": 3, "total_tokens": 5}}


def event(payload):
    return ("data: " + json.dumps(payload, separators=(",", ":")) + "\n\n").encode()


class Fixture(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        path = self.path
        if path in {"/http", "/partial"}:
            payload = HTTP_BODY
            status = 429
        elif path == "/invalid_json":
            status, payload = 200, b"data: {invalid\n\n"
        elif path == "/invalid_usage":
            status, payload = 200, event({"usage": {
                "completion_tokens": 2, "prompt_tokens": 3, "total_tokens": "bad"}})
        elif path == "/error_event":
            status, payload = 200, event({"error": {
                "message": "quota exceeded", "code": "quota",
                "details": {"path": "/provider/path", "values": [None, 42]}}})
        elif path == "/unified_error":
            status, payload = 200, event({"error": {
                "source": "remote", "operation": "invoke", "type": "system_error",
                "message": "original", "data": [{"code": 5, "path": "/native/path"}],
                "causes": []}})
        else:
            status, payload = 200, b""
            if path != "/empty":
                payload += event({"choices": [{"delta": {"content": "summary text"}}]})
            if path != "/without_usage":
                payload += event(USAGE)
            payload += b"data: [DONE]\n\n"
            if request.get("stream_options", {}).get("include_usage") is not True:
                self.send_error(400, "Expected schema-configured include_usage")
                return
        self.send_response(status)
        self.send_header("Content-Type", "text/event-stream" if status == 200 else "application/json")
        self.send_header("Content-Length", str(len(payload) + (17 if path == "/partial" else 0)))
        self.send_header("Connection", "close")
        self.end_headers()
        with contextlib.suppress(BrokenPipeError, ConnectionResetError):
            self.wfile.write(payload)
        self.close_connection = True


def check_codegen(executable):
    fields = {"source", "operation", "type", "message", "data", "causes"}
    target_dir = Path(executable).resolve().parent
    with tempfile.TemporaryDirectory(prefix="provider-errors-", dir=target_dir) as temp:
        folder = Path(temp)
        assert folder.resolve().is_relative_to(target_dir)
        for content in (None, "", "{invalid", '{"providers":[{}]}'):
            schema = folder / "schema.json"
            if content is not None:
                schema.write_text(content, encoding="utf-8")
            result = subprocess.run(
                [executable, str(schema), str(folder / "generated.h"), str(folder / "generated.cpp")],
                capture_output=True, text=True, timeout=30)
            error = json.loads(result.stderr)
            assert result.returncode != 0 and set(error) == fields, result.stderr
            assert isinstance(error["data"], list) and isinstance(error["causes"], list)
            if content is None:
                assert error["operation"] == "read_schema"
                assert error["data"][0]["path"] == str(schema)
                assert error["data"][0]["code"] != 0
            if content in ("", "{invalid"):
                assert error["data"][-1]["code"] == 101
                assert "byte" in error["data"][-1]
                assert error["data"][0]["contents"] == content
        valid_schema = Path(__file__).resolve().parents[1] / "src/request/provider_types.json"
        blocked = subprocess.run(
            [executable, str(valid_schema), str(folder), str(folder / "generated.cpp")],
            capture_output=True, text=True, timeout=30)
        error = json.loads(blocked.stderr)
        assert blocked.returncode != 0 and set(error) == fields
        assert error["operation"] == "write_generated_file"
        assert error["data"][0]["path"] == str(folder)
        assert error["data"][0]["code"] != 0
        print("Code generator native file, JSON and schema error tests passed")


def main():
    check_codegen(sys.argv[2])
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Fixture) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}"
            result = subprocess.run([sys.argv[1], url], timeout=60)
        finally:
            server.shutdown()
            worker.join()
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
