import { randomUUID } from "node:crypto";

import { getModelContextLimit } from "@homegrowh/provider";

import { toolRegistry } from "../../../../../tools/registry/index";

import {
  DEFAULT_REASONING_HISTORY_POLICY,
  getActiveConversation,
  normalizeReasoningHistoryPolicy,
  writeConversationContextUsage,
  type ConversationContextUsageUpdatedEvent,
  type ConversationRequestSnapshot,
  type ConversationRequestStateEvent,
  type ConversationRowAppendedEvent,
  type ProviderErrorNoticeEvent,
  type ReasoningHistoryPolicy,
  type SendChatRequestInput,
} from "../../history_conversation/main";
import type { AddRepositoryOptions } from "../../history_conversation/main";
import { buildModelContext } from "../../context/main";
import { resolveModelRuntime } from "../../picker_model/main";
import {
  createConversationDispatcher,
  type ConversationDispatcher,
  type ConversationRequestContext,
} from "./conversation_dispatcher";
import type { SessionModelEvent } from "./model_events";
import { runProviderTurn } from "./provider_turn";

export interface SessionConversationRuntime {
  createRequest(context: ConversationRequestContext): string;
  registerRequest(requestId: string, context: ConversationRequestContext): void;
  releaseRequest(requestId: string): void;
  sendChatRequest(input: SendChatRequestInput): Promise<string>;
  cancelChatRequest(requestId: string): void;
  listActiveRequests(): ConversationRequestSnapshot[];
  close(): Promise<void>;
}

export interface SessionConversationRuntimeOptions extends AddRepositoryOptions {
  onPersistedRow?: (event: ConversationRowAppendedEvent) => void;
  onRequestState?: (event: ConversationRequestStateEvent) => void;
  onContextUsageUpdated?: (event: ConversationContextUsageUpdatedEvent) => void;
  onProviderError?: (event: ProviderErrorNoticeEvent) => void;
  onError?: (error: unknown, event: SessionModelEvent) => void;
}

interface ActiveModelRequest {
  provider: SendChatRequestInput["provider"];
  model: string;
  contextWindowTokens: number;
  apiKey?: string;
  baseUrl?: string;
  apiSession: string;
  context: ConversationRequestContext;
  contextTrigger: "user" | "toolresult";
  reasoningHistory: ReasoningHistoryPolicy;
  abortController: AbortController | null;
}

let runtime: SessionConversationRuntime | null = null;
let startPromise: Promise<SessionConversationRuntime> | null = null;

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function toolExecutionErrorResult(
  toolCall: Parameters<typeof toolRegistry.execute>[0],
  error: unknown,
): Awaited<ReturnType<typeof toolRegistry.execute>> {
  return {
    role: "tool",
    tool_call_id: toolCall.id,
    content: JSON.stringify({
      ok: false,
      error: {
        code: "tool_execution_error",
        tool: toolCall.function.name,
        name: error instanceof Error ? error.name : typeof error,
        message: errorMessage(error),
      },
    }),
  };
}

function toolSchemaErrorResult(
  toolCall: Parameters<typeof toolRegistry.execute>[0],
  message: string,
): Awaited<ReturnType<typeof toolRegistry.execute>> {
  return {
    role: "tool",
    tool_call_id: toolCall.id,
    content: JSON.stringify({
      ok: false,
      error: {
        code: "invalid_tool_call_schema",
        tool: toolCall.function.name,
        message,
      },
    }),
  };
}

