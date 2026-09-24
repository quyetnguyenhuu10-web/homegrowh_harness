import { toolDefinition } from "../tool_definitions.js";
import { stat } from "node:fs/promises";
import { dirname, relative } from "node:path";

import type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import {
  createGlobMatcher,
  readTextFileIfText,
  walkFiles,
  type WalkedFile,
} from "../_shared/file_search.js";
import {
  requireArguments,
  requireString,
  resolveToolPath,
  resultMessage,
  type ToolExecutionContext,
} from "../_shared/native_tool.js";

export const GREP_TOOL_NAME = "grep";

export const grepToolDefinition = toolDefinition(GREP_TOOL_NAME);

export async function executeGrepToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  const args = requireArguments(toolCall, GREP_TOOL_NAME);
  const pattern = requireString(args.pattern, "pattern");
  const requested = resolveToolPath(context, args.path);
  const info = await stat(requested).catch(() => null);
  if (!info) throw new Error(`Path not found: ${requested}`);

  let regex: RegExp;
  try {
    regex = new RegExp(pattern);
  } catch (error) {
    throw new Error(
      `Invalid regex pattern: ${error instanceof Error ? error.message : String(error)}`,
    );
  }

  const root = info.isDirectory() ? requested : dirname(requested);
  let files: WalkedFile[];
  if (info.isDirectory()) {
    files = await walkFiles(requested);
  } else {
    files = [{
      path: requested,
      relativePath: relative(root, requested).replace(/\\/g, "/"),
      mtimeMs: info.mtimeMs,
    }];
  }

  const include =
    typeof args.include === "string" && args.include.trim()
      ? createGlobMatcher(args.include.trim())
      : () => true;
  const rows: Array<{ path: string; line: number; text: string; mtimeMs: number }> = [];

  for (const file of files) {
    if (!include(file.relativePath)) continue;
    const content = await readTextFileIfText(file.path);
    if (content === null) continue;
    const lines = content.split(/\r?\n/);
    for (let index = 0; index < lines.length; index += 1) {
      regex.lastIndex = 0;
      if (!regex.test(lines[index])) continue;
      rows.push({
        path: file.path,
        line: index + 1,
        text:
          lines[index].length > 2000
            ? `${lines[index].slice(0, 2000)}...`
            : lines[index],
        mtimeMs: file.mtimeMs,
      });
    }
  }

  rows.sort((left, right) => right.mtimeMs - left.mtimeMs);
  const limit = 100;
  const truncated = rows.length > limit;
  const final = rows.slice(0, limit);
  const output: string[] = [];
  if (final.length === 0) {
    output.push("No files found");
  } else {
    output.push(
      `Found ${rows.length} matches${truncated ? ` (showing first ${limit})` : ""}`,
    );
    let current = "";
    for (const match of final) {
      if (current !== match.path) {
        if (current) output.push("");
        current = match.path;
        output.push(`${match.path}:`);
      }
      output.push(`  Line ${match.line}: ${match.text}`);
    }
  }

  return resultMessage(toolCall, {
    title: pattern,
    metadata: { matches: rows.length, truncated },
    output: output.join("\n"),
  });
}
