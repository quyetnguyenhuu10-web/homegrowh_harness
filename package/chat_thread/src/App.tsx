import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import ChatContainer from "./component/chat_container/renderer";
import Composer, {
  type ComposerSubmitInput,
} from "./component/composer/renderer";
import EmptyConversationState from "./component/empty_conversation_state/renderer";
import type { ModelSelection } from "./component/picker_model/renderer";
import { sendActiveConversationChatRequest } from "./component/session_conversation/renderer";
import {
  getChatThreadDesktopBridge,
  NORMAL_CONVERSATION_SCOPE,
  type ConversationRequestSnapshot,
  type HistoryContextUsage,
  type HistoryRow,
  type ReasoningHistoryPolicy,
} from "./component/history_conversation/renderer";
import { projectHistoryEntries } from "./component/view_history/renderer";
import type { HistoryEntry } from "./component/view_history/renderer";

const NAVIGATOR_BASE = {
  title: "Homegrowh Harness",
  conversations: [],
  projects: [],
} as const;

type RequestVisualPhase = "waiting" | "reasoning" | "other";

function mergeHistoryRows(
  ...collections: ReadonlyArray<readonly HistoryRow[]>
): HistoryRow[] {
  const byId = new Map<number, HistoryRow>();
  for (const rows of collections) {
    for (const row of rows) byId.set(row.id, row);
  }
  return [...byId.values()].sort((left, right) => left.id - right.id);
}

