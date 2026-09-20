import { useLayoutEffect } from "react";

import { acquireStyleTag, releaseStyleTag } from "../../style_tag/renderer";

import cssText from "./style.css?inline";

export interface HistoryEntry {
  id: string;
  type: string;
  role: string | null;
  text?: string;
  apiSession?: string | null;
  eventIndex?: number | null;
  sourceRowIds: number[];
  streaming?: boolean;
  /** Entry này nối sát entry kế tiếp về mặt ngữ nghĩa, dù bị chia khác panel. */
  compactAfter?: boolean;
}

export interface ViewHistoryProps {
  /** Logical history entries owned by this resident panel. */
  entries: readonly HistoryEntry[];

  /** Stable logical panel index supplied by ChatContainer. */
  panelIndex: number;

  /** Whether this is the final logical panel in the conversation. */
  isLastPanel?: boolean;

  /** Chỉ điều khiển render thinking; dữ liệu thinking vẫn được giữ ở caller. */
  showThinking?: boolean;
}

const STYLE_KEY = "view-history";

type ViewHistoryVisualKind =
  | "assistant"
  | "thinking"
  | "tool-call"
  | "tool-result"
  | "user";

function normalizeEntryType(type: string): string {
  return type.trim().toLowerCase().replace(/[\s_-]+/g, "");
}

function getVisualKind(entry: HistoryEntry): ViewHistoryVisualKind {
  const type = normalizeEntryType(entry.type);

  if (type === "thinking" || type === "reasoning") return "thinking";
  if (type === "toolcall") return "tool-call";
  if (type === "toolresult" || type === "toolresults") return "tool-result";
  if (entry.role === "user") return "user";
  return "assistant";
}

/**
 * History panel visual shell.
 *
 * Geometry is ported from md_ai ChatThread/ChatRow. This component deliberately
 * contains no storage, paging, markdown, copy, append or tool-row behavior yet.
 */
export default function ViewHistory({
  entries,
  panelIndex,
  isLastPanel = false,
  showThinking = true,
}: ViewHistoryProps) {
  useLayoutEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);
    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  return (
    <section
      className="ct-view-history"
      data-panel-index={panelIndex}
      data-panel-last={isLastPanel ? "true" : "false"}
    >
      {entries.map((entry) => {
        const normalizedType = normalizeEntryType(entry.type);
        if (!showThinking && (normalizedType === "thinking" || normalizedType === "reasoning")) return null;

        const visualKind = getVisualKind(entry);

        return (
          <div
            key={entry.id}
            className={`ct-view-history__row ct-view-history__row--${visualKind}${entry.compactAfter ? " ct-view-history__row--compact-after" : ""}`}
            data-msg-id={entry.id}
            data-entry-type={entry.type}
            data-event-index={entry.eventIndex ?? undefined}
            data-entry-kind={visualKind}
          >
            <article
              className={`ct-view-history__message ct-view-history__message--${entry.role ?? "entry"} ct-view-history__message-kind--${visualKind}`}
            >
            {entry.role === "user" ? (
              <div className="ct-view-history__user-cluster">
                <div className="ct-view-history__user-content">
                  {entry.text ?? ""}
                </div>
              </div>
            ) : entry.role === "assistant" ? (
              visualKind === "thinking" ? (
                entry.text ? (
                  <details
                    className={
                      entry.streaming
                        ? "ct-view-history__thinking-bubble ct-view-history__thinking--streaming"
                        : "ct-view-history__thinking-bubble"
                    }
                    open
                  >
                    <summary className="ct-view-history__thinking-summary">
                      <svg
                        className="ct-view-history__thinking-chevron"
                        viewBox="0 0 24 24"
                        aria-hidden="true"
                      >
                        <path d="m9 18 6-6-6-6" />
                      </svg>
                      <span
                        className="ct-view-history__thinking-title"
                        data-text="Thinking"
                      >
                        Thinking
                      </span>
                    </summary>
                    <div className="ct-view-history__thinking-content">
                      <div className="ct-view-history__thinking-body">
                        {entry.text}
                      </div>
                    </div>
                  </details>
                ) : (
                  <div className="ct-view-history__assistant-thinking ct-view-history__thinking--streaming">
                    <span
                      className="ct-view-history__thinking-title"
                      data-text="Thinking"
                    >
                      Thinking
                    </span>
                  </div>
                )
              ) : (
                <div className="ct-view-history__assistant-content">
                  {entry.text ?? ""}
                </div>
              )
            ) : (
              <div className="ct-view-history__entry-content">
                <div className="ct-view-history__entry-type">{entry.type}</div>
                <div className="ct-view-history__entry-text">{entry.text ?? ""}</div>
              </div>
            )}
            </article>
          </div>
        );
      })}
    </section>
  );
}
