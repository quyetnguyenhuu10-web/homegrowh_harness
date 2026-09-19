import { useLayoutEffect } from "react";

import { acquireStyleTag, releaseStyleTag } from "../style_tag";

import cssText from "./style.css?inline";

export interface ViewHistoryMessage {
  id: string;
  role: "user" | "assistant";
  text?: string;
}

export interface ViewHistoryProps {
  /** Messages owned by this resident history panel. */
  messages: readonly ViewHistoryMessage[];

  /** Stable logical panel index supplied by ChatContainer. */
  panelIndex: number;

  /** Whether this is the final logical panel in the conversation. */
  isLastPanel?: boolean;
}

const STYLE_KEY = "view-history";

/**
 * History panel visual shell.
 *
 * Geometry is ported from md_ai ChatThread/ChatRow. This component deliberately
 * contains no storage, paging, markdown, copy, append or tool-row behavior yet.
 */
export default function ViewHistory({
  messages,
  panelIndex,
  isLastPanel = false,
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
      {messages.map((message) => (
        <div
          key={message.id}
          className={`ct-view-history__row ct-view-history__row--${message.role}`}
          data-msg-id={message.id}
        >
          <article
            className={`ct-view-history__message ct-view-history__message--${message.role}`}
          >
            {message.role === "user" ? (
              <div className="ct-view-history__user-cluster">
                <div className="ct-view-history__user-content">
                  {message.text ?? ""}
                </div>
              </div>
            ) : (
              <div className="ct-view-history__assistant-content">
                {message.text ?? ""}
              </div>
            )}
          </article>
        </div>
      ))}
    </section>
  );
}
