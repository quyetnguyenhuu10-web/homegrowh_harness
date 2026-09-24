import definitions from "./tool_definitions.json" with { type: "json" };

import type { OpenAIFunctionToolDefinition } from "./_shared/openai_executable_tool.js";

const toolDefinitions = definitions as OpenAIFunctionToolDefinition[];

export function toolDefinition(name: string): OpenAIFunctionToolDefinition {
  const definition = toolDefinitions.find(
    (item) => item.type === "function" && item.function.name === name,
  );

  if (!definition) {
    throw new Error(`Tool definition not found: ${name}`);
  }

  return structuredClone(definition);
}
