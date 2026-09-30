import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


RUNTIME = Path(sys.argv[1]).resolve()
FIXTURE = Path(sys.argv[2]).resolve()
sys.argv = [sys.argv[0]]


def original_error():
    return {
        "source": "provider",
        "operation": "request",
        "type": "http_error",
        "message": "HTTP request failed with status 429",
        "data": [{"status_code": 429, "body": {"retry": 10}}, None, True, 7, "detail", [1, 2]],
        "causes": [{
            "source": "transport", "operation": "send", "type": "system_error",
            "message": "Connection reset", "data": [{"code": 104, "api": "recv", "path": "socket"}], "causes": []
        }],
    }


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="tool-runtime-test-", dir=RUNTIME.parent)
        cls.path = Path(cls.directory.name)
        cls.definitions = cls.path / "definitions.json"
        cls.definitions.write_text(json.dumps([
            {"type": "function", "function": {"name": "read", "parameters": {
                "type": "object", "properties": {"path": {"type": "string"}},
                "required": ["path"], "additionalProperties": False,
            }}},
            {"type": "function", "function": {"name": "edit_file", "parameters": {"type": "object"}}},
        ]), encoding="utf-8")
        cls.worker = cls.path / "worker.mjs"
        cls.worker.write_text(
            "process.stdin.resume(); process.stdin.on('end', () => {"
            "process.stdout.write(process.env.TOOL_RUNTIME_FIXTURE_RESPONSE || '{}');"
            "process.exitCode = Number(process.env.TOOL_RUNTIME_FIXTURE_EXIT_CODE || '0');"
            "});", encoding="utf-8")
        cls.node = shutil.which("node")
        cls.env = os.environ.copy()
        cls.env.update({
            "HOMEGROWPH_TOOL_DEFINITIONS": str(cls.definitions),
            "HOMEGROWPH_EDIT_FILE": str(FIXTURE),
            "HOMEGROWPH_TOOL_HOST": str(cls.worker),
        })
        if cls.node:
            cls.env["HOMEGROWPH_NODE"] = cls.node

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def invoke(self, tool="edit_file", arguments=None, response=None, raw=None, overrides=None, exit_code=0):
        env = self.env.copy()
        env["TOOL_RUNTIME_FIXTURE_RESPONSE"] = json.dumps(response if response is not None else {"ok": True, "result": "success"})
        env["TOOL_RUNTIME_FIXTURE_EXIT_CODE"] = str(exit_code)
        if overrides:
            env.update(overrides)
        if raw is None:
            if arguments is None:
                arguments = {"path": "file.txt"} if tool == "read" else {}
            raw = json.dumps({"id": "test-call", "type": "function", "function": {
                "name": tool, "arguments": json.dumps(arguments) if not isinstance(arguments, str) else arguments,
            }}).encode()
        process = subprocess.run([str(RUNTIME)], input=raw, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 cwd=self.path, env=env, timeout=15)
        self.assertEqual(process.returncode, 0, process.stderr.decode(errors="replace"))
        outer = json.loads(process.stdout)
        envelope = json.loads(outer["result"]["content"])
        self.assertEqual(outer["tool_call"]["id"], outer["result"]["tool_call_id"])
        self.assertEqual(envelope["call_id"], outer["tool_call"]["id"])
        self.assertEqual(len(envelope["results"]), 1)
        item = envelope["results"][0]
        self.assertIn("value", item)
        self.assertIn("error", item)
        if item["error"] is not None:
            self.assertIsNone(item["value"])
            self.assertFalse(item["ok"])
            self.assert_schema(item["error"])
        else:
            self.assertTrue(item["ok"])
        return item

    def assert_schema(self, error):
        self.assertEqual(set(error), {"source", "operation", "type", "message", "data", "causes"})
        for field in ("source", "operation", "type", "message"):
            self.assertIsInstance(error[field], str)
        self.assertIsInstance(error["data"], list)
        self.assertIsInstance(error["causes"], list)
        for cause in error["causes"]:
            self.assert_schema(cause)

    def worker_required(self):
        if self.node is None:
            self.skipTest("Node.js is not available")

    def test_invalid_json_preserves_parser_diagnostics(self):
        error = self.invoke(raw=b'{"broken":')["error"]
        self.assertEqual(error["operation"], "parse_input")
        self.assertEqual(error["data"][0]["id"], 101)
        self.assertIn("byte", error["data"][0])
        self.assertEqual(error["data"][0]["input"], '{"broken":')

    def test_invalid_utf8_preserves_bytes(self):
        error = self.invoke(raw=b'\xff')["error"]
        self.assertEqual(error["data"][0]["input"], {"encoding": "bytes", "bytes": [255]})

    def test_tool_call_validation(self):
        error = self.invoke(raw=b'17')["error"]
        self.assertEqual(error["type"], "validation_error")
        self.assertEqual(error["data"][0]["tool_call"], 17)

    def test_unknown_tool_retains_name(self):
        error = self.invoke(tool="unregistered")["error"]
        self.assertEqual(error["operation"], "find_tool_definition")
        self.assertEqual(error["data"][0]["tool"], "unregistered")

    def test_invalid_arguments_preserve_json_error(self):
        error = self.invoke(arguments='{"broken":')["error"]
        self.assertEqual(error["operation"], "parse_arguments")
        self.assertEqual(error["data"][0]["id"], 101)
        self.assertEqual(error["data"][0]["path"], "function.arguments")

    def test_independent_validation_errors_are_causes(self):
        error = self.invoke(tool="read", arguments={"extra": 3})["error"]
        self.assertEqual(error["type"], "validation_error")
        self.assertEqual(len(error["causes"]), 2)
        self.assertEqual({cause["data"][0]["path"] for cause in error["causes"]},
                         {"function.arguments.path", "function.arguments.extra"})

    def test_native_error_identity(self):
        error = original_error()
        self.assertEqual(self.invoke(response={"ok": False, "error": error})["error"], error)

    def test_native_independent_errors(self):
        first = original_error()
        second = original_error()
        second["source"] = "plugin_loader"
        error = self.invoke(response={"ok": False, "results": [
            {"ok": False, "error": first}, {"ok": False, "error": second}
        ]})["error"]
        self.assertEqual(error["causes"], [first, second])

    def test_native_success_has_null_error(self):
        payload = {"ok": True, "result": {"updated": 2}}
        item = self.invoke(response=payload)
        self.assertIsNone(item["error"])
        self.assertEqual(item["value"], payload)

    def test_batch_failure_retains_partial_success_and_metadata(self):
        original = original_error()
        completed = {"ok": True, "value": {"updated": 1}}
        error = self.invoke(response={"ok": False, "diagnostic": {"remaining": 1},
                           "results": [completed, {"error": original}]})["error"]
        self.assertEqual(error["causes"], [original])
        self.assertEqual(error["data"][0]["diagnostic"], {"remaining": 1})
        self.assertEqual(error["data"][0]["successful_results"], [{"index": 0, "result": completed}])
        self.assertEqual(error["data"][0]["failed_result_indexes"], [1])

    def test_identical_independent_errors_are_retained(self):
        original = original_error()
        error = self.invoke(response={"ok": False, "results": [
            {"error": original}, {"error": original}
        ]})["error"]
        self.assertEqual(error["causes"], [original, original])

    def test_failed_exit_preserves_reported_error(self):
        original = original_error()
        error = self.invoke(response={"ok": False, "error": original}, exit_code=9)["error"]
        self.assertEqual(error["causes"], [original])
        self.assertEqual(error["data"][0]["process"]["exit_code"], 9)

    def test_nonzero_exit_is_failure(self):
        error = self.invoke(exit_code=23)["error"]
        self.assertEqual(error["type"], "process_error")
        self.assertEqual(error["data"][0]["process"]["exit_code"], 23)

    def test_exit_code_retains_native_width(self):
        error = self.invoke(exit_code=-1)["error"]
        self.assertEqual(error["data"][0]["process"]["exit_code"], 4294967295 if os.name == "nt" else 255)

    def test_missing_native_executable_preserves_path(self):
        missing = self.path / "missing-executable"
        error = self.invoke(overrides={"HOMEGROWPH_EDIT_FILE": str(missing)})["error"]
        self.assertTrue(any(data.get("path") == str(missing) for data in error["data"] if isinstance(data, dict)))
        self.assertEqual(error["type"], "system_error")
        self.assertIn("code", error["data"][0])

    def test_missing_definitions_preserves_native_error(self):
        missing = self.path / "missing-definitions.json"
        error = self.invoke(overrides={"HOMEGROWPH_TOOL_DEFINITIONS": str(missing)})["error"]
        self.assertEqual(error["operation"], "load_tool_definitions")
        self.assertEqual(error["type"], "system_error")
        self.assertEqual(error["data"][0]["path"], str(missing))
        self.assertIn("code", error["data"][0])

    def test_malformed_definitions_preserves_path_and_parser(self):
        definitions = self.path / "malformed.json"
        definitions.write_text('[{"broken":', encoding="utf-8")
        error = self.invoke(overrides={"HOMEGROWPH_TOOL_DEFINITIONS": str(definitions)})["error"]
        self.assertEqual(error["data"][0]["path"], str(definitions))
        self.assertEqual(error["data"][0]["id"], 101)

    def test_unicode_definition_and_executable_paths(self):
        definitions = self.path / "định nghĩa.json"
        executable = self.path / ("công cụ" + FIXTURE.suffix)
        shutil.copy2(self.definitions, definitions)
        shutil.copy2(FIXTURE, executable)
        item = self.invoke(overrides={"HOMEGROWPH_TOOL_DEFINITIONS": str(definitions),
                                     "HOMEGROWPH_EDIT_FILE": str(executable)})
        self.assertIsNone(item["error"])

    def test_worker_error_identity(self):
        self.worker_required()
        error = original_error()
        self.assertEqual(self.invoke(tool="read", response={"ok": False, "error": error})["error"], error)

    def test_worker_legacy_preserves_payload(self):
        self.worker_required()
        legacy = {"message": "Failed", "code": "EWORKER", "stack": "original stack", "path": "file.txt",
                  "body": {"details": [1, 2]}, "source_error": original_error()}
        error = self.invoke(tool="read", response={"ok": False, "error": legacy})["error"]
        self.assertEqual(error["causes"], [original_error()])
        self.assertEqual(error["data"][0], {key: value for key, value in legacy.items()
                                          if key not in ("message", "source_error")})

    def test_worker_success_has_null_error(self):
        self.worker_required()
        payload = {"ok": True, "result": {"text": "complete"}}
        item = self.invoke(tool="read", response={"ok": True, "readFiles": [],
                           "result": {"role": "tool", "content": json.dumps(payload)}})
        self.assertIsNone(item["error"])
        self.assertEqual(item["value"], payload)

    def test_worker_inner_error_identity(self):
        self.worker_required()
        error = original_error()
        item = self.invoke(tool="read", response={"ok": True, "readFiles": [],
                           "result": {"role": "tool", "content": json.dumps({"ok": False, "error": error})}})
        self.assertEqual(item["error"], error)

    def test_worker_missing_error_preserves_response(self):
        self.worker_required()
        response = {"ok": False, "diagnostic": {"status": 7}}
        error = self.invoke(tool="read", response=response)["error"]
        self.assertEqual(error["type"], "protocol_error")
        self.assertEqual(error["data"][0]["response"], response)

    def test_worker_invalid_stdout_preserves_parse_error(self):
        self.worker_required()
        error = self.invoke(tool="read", overrides={"TOOL_RUNTIME_FIXTURE_RESPONSE": "invalid json"})["error"]
        self.assertEqual(error["operation"], "parse_worker_response")
        self.assertEqual(error["data"][0]["stdout"], "invalid json")
        self.assertEqual(error["data"][0]["id"], 101)

    def test_worker_invalid_read_files(self):
        self.worker_required()
        error = self.invoke(tool="read", response={"ok": True, "readFiles": [7],
                           "result": {"role": "tool", "content": "{}"}})["error"]
        self.assertEqual(error["operation"], "merge_read_files")
        self.assertEqual(error["data"][0]["path"], "readFiles[0]")

    def test_worker_error_and_read_state_error_are_retained(self):
        self.worker_required()
        original = original_error()
        error = self.invoke(tool="read", response={"ok": True, "readFiles": [7],
                           "result": {"role": "tool", "content": json.dumps({"error": original})}})["error"]
        self.assertEqual(len(error["causes"]), 2)
        self.assertEqual(error["causes"][0], original)
        self.assertEqual(error["causes"][1]["operation"], "merge_read_files")


if __name__ == "__main__":
    unittest.main()
