import {
  runOpenAIExecutableTool,
  type OpenAIFunctionToolCall,
  type OpenAIFunctionToolDefinition,
  type OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool";

export const EDIT_FILE_TOOL_NAME = "edit_file";

/** Relative từ package/tools/edit_file/. Không hardcode checkout path. */
export const EDIT_FILE_EXECUTABLE =
  "../filesystems/build/Debug/edit_file.exe";

export const editFileToolDefinition: OpenAIFunctionToolDefinition = {
  type: "function",
  function: {
    name: EDIT_FILE_TOOL_NAME,
    description:
      "Thay old_content bằng new_content trong một hoặc nhiều file.",
    parameters: {
      oneOf: [
        {
          type: "object",
          properties: {
            path: { type: "string" },
            old_content: { type: "string" },
            new_content: { type: "string" },
          },
          required: ["path", "old_content", "new_content"],
          additionalProperties: false,
        },
        {
          type: "array",
          minItems: 1,
          items: {
            type: "object",
            properties: {
              path: { type: "string" },
              old_content: { type: "string" },
              new_content: { type: "string" },
            },
            required: ["path", "old_content", "new_content"],
            additionalProperties: false,
          },
        },
      ],
    },
  },
};

export function executeEditFileToolCall(
  toolCall: OpenAIFunctionToolCall,
): Promise<OpenAIToolResultMessage> {
  return runOpenAIExecutableTool({
    toolName: EDIT_FILE_TOOL_NAME,
    executableRelativePath: EDIT_FILE_EXECUTABLE,
    moduleUrl: import.meta.url,
    toolCall,
  });
}

export type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool";
