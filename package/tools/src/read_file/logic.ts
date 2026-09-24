import type {
  OpenAIFunctionToolCall,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import {
  readPositiveInteger,
  requireArguments,
  requireString,
  resolveToolPath,
  resultMessage,
  type ToolExecutionContext,
} from "../_shared/native_tool.js";
import { markFileRead } from "../_shared/read_state.js";
import {
  DEFAULT_READ_LIMIT,
  MAX_BYTES,
  isBinaryFile,
  readDirectoryNames,
  readLines,
  readSample,
  statSafe,
} from "../_shared/text_file.js";

export const READ_FILE_TOOL_NAME = "read";

export async function executeReadFileToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  const args = requireArguments(toolCall, READ_FILE_TOOL_NAME);
  const requested = requireString(args.filePath, "filePath");
  const filePath = resolveToolPath(context, requested);
  const info = await statSafe(filePath);
  if (!info) throw new Error(`Path not found: ${filePath}`);

  const offset = readPositiveInteger(args.offset, 1);
  const limit =
    args.limit === 0
      ? 0
      : readPositiveInteger(args.limit, DEFAULT_READ_LIMIT);

  if (info.isDirectory()) {
    const names = await readDirectoryNames(filePath);
    const start = offset - 1;
    const sliced = names.slice(start, start + limit);
    const truncated = start + sliced.length < names.length;
    return resultMessage(toolCall, {
      title: filePath,
      output: [
        `<path>${filePath}</path>`,
        "<type>directory</type>",
        "<entries>",
        sliced.join("\n"),
        truncated
          ? `\n(Showing ${sliced.length} of ${names.length} entries. Use offset=${offset + sliced.length} to continue.)`
          : `\n(${names.length} entries)`,
        "</entries>",
      ].join("\n"),
      metadata: {
        preview: sliced.slice(0, 20).join("\n"),
        truncated,
        loaded: [],
      },
    });
  }

  if (!info.isFile()) {
    throw new Error(`Path is not a regular file: ${filePath}`);
  }

  const sample = await readSample(filePath, info.size);
  if (isBinaryFile(filePath, sample)) {
    throw new Error(`Cannot read binary file: ${filePath}`);
  }

  const file = await readLines(filePath, offset, limit);
  if (file.count < file.offset && !(file.count === 0 && file.offset === 1)) {
    throw new Error(
      `Offset ${file.offset} is out of range for this file (${file.count} lines)`,
    );
  }

  let output = [
    `<path>${filePath}</path>`,
    "<type>file</type>",
    "<content>\n",
  ].join("\n");
  output += file.raw
    .map((line, index) => `${index + file.offset}: ${line}`)
    .join("\n");

  const last = file.offset + file.raw.length - 1;
  const next = last + 1;
  if (file.cut) {
    output += `\n\n(Output capped at ${MAX_BYTES / 1024} KB. Showing lines ${file.offset}-${last}. Use offset=${next} to continue.)`;
  } else if (file.more) {
    output += `\n\n(Showing lines ${file.offset}-${last} of ${file.count}. Use offset=${next} to continue.)`;
  } else {
    output += `\n\n(End of file - total ${file.count} lines)`;
  }
  output += "\n</content>";

  markFileRead(
    filePath,
    context.conversationId,
    context.requestId,
  );

  return resultMessage(toolCall, {
    title: filePath,
    output,
    metadata: {
      preview: file.raw.slice(0, 20).join("\n"),
      truncated: file.more || file.cut,
      loaded: [],
    },
  });
}
