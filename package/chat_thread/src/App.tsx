import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import ChatContainer from "./component/chat_container/renderer";
import Composer, {
  type ComposerSubmitInput,
} from "./component/composer/renderer";
import EmptyConversationState from "./component/empty_conversation_state/renderer";
import type { ModelSelection } from "./component/picker_model/renderer";
import {
  getChatThreadDesktopBridge,
  NORMAL_CONVERSATION_SCOPE,
  type ConversationSessionSnapshot,
  type HistoryContextUsage,
  type HistoryRow,
  type ReasoningHistoryPolicy,
} from "./component/history_conversation/renderer";
import { mergeHistoryRows } from "./component/history_conversation/renderer/history_rows";
import { projectHistoryEntries } from "./component/view_history/renderer";
import type { HistoryEntry } from "./component/view_history/renderer";

const NAVIGATOR_BASE = {
  title: "Homegrowh Harness",
  conversations: [],
  projects: [],
} as const;

type SessionVisualPhase = "waiting" | "reasoning" | "other";

interface CompactionDebugState {
  repositoryPath: string;
  conversationId: string;
  sessionId: string;
  requestId: string;
  text: string;
  streaming: boolean;
}

export default function App() {
  const [historyRows, setHistoryRows] = useState<readonly HistoryRow[]>([]);
  const [showThinking, setShowThinking] = useState(true);
  const [navigatorRevision, setNavigatorRevision] = useState(0);
  const [selectedModel, setSelectedModel] =
    useState<ModelSelection | null>(null);
  const [sessionSnapshots, setSessionSnapshots] = useState<
    readonly ConversationSessionSnapshot[]
  >([]);
  const [providerNotice, setProviderNotice] = useState<string | null>(null);
  const [compactionDebug, setCompactionDebug] =
    useState<CompactionDebugState | null>(null);
  const [reasoningHistory, setReasoningHistory] =
    useState<ReasoningHistoryPolicy>({ mode: "recent", requestCount: 5 });
  const [contextUsage, setContextUsage] = useState<HistoryContextUsage | null>(
    null,
  );
  const [sessionVisualPhases, setSessionVisualPhases] = useState<
    Readonly<Record<string, SessionVisualPhase>>
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
    const debugEntry: HistoryEntry | null =
      compactionDebug &&
      compactionDebug.repositoryPath === activeConversation.repositoryPath &&
      compactionDebug.conversationId === activeConversation.conversationId
        ? {
            id: `compaction-debug:${compactionDebug.sessionId}:${compactionDebug.requestId}`,
            type: "compaction-debug",
            role: "assistant",
            text: compactionDebug.text,
            label: "Compaction debug",
            sourceRowPositions: [],
            streaming: compactionDebug.streaming,
          }
        : null;
    const activeSession = sessionSnapshots.find(
      (sessionSnapshot) =>
        sessionSnapshot.repositoryPath === activeConversation.repositoryPath &&
        sessionSnapshot.conversationId === activeConversation.conversationId,
    );
    if (!activeSession) return debugEntry ? [...projected, debugEntry] : projected;

    const phase = sessionVisualPhases[activeSession.sessionId] ?? "waiting";
    if (phase === "waiting") {
      if (debugEntry) return [...projected, debugEntry];
      return [
        ...projected,
        {
          id: `thinking:${activeSession.sessionId}`,
          type: "thinking",
          role: "assistant",
          sourceRowPositions: [],
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

    return debugEntry ? [...projected, debugEntry] : projected;
  }, [
    activeConversation,
    compactionDebug,
    historyRows,
    sessionSnapshots,
    sessionVisualPhases,
  ]);

  useEffect(() => {
    const bridge = getChatThreadDesktopBridge();
    if (!bridge) return;

    const removeRow = bridge.onConversationRow((event) => {
      rowQueryTailRef.current = rowQueryTailRef.current
        .catch(() => {})
        .then(async () => {
          const row = event.row;
          if (row.rowPosition !== event.rowPosition) {
            throw new Error("Conversation row event có rowPosition không nhất quán.");
          }

          const active = activeConversationRef.current;
          if (
            active?.repositoryPath !== event.repositoryPath ||
            active.conversationId !== event.conversationId
          ) {
            return;
          }

          if (row.type === "compaction") {
            setHistoryRows(
              await bridge.readConversation(event.repositoryPath, event.conversationId),
            );
          } else {
            setHistoryRows((current) => mergeHistoryRows(current, [row]));
          }
          if (event.sessionId) {
            if (row.type === "reasoning" && row.role === "assistant") {
              setSessionVisualPhases((current) => ({
                ...current,
                [event.sessionId!]: "reasoning",
              }));
            } else if (
              (row.type === "reply" || row.type === "toolcall") &&
              row.role === "assistant"
            ) {
              setSessionVisualPhases((current) => ({
                ...current,
                [event.sessionId!]: "other",
              }));
            } else if (row.type === "toolresult") {
              setSessionVisualPhases((current) => ({
                ...current,
                [event.sessionId!]: "waiting",
              }));
            }
          }
        })
        .catch((error) => {
          console.error("[App] read appended history row failed", error);
        });
    });

    const removeSessionState =
      typeof bridge.onChatSessionState === "function"
        ? bridge.onChatSessionState((event) => {
            rowQueryTailRef.current = rowQueryTailRef.current.catch(() => {}).then(() => {
            if (event.active) {
              setCompactionDebug((current) =>
                current &&
                current.repositoryPath === event.session.repositoryPath &&
                current.conversationId === event.session.conversationId
                  ? null
                  : current,
              );
              setSessionSnapshots((current) => [
                  ...current.filter(
                    (sessionSnapshot) =>
                      sessionSnapshot.sessionId !== event.session.sessionId,
                  ),
                  event.session,
                ]);
                setSessionVisualPhases((current) => ({
                  ...current,
                  [event.session.sessionId]: "waiting",
                }));
                return;
              }

              setSessionSnapshots((current) =>
                current.filter(
                  (sessionSnapshot) => sessionSnapshot.sessionId !== event.sessionId,
                ),
              );
              setSessionVisualPhases((current) => {
                const next = { ...current };
                delete next[event.sessionId];
                return next;
              });
            });
          })
        : () => {};

    const removeCompactionDebug =
      typeof bridge.onCompactionDebug === "function"
        ? bridge.onCompactionDebug((event) => {
            setCompactionDebug((current) => {
              if (event.phase === "done") {
                if (
                  !current ||
                  current.sessionId !== event.sessionId ||
                  current.requestId !== event.requestId
                ) {
                  return current;
                }
                return { ...current, streaming: false };
              }

              const delta = event.delta ?? "";
              if (
                current &&
                current.sessionId === event.sessionId &&
                current.requestId === event.requestId
              ) {
                return {
                  ...current,
                  text: current.text + delta,
                  streaming: true,
                };
              }

              return {
                repositoryPath: event.repositoryPath,
                conversationId: event.conversationId,
                sessionId: event.sessionId,
                requestId: event.requestId,
                text: delta,
                streaming: true,
              };
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
            if (
              !usage ||
              usage.sessionId !== event.sessionId ||
              usage.requestId !== event.requestId
            ) return;

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
      removeSessionState();
      removeCompactionDebug();
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
      setCompactionDebug(null);

      void bridge.setActiveConversation(repositoryPath, conversationId)
        .then(async () => {
          const rows = await bridge.readConversation(
            repositoryPath,
            conversationId,
          );
          const sessions =
            typeof bridge.listActiveChatSessions === "function"
              ? await bridge.listActiveChatSessions().catch(() => [])
              : [];
          const usage = await bridge
            .readConversationContextUsage(repositoryPath, conversationId)
            .catch(() => null);
          return [rows, sessions, usage] as const;
        })
        .then(([rows, sessions, usage]) => {
          if (readRequestRef.current !== requestId) return;

          setSessionSnapshots(sessions);
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

      let sessions: readonly ConversationSessionSnapshot[] = [];
      if (typeof bridge.listActiveChatSessions === "function") {
        sessions = await bridge.listActiveChatSessions().catch((error) => {
          console.error("[App] restore active sessions failed", error);
          return [];
        });
        if (cancelled) return;
        setSessionSnapshots(sessions);
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
      const normalConversation = normalConversations.find((item) => item.active);
      if (normalConversation) {
        target = {
          repositoryPath: NORMAL_CONVERSATION_SCOPE,
          conversationId: normalConversation.id,
        };
      } else {
        for (const repository of repositories) {
          const conversation = repository.conversations.find(
            (item) => item.active,
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

  const submitMessage = async ({ text }: ComposerSubmitInput): Promise<void> => {
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

    await bridge.sendChatRequest({
      repositoryPath: target.repositoryPath,
      conversationId: target.conversationId,
      prompt: text,
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

    const activeSession = sessionSnapshots.find(
      (item) =>
        item.repositoryPath === activeConversation.repositoryPath &&
        item.conversationId === activeConversation.conversationId,
    );
    if (!activeSession) return;

    await bridge.cancelChatSession(activeSession.sessionId);
  };

  const activeSession = activeConversation
    ? sessionSnapshots.find(
        (sessionSnapshot) =>
          sessionSnapshot.repositoryPath === activeConversation.repositoryPath &&
          sessionSnapshot.conversationId === activeConversation.conversationId,
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
          running={Boolean(activeSession)}
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
