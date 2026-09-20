import type { ChatToolCall } from "@homegrowh/provider";

export type SessionModelEvent =
  | {
      type: "assistant.delta";
      requestId: string;
      apiSession: string;
      eventIndex: number;
      delta: string;
    }
  | {
      type: "assistant.reasoning.delta";
      requestId: string;
      apiSession: string;
      eventIndex: number;
      delta: string;
    }
  | {
      type: "assistant.tool_call.delta";
      requestId: string;
      apiSession: string;
      eventIndex: number;
      delta: string;
    }
  | {
      type: "assistant.tool_calls";
      requestId: string;
      apiSession: string;
      toolCalls: ChatToolCall[];
    }
  | {
      type: "assistant.done";
      requestId: string;
      apiSession: string;
    }
  | {
      type: "assistant.cancelled";
      requestId: string;
      apiSession: string;
    };
