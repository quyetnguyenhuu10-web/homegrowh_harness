import type { ProviderName } from "@homegrowh/provider";

import {
  getChatThreadDesktopBridge,
  type ReasoningHistoryPolicy,
  type SendChatRequestResult,
} from "../../history_conversation/renderer";

export interface ActiveConversationChatRequestInput {
  provider: ProviderName;
  model: string;
  prompt: string;
  stream?: boolean;
  reasoningHistory?: ReasoningHistoryPolicy;
}

/** Renderer adapter mỏng; session_conversation/main tự resolve active conversation. */
export async function sendActiveConversationChatRequest(
  input: ActiveConversationChatRequestInput,
): Promise<SendChatRequestResult> {
  const bridge = getChatThreadDesktopBridge();
  if (!bridge) {
    throw new Error("Chat thread desktop bridge chưa sẵn sàng.");
  }

  return bridge.sendChatRequest({
    provider: input.provider,
    model: input.model,
    prompt: input.prompt,
    stream: input.stream,
    reasoningHistory: input.reasoningHistory,
  });
}
