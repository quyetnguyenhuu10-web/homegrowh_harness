import {
  runOpenAIExecutableTool,
  type OpenAIFunctionToolCall,
  type OpenAIFunctionToolDefinition,
  type OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool";

export const READ_FILE_TOOL_NAME = "read_file";

/** Relative từ package/tools/read_file/. Không hardcode checkout path. */
export const READ_FILE_EXECUTABLE =
  "../filesystems/build/Debug/read_file.exe";

export const readFileToolDefinition: OpenAIFunctionToolDefinition = {
  type: "function",
  function: {
    name: READ_FILE_TOOL_NAME,
    description:
      "Đọc một hoặc nhiều khoảng dòng từ file. Dòng đánh số từ 1 và end_line là inclusive.",
    parameters: {
      oneOf: [
        {
          type: "object",
          properties: {
            path: { type: "string" },
            start_line: { type: "integer", minimum: 1 },
            end_line: { type: "integer", minimum: 1 },
          },
          required: ["path", "start_line", "end_line"],
          additionalProperties: false,
        },
        {
          type: "array",
          minItems: 1,
          items: {
            type: "object",
            properties: {
              path: { type: "string" },
              start_line: { type: "integer", minimum: 1 },
              end_line: { type: "integer", minimum: 1 },
            },
            required: ["path", "start_line", "end_line"],
            additionalProperties: false,
          },
        },
      ],
    },
  },
};

export function executeReadFileToolCall(
  toolCall: OpenAIFunctionToolCall,
): Promise<OpenAIToolResultMessage> {
  return runOpenAIExecutableTool({
    toolName: READ_FILE_TOOL_NAME,
    executableRelativePath: READ_FILE_EXECUTABLE,
    moduleUrl: import.meta.url,
    toolCall,
  });
}

export type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool";
