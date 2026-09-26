import { stat } from "node:fs/promises";

import type {
  OpenAIFunctionToolCall,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import { createGlobMatcher, walkFiles } from "../_shared/file_search.js";
import {
  requireArguments,
  requireString,
  resolveToolPath,
  resultMessage,
  type ToolExecutionContext,
} from "../_shared/native_tool.js";

export const GLOB_TOOL_NAME = "glob";

export async function executeGlobToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  const args = requireArguments(toolCall, GLOB_TOOL_NAME);
  const pattern = requireString(args.pattern, "pattern");
  const root = resolveToolPath(context, args.path);
  const info = await stat(root);
  if (!info.isDirectory()) {
    throw new Error(`glob path must be an existing directory: ${root}`);
  }

  const matcher = createGlobMatcher(pattern);
  const matches = (await walkFiles(root))
    .filter((file) => matcher(file.relativePath))
    .sort((left, right) => right.mtimeMs - left.mtimeMs);
  const limit = 100;
  const truncated = matches.length > limit;
  const final = matches.slice(0, limit);
  const output = final.map((file) => file.path);
  if (output.length === 0) output.push("No files found");
  if (truncated) {
    output.push(
      "",
      `(Results are truncated: showing first ${limit} results. Consider a more specific path or pattern.)`,
    );
  }

  return resultMessage(toolCall, {
    title: root,
    metadata: { count: final.length, truncated },
    output: output.join("\n"),
  });
}
