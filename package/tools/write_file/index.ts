import {
  runOpenAIExecutableTool,
  type OpenAIFunctionToolCall,
  type OpenAIFunctionToolDefinition,
  type OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool";

export const WRITE_FILE_TOOL_NAME = "write_file";

/** Relative từ package/tools/write_file/. Không hardcode checkout path. */
export const WRITE_FILE_EXECUTABLE =
  "../filesystems/build/Debug/write_file.exe";

export const writeFileToolDefinition: OpenAIFunctionToolDefinition = {
  type: "function",
  function: {
    name: WRITE_FILE_TOOL_NAME,
    description:
      "Ghi đè một hoặc nhiều file hiện có hoặc tạo file mới.",
    parameters: {
      oneOf: [
        {
          type: "object",
          properties: {
            path: { type: "string" },
            new_content: { type: "string" },
          },
          required: ["path", "new_content"],
          additionalProperties: false,
        },
        {
          type: "array",
          minItems: 1,
          items: {
            type: "object",
            properties: {
              path: { type: "string" },
              new_content: { type: "string" },
            },
            required: ["path", "new_content"],
            additionalProperties: false,
          },
        },
      ],
    },
  },
};

export function executeWriteFileToolCall(
  toolCall: OpenAIFunctionToolCall,
): Promise<OpenAIToolResultMessage> {
  return runOpenAIExecutableTool({
    toolName: WRITE_FILE_TOOL_NAME,
    executableRelativePath: WRITE_FILE_EXECUTABLE,
    moduleUrl: import.meta.url,
    toolCall,
  });
}

export type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool";