function createRuntime(
  options: SessionConversationRuntimeOptions,
): SessionConversationRuntime {
  const dispatcher: ConversationDispatcher = createConversationDispatcher({
    projectsDir: options.projectsDir,
    onPersistedRow: options.onPersistedRow,
    onRequestState: options.onRequestState,
    onError: options.onError,
  });

  const activeModelRequests = new Map<string, ActiveModelRequest>();
  let closed = false;

  const runRequest = async (
    requestId: string,
    state: ActiveModelRequest,
  ): Promise<void> => {
    try {
      while (!closed && activeModelRequests.get(requestId) === state) {
        const apiSession = state.apiSession;
        const contextLimit = getModelContextLimit(
          state.provider,
          state.model,
          state.contextWindowTokens,
        );
        const usage = writeConversationContextUsage(
          state.context.repositoryPath,
          state.context.conversationId,
          {
            provider: state.provider,
            model: state.model,
            API_sessions: apiSession,
            trigger: state.contextTrigger,
            contextLimitTokens: contextLimit.tokens,
            contextLimitCharacters: contextLimit.estimatedCharacters,
            charactersPerToken: contextLimit.charactersPerToken,
            reasoningHistory: state.reasoningHistory,
          },
          { projectsDir: options.projectsDir },
        );
        options.onContextUsageUpdated?.({
          repositoryPath: state.context.repositoryPath,
          conversationId: state.context.conversationId,
          API_sessions: apiSession,
        });

        if (usage.estimatedTokens > contextLimit.tokens) {
          options.onProviderError?.({
            repositoryPath: state.context.repositoryPath,
            conversationId: state.context.conversationId,
            requestId,
            message:
              `Context ước tính ${usage.estimatedTokens.toLocaleString()} token ` +
              `vượt giới hạn ${contextLimit.tokens.toLocaleString()} token của ${state.model}.`,
          });
          activeModelRequests.delete(requestId);
          dispatcher.releaseRequest(requestId);
          return;
        }

        const modelContext = buildModelContext(
          state.context.repositoryPath,
          state.context.conversationId,
          { projectsDir: options.projectsDir },
          state.reasoningHistory,
        );

        const controller = new AbortController();
        state.abortController = controller;

        let turn;
        try {
          turn = await runProviderTurn({
            requestId,
            apiSession,
            provider: state.provider,
            model: state.model,
            apiKey: state.apiKey,
            baseUrl: state.baseUrl,
            messages: modelContext.messages,
            tools: modelContext.tools,
            signal: controller.signal,
            onEvent: (event) => dispatcher.handleModelEvent(event),
          });
        } catch (error) {
          if (controller.signal.aborted) {
            await dispatcher.handleModelEvent({
              type: "assistant.cancelled",
              requestId,
              apiSession,
            });
          } else {
            options.onProviderError?.({
              repositoryPath: state.context.repositoryPath,
              conversationId: state.context.conversationId,
              requestId,
              message: errorMessage(error),
            });
            dispatcher.releaseRequest(requestId);
          }
          activeModelRequests.delete(requestId);
          return;
        } finally {
          if (state.abortController === controller) {
            state.abortController = null;
          }
        }

        if (turn.toolCalls.length === 0) {
          await dispatcher.handleModelEvent({
            type: "assistant.done",
            requestId,
            apiSession,
          });
          activeModelRequests.delete(requestId);
          return;
        }

        await dispatcher.handleModelEvent({
          type: "assistant.tool_calls",
          requestId,
          apiSession,
          toolCalls: turn.toolCalls.map((attempt) => attempt.toolCall),
        });

        for (const attempt of turn.toolCalls) {
          const toolCall = attempt.toolCall;
          let result: Awaited<ReturnType<typeof toolRegistry.execute>>;
          if (attempt.error) {
            result = toolSchemaErrorResult(toolCall, attempt.error.message);
          } else {
            try {
              result = await toolRegistry.execute(toolCall);
            } catch (error) {
              result = toolExecutionErrorResult(toolCall, error);
            }
          }

          await dispatcher.persistRequestRow(requestId, {
            type: "toolresult",
            role: "tool",
            content: JSON.stringify(result),
            API_sessions: apiSession,
          });
        }

        state.apiSession = randomUUID();
        state.contextTrigger = "toolresult";
      }
    } catch (error) {
      activeModelRequests.delete(requestId);
      dispatcher.releaseRequest(requestId);
      console.error("[chat_thread] session request failed", error);
    }
  };

  const current: SessionConversationRuntime = {
    createRequest(context): string {
      return dispatcher.createRequest(context);
    },

    registerRequest(requestId, context): void {
      dispatcher.registerRequest(requestId, context);
    },

    releaseRequest(requestId): void {
      activeModelRequests.get(requestId)?.abortController?.abort(
        new Error("Session request released"),
      );
      activeModelRequests.delete(requestId);
      dispatcher.releaseRequest(requestId);
    },

    async sendChatRequest(input): Promise<string> {
      if (closed) {
        throw new Error("Session conversation runtime đã đóng.");
      }

      const prompt = input.prompt.trim();
      if (!prompt) throw new Error("Không thể gửi prompt rỗng.");
      if (!input.model.trim()) {
        throw new Error("Không thể gửi khi chưa chọn model.");
      }

      const active = getActiveConversation({
        projectsDir: options.projectsDir,
      });
      if (!active) {
        throw new Error("Không có conversation active để gửi provider.");
      }

      const context: ConversationRequestContext = {
        repositoryPath: active.repositoryPath,
        conversationId: active.conversationId,
      };
      const resolvedModel = resolveModelRuntime(
        {
          provider: input.provider,
          model: input.model,
        },
        { projectsDir: options.projectsDir },
      );
      const requestId = dispatcher.createRequest(context);
      const state: ActiveModelRequest = {
        provider: resolvedModel.provider,
        model: resolvedModel.model,
        contextWindowTokens: resolvedModel.contextWindowTokens,
        apiKey: resolvedModel.apiKey,
        baseUrl: resolvedModel.baseUrl,
        apiSession: randomUUID(),
        context,
        reasoningHistory: normalizeReasoningHistoryPolicy(
          input.reasoningHistory ?? DEFAULT_REASONING_HISTORY_POLICY,
        ),
        contextTrigger: "user",
        abortController: null,
      };
      activeModelRequests.set(requestId, state);

      try {
        await dispatcher.persistRequestRow(requestId, {
          type: "user",
          role: "user",
          content: prompt,
          API_sessions: state.apiSession,
        });
      } catch (error) {
        activeModelRequests.delete(requestId);
        dispatcher.releaseRequest(requestId);
        throw error;
      }

      void runRequest(requestId, state);
      return requestId;
    },

    cancelChatRequest(requestId): void {
      const state = activeModelRequests.get(requestId.trim());
      state?.abortController?.abort(new Error("Cancelled by user"));
    },

    listActiveRequests(): ConversationRequestSnapshot[] {
      return dispatcher.listActiveRequests();
    },

    async close(): Promise<void> {
      if (closed) return;
      closed = true;

      for (const state of activeModelRequests.values()) {
        state.abortController?.abort(new Error("Session runtime closed"));
      }
      activeModelRequests.clear();
      dispatcher.dispose();
    },
  };

  return current;
}

export function startSessionConversationRuntime(
  options: SessionConversationRuntimeOptions = {},
): Promise<SessionConversationRuntime> {
  if (runtime) return Promise.resolve(runtime);
  if (startPromise) return startPromise;

  startPromise = Promise.resolve(createRuntime(options))
    .then((created) => {
      runtime = created;
      return created;
    })
    .finally(() => {
      startPromise = null;
    });

  return startPromise;
}

export function getSessionConversationRuntime(): SessionConversationRuntime | null {
  return runtime;
}

export async function stopSessionConversationRuntime(): Promise<void> {
  const pending = startPromise;
  if (pending) {
    try {
      await pending;
    } catch {
      return;
    }
  }

  const current = runtime;
  runtime = null;
  await current?.close();
}