export default function App() {
  const [historyRows, setHistoryRows] = useState<readonly HistoryRow[]>([]);
  const [showThinking, setShowThinking] = useState(true);
  const [navigatorRevision, setNavigatorRevision] = useState(0);
  const [selectedModel, setSelectedModel] =
    useState<ModelSelection | null>(null);
  const [requestSnapshots, setRequestSnapshots] = useState<
    readonly ConversationRequestSnapshot[]
  >([]);
  const [providerNotice, setProviderNotice] = useState<string | null>(null);
  const [reasoningHistory, setReasoningHistory] =
    useState<ReasoningHistoryPolicy>({ mode: "recent", requestCount: 5 });
  const [contextUsage, setContextUsage] = useState<HistoryContextUsage | null>(
    null,
  );
  const [requestVisualPhases, setRequestVisualPhases] = useState<
    Readonly<Record<string, RequestVisualPhase>>
  >({});
  const [activeConversation, setActiveConversation] = useState<{
    repositoryPath: string;
    conversationId: string;
  } | null>(null);
  const activeConversationRef = useRef<{
    repositoryPath: string;
    conversationId: string;
  } | null>(null);
  const readRequestRef = useRef(0);
  const rowQueryTailRef = useRef<Promise<void>>(Promise.resolve());
  const contextUsageQueryTailRef = useRef<Promise<void>>(Promise.resolve());
  const providerNoticeTimerRef = useRef<ReturnType<typeof setTimeout> | null>(
    null,
  );

  const clearProviderNotice = useCallback((): void => {
    if (providerNoticeTimerRef.current !== null) {
      clearTimeout(providerNoticeTimerRef.current);
      providerNoticeTimerRef.current = null;
    }
    setProviderNotice(null);
  }, []);

  const entries = useMemo((): HistoryEntry[] => {
    if (!activeConversation) return [];

    const projected = projectHistoryEntries(
      activeConversation.conversationId,
      historyRows,
    );
    const activeRequest = requestSnapshots.find(
      (request) =>
        request.repositoryPath === activeConversation.repositoryPath &&
        request.conversationId === activeConversation.conversationId,
    );
    if (!activeRequest) return projected;

    const phase = requestVisualPhases[activeRequest.requestId] ?? "waiting";
    if (phase === "waiting") {
      return [
        ...projected,
        {
          id: `thinking:${activeRequest.requestId}`,
          type: "thinking",
          role: "assistant",
          sourceRowIds: [],
          streaming: true,
        },
      ];
    }

    if (phase === "reasoning") {
      for (let index = projected.length - 1; index >= 0; index -= 1) {
        const entry = projected[index];
        if (entry.type === "reasoning" || entry.type === "thinking") {
          projected[index] = { ...entry, streaming: true };
          break;
        }
      }
    }

    return projected;
  }, [activeConversation, historyRows, requestSnapshots, requestVisualPhases]);

  useEffect(() => {
    const bridge = getChatThreadDesktopBridge();
    if (!bridge) return;

    const removeRow = bridge.onConversationRow((event) => {
      rowQueryTailRef.current = rowQueryTailRef.current
        .catch(() => {})
        .then(async () => {
          const row = await bridge.readConversationRow(
            event.repositoryPath,
            event.conversationId,
            event.rowId,
          );
          if (!row) {
            throw new Error(
              `DB đã báo append row ${event.rowId} nhưng query lại không thấy row.`,
            );
          }
          if (
            row.id !== event.rowId ||
            row.API_sessions !== event.API_sessions
          ) {
            throw new Error(
              `Row reference lệch nguồn sự thật: event row=${event.rowId}/${event.API_sessions}, DB row=${row.id}/${row.API_sessions}.`,
            );
          }

          const active = activeConversationRef.current;
          if (
            active?.repositoryPath !== event.repositoryPath ||
            active.conversationId !== event.conversationId
          ) {
            return;
          }

          setHistoryRows((current) => mergeHistoryRows(current, [row]));
          if (event.requestId) {
            if (row.type === "reasoning" && row.role === "assistant") {
              setRequestVisualPhases((current) => ({
                ...current,
                [event.requestId!]: "reasoning",
              }));
            } else if (
              (row.type === "reply" || row.type === "toolcall") &&
              row.role === "assistant"
            ) {
              setRequestVisualPhases((current) => ({
                ...current,
                [event.requestId!]: "other",
              }));
            } else if (row.type === "toolresult") {
              setRequestVisualPhases((current) => ({
                ...current,
                [event.requestId!]: "waiting",
              }));
            }
          }
        })
        .catch((error) => {
          console.error("[App] read appended history row failed", error);
        });
    });

    const removeRequestState =
      typeof bridge.onChatRequestState === "function"
        ? bridge.onChatRequestState((event) => {
            if (event.active) {
              setRequestSnapshots((current) => [
                ...current.filter(
                  (request) => request.requestId !== event.request.requestId,
                ),
                event.request,
              ]);
              setRequestVisualPhases((current) => ({
                ...current,
                [event.request.requestId]: "waiting",
              }));
              return;
            }

            setRequestSnapshots((current) =>
              current.filter((request) => request.requestId !== event.requestId),
            );
            setRequestVisualPhases((current) => {
              const next = { ...current };
              delete next[event.requestId];
              return next;
            });

          })
        : () => {};

    const removeProviderErrorNotice = bridge.onProviderErrorNotice((event) => {
      const active = activeConversationRef.current;
      if (
        active?.repositoryPath !== event.repositoryPath ||
        active.conversationId !== event.conversationId
      ) {
        return;
      }

      if (providerNoticeTimerRef.current !== null) {
        clearTimeout(providerNoticeTimerRef.current);
      }
      setProviderNotice(event.message);
      providerNoticeTimerRef.current = setTimeout(() => {
        providerNoticeTimerRef.current = null;
        setProviderNotice(null);
      }, 4_000);
    });

    const removeContextUsageUpdated =
      bridge.onConversationContextUsageUpdated((event) => {
        contextUsageQueryTailRef.current = contextUsageQueryTailRef.current
          .catch(() => {})
          .then(async () => {
            const usage = await bridge.readConversationContextUsage(
              event.repositoryPath,
              event.conversationId,
            );
            if (!usage || usage.API_sessions !== event.API_sessions) return;

            const active = activeConversationRef.current;
            if (
              active?.repositoryPath !== event.repositoryPath ||
              active.conversationId !== event.conversationId
            ) {
              return;
            }
            setContextUsage(usage);
          })
          .catch((error) => {
            console.error("[App] read context usage failed", error);
          });
      });

    return () => {
      removeRow();
      removeRequestState();
      removeProviderErrorNotice();
      removeContextUsageUpdated();
      if (providerNoticeTimerRef.current !== null) {
        clearTimeout(providerNoticeTimerRef.current);
        providerNoticeTimerRef.current = null;
      }
    };
  }, []);

  const openConversation = useCallback(
    (repositoryPath: string, conversationId: string): void => {
      const bridge = getChatThreadDesktopBridge();
      if (!bridge) return;

      const requestId = ++readRequestRef.current;
      clearProviderNotice();
      const nextActive = { repositoryPath, conversationId };
      activeConversationRef.current = nextActive;
      setActiveConversation(nextActive);
      setHistoryRows([]);
      setContextUsage(null);

      void bridge.setActiveConversation(repositoryPath, conversationId)
        .then(async () => {
          const rows = await bridge.readConversation(
            repositoryPath,
            conversationId,
          );
          const requests =
            typeof bridge.listActiveChatRequests === "function"
              ? await bridge.listActiveChatRequests().catch(() => [])
              : [];
          const usage = await bridge
            .readConversationContextUsage(repositoryPath, conversationId)
            .catch(() => null);
          return [rows, requests, usage] as const;
        })
        .then(([rows, requests, usage]) => {
          if (readRequestRef.current !== requestId) return;

          setRequestSnapshots(requests);
          setHistoryRows((current) => mergeHistoryRows(rows, current));
          setContextUsage(usage);
        })
        .catch((error) => {
          if (readRequestRef.current !== requestId) return;
          console.error("[App] openConversation failed", error);
          setHistoryRows([]);
          setContextUsage(null);
        });
    },
    [clearProviderNotice],
  );

  useEffect(() => {
    let cancelled = false;

    const bootstrap = async (): Promise<void> => {
      let bridge = getChatThreadDesktopBridge();

      // Full renderer reload có thể dựng React sớm hơn bridge ở một số vòng reload.
      // Chờ bridge thay vì kết luận app không có dữ liệu từ lần đọc đầu tiên.
      for (let attempt = 0; !bridge && attempt < 50; attempt += 1) {
        await new Promise((resolve) => setTimeout(resolve, 20));
        if (cancelled) return;
        bridge = getChatThreadDesktopBridge();
      }

      if (!bridge || cancelled) return;

      // Durable history là bootstrap lõi. Không để API phụ mới hơn preload/main
      // làm fail toàn bộ UI sau Ctrl+R trong lúc dev.
      const [normalConversations, repositories] = await Promise.all([
        bridge.listConversations(),
        bridge.listRepositories(),
      ]);
      if (cancelled) return;

      let requests: readonly ConversationRequestSnapshot[] = [];
      if (typeof bridge.listActiveChatRequests === "function") {
        requests = await bridge.listActiveChatRequests().catch((error) => {
          console.error("[App] restore active requests failed", error);
          return [];
        });
        if (cancelled) return;
        setRequestSnapshots(requests);
      }

      if (typeof bridge.getSelectedModel === "function") {
        const persistedModel = await bridge.getSelectedModel().catch((error) => {
          console.error("[App] restore selected model failed", error);
          return null;
        });
        if (cancelled) return;

        let initialModel: ModelSelection | null = persistedModel;
        if (typeof bridge.listModelRegistry === "function") {
          const registry = await bridge.listModelRegistry().catch((error) => {
            console.error("[App] load model registry failed", error);
            return null;
          });
          if (cancelled) return;

          if (registry) {
            const registered = (selection: ModelSelection | null): boolean =>
              Boolean(
                selection &&
                  registry.groups.some(
                    (group) =>
                      group.provider === selection.provider &&
                      group.models.some((item) => item.model === selection.model),
                  ),
              );
            if (!registered(initialModel)) {
              initialModel = registry.defaultModel;
            }
          }
        }

        if (initialModel) {
          setSelectedModel(initialModel);
          const changedFromPersisted =
            !persistedModel ||
            persistedModel.provider !== initialModel.provider ||
            persistedModel.model !== initialModel.model;
          if (
            changedFromPersisted &&
            typeof bridge.setSelectedModel === "function"
          ) {
            await bridge.setSelectedModel(initialModel).catch((error) => {
              console.error("[App] persist registry default model failed", error);
            });
            if (cancelled) return;
          }
        }
      }

      // Navigator cũng phải đọc lại registry sau mỗi full renderer reload.
      setNavigatorRevision((current) => current + 1);

      let target: { repositoryPath: string; conversationId: string } | null =
        null;
      const normalConversation = normalConversations.find((item) => item.user);
      if (normalConversation) {
        target = {
          repositoryPath: NORMAL_CONVERSATION_SCOPE,
          conversationId: normalConversation.id,
        };
      } else {
        for (const repository of repositories) {
          const conversation = repository.conversations.find(
            (item) => item.user,
          );
          if (!conversation) continue;
          target = {
            repositoryPath: repository.repositoryPath,
            conversationId: conversation.id,
          };
          break;
        }
      }

      if (!target) {
        readRequestRef.current += 1;
        activeConversationRef.current = null;
        setActiveConversation(null);
        setHistoryRows([]);
        setContextUsage(null);
        clearProviderNotice();
        return;
      }

      const requestId = ++readRequestRef.current;
      activeConversationRef.current = target;
      setActiveConversation(target);

      const rows = await bridge.readConversation(
        target.repositoryPath,
        target.conversationId,
      );
      const usage = await bridge
        .readConversationContextUsage(
          target.repositoryPath,
          target.conversationId,
        )
        .catch(() => null);
      if (cancelled || readRequestRef.current !== requestId) return;

      setHistoryRows((current) => mergeHistoryRows(rows, current));
      setContextUsage(usage);
    };

    void bootstrap().catch((error) => {
      if (cancelled) return;
      console.error("[App] restore active conversation failed", error);
    });

    return () => {
      cancelled = true;
    };
  }, []);

  const onConversationDeleted = (
    repositoryPath: string,
    conversationId: string,
  ): void => {
    if (
      activeConversation?.repositoryPath !== repositoryPath ||
      activeConversation.conversationId !== conversationId
    ) {
      return;
    }

    readRequestRef.current += 1;
    activeConversationRef.current = null;
    setActiveConversation(null);
    setHistoryRows([]);
    setContextUsage(null);
    clearProviderNotice();
  };

  const createConversation = async (
    repositoryPath: string,
  ): Promise<{ id: string; title: string }> => {
    const bridge = getChatThreadDesktopBridge();
    if (!bridge) {
      throw new Error("Chat thread desktop bridge chưa sẵn sàng.");
    }

    readRequestRef.current += 1;
    activeConversationRef.current = null;
    setActiveConversation(null);
    setHistoryRows([]);
    setContextUsage(null);
    clearProviderNotice();

    const conversation = await bridge.addConversation(repositoryPath);
    await bridge.setActiveConversation(repositoryPath, conversation.id);

    const nextActive = {
      repositoryPath,
      conversationId: conversation.id,
    };
    activeConversationRef.current = nextActive;
    setActiveConversation(nextActive);
    setHistoryRows([]);
    setContextUsage(null);
    clearProviderNotice();
    setNavigatorRevision((current) => current + 1);
    return {
      id: conversation.id,
      title: conversation.id,
    };
  };

  const submitMessage = async ({
    text,
    model,
  }: ComposerSubmitInput): Promise<void> => {
    const bridge = getChatThreadDesktopBridge();
    if (!bridge) {
      throw new Error("Chat thread desktop bridge chưa sẵn sàng.");
    }

    let target = activeConversationRef.current;
    if (!target) {
      await createConversation(NORMAL_CONVERSATION_SCOPE);
      target = activeConversationRef.current;
      if (!target) {
        throw new Error("Không tạo được conversation thường.");
      }
    }

    await sendActiveConversationChatRequest({
      provider: model.provider,
      model: model.model,
      prompt: text,
      stream: true,
      reasoningHistory,
    });
  };

  const changeSelectedModel = (selection: ModelSelection): void => {
    setSelectedModel(selection);

    const bridge = getChatThreadDesktopBridge();
    if (!bridge || typeof bridge.setSelectedModel !== "function") return;

    void bridge.setSelectedModel(selection).catch((error) => {
      console.error("[App] persist selected model failed", error);
    });
  };

  const cancelMessage = async (): Promise<void> => {
    if (!activeConversation) return;

    const bridge = getChatThreadDesktopBridge();
    if (!bridge) {
      throw new Error("Chat thread desktop bridge chưa sẵn sàng.");
    }

    const request = requestSnapshots.find(
      (item) =>
        item.repositoryPath === activeConversation.repositoryPath &&
        item.conversationId === activeConversation.conversationId,
    );
    if (!request) return;

    await bridge.cancelChatRequest(request.requestId);
  };

  const activeRequest = activeConversation
    ? requestSnapshots.find(
        (request) =>
          request.repositoryPath === activeConversation.repositoryPath &&
          request.conversationId === activeConversation.conversationId,
      )
    : undefined;

  return (
    <div
      style={{
        position: "relative",
        width: "100%",
        height: "100%",
        margin: 0,
        padding: 0,
        overflow: "hidden",
        background: "#ffffff",
      }}
    >
      <ChatContainer
        entries={entries}
        entriesPerPanel={5}
        maxPanelsInRam={10}
        showThinking={showThinking}
        notice={providerNotice}
        conversationNavigator={{
          ...NAVIGATOR_BASE,
          activeRepositoryPath: activeConversation?.repositoryPath ?? null,
          activeConversationId: activeConversation?.conversationId ?? null,
          refreshKey: navigatorRevision,
          onConversationCreate: createConversation,
          onConversationSelect: openConversation,
          onConversationDeleted,
        }}
      >
        {!activeConversation && (
          <EmptyConversationState
            title="Homegrowh Harness"
            onConversationCreate={async (repositoryPath) => {
              await createConversation(repositoryPath);
            }}
            onConversationSelect={openConversation}
            onRepositoryAdded={() =>
              setNavigatorRevision((current) => current + 1)
            }
          />
        )}
        <Composer
          disabled={false}
          running={Boolean(activeRequest)}
          model={selectedModel ?? undefined}
          onModelChange={changeSelectedModel}
          showThinking={showThinking}
          onShowThinkingChange={setShowThinking}
          onSubmit={submitMessage}
          onCancel={cancelMessage}
          contextProgress={(contextUsage?.usageRatio ?? 0) * 100}
          contextLabel={
            contextUsage
              ? `Context ước tính ${contextUsage.estimatedTokens.toLocaleString()} / ${contextUsage.contextLimitTokens.toLocaleString()} token (${contextUsage.characters.toLocaleString()} ký tự)`
              : "Context chưa được tính"
          }
          reasoningHistory={reasoningHistory}
          onReasoningHistoryChange={setReasoningHistory}
        />
      </ChatContainer>
    </div>
  );
}
