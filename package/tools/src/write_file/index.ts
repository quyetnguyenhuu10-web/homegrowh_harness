import { toolDefinition } from "../tool_definitions.js";
import { mkdir, stat, writeFile } from "node:fs/promises";
import { dirname } from "node:path";

import type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import {
  requireArguments,
  requireString,
  resolveToolPath,
  resultMessage,
  type ToolExecutionContext,
} from "../_shared/native_tool.js";
import { markFileRead, wasFileRead } from "../_shared/read_state.js";
import {
  convertToLineEnding,
  detectLineEnding,
  exists,
  joinBom,
  normalizeLineEndings,
  readFileWithBom,
  splitBom,
} from "../_shared/text_file.js";

export const WRITE_FILE_TOOL_NAME = "write";

export const writeFileToolDefinition = toolDefinition(WRITE_FILE_TOOL_NAME);

export async function executeWriteFileToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  const args = requireArguments(toolCall, WRITE_FILE_TOOL_NAME);
  const requested = requireString(args.filePath, "filePath");
  if (typeof args.content !== "string") {
    throw new Error("content is required");
  }

  const filePath = resolveToolPath(context, requested);
  const existed = await exists(filePath);
  if (
    existed &&
    !wasFileRead(filePath, context.conversationId, context.requestId)
  ) {
    throw new Error(
      "You must use the read tool before overwriting an existing file.",
    );
  }

  if (existed) {
    const info = await stat(filePath);
    if (info.isDirectory()) {
      throw new Error(`Path is a directory, not a file: ${filePath}`);
    }
  }

  const source = existed
    ? await readFileWithBom(filePath)
    : { bom: false, text: "" };
  const next = splitBom(args.content);
  const nextText = existed
    ? convertToLineEnding(
        normalizeLineEndings(next.text),
        detectLineEnding(source.text),
      )
    : next.text;
  const desiredBom = source.bom || next.bom;

  await mkdir(dirname(filePath), { recursive: true });
  await writeFile(filePath, joinBom(nextText, desiredBom), "utf8");
  markFileRead(filePath, context.conversationId, context.requestId);

  return resultMessage(toolCall, {
    title: filePath,
    metadata: {
      filepath: filePath,
      exists: existed,
    },
    output: "Wrote file successfully.",
  });
}
