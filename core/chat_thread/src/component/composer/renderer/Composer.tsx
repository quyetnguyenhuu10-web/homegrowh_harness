import { useEffect, useLayoutEffect, useRef, useState } from "react";
import type { CSSProperties } from "react";

import { acquireStyleTag, releaseStyleTag } from "../../style_tag/renderer";
import PickerModel, {
  type ModelSelection,
} from "../../picker_model/renderer";
import type { ReasoningHistoryPolicy } from "../../history_conversation/renderer";

import cssText from "./style.css?inline";

export interface ComposerProps {
  placeholder?: string;
  model?: ModelSelection;
  defaultModel?: ModelSelection;
  onModelChange?: (selection: ModelSelection) => void;
  onSubmit?: (input: ComposerSubmitInput) => void | Promise<void>;
  onCancel?: () => void | Promise<void>;
  showThinking?: boolean;
  onShowThinkingChange?: (show: boolean) => void;
  running?: boolean;
  disabled?: boolean;
  /** Mức context 0..100 của một model request. */
  contextProgress?: number;
  contextLabel?: string;
  reasoningHistory?: ReasoningHistoryPolicy;
  onReasoningHistoryChange?: (policy: ReasoningHistoryPolicy) => void;
}

export interface ComposerSubmitInput {
  text: string;
  model: ModelSelection;
}

const STYLE_KEY = "composer";
const DEFAULT_REASONING_HISTORY: ReasoningHistoryPolicy = {
  mode: "recent",
  requestCount: 5,
};

/**
 * Composer visual shell only.
 *
 * Geometry is ported from md_ai's ChatComposer. No send/model/sandbox/
 * attachment behavior is connected here yet.
 */
export default function Composer({
  placeholder = "Nhắn cho Chat…",
  model,
  defaultModel,
  onModelChange,
  onSubmit,
  onCancel,
  showThinking = true,
  onShowThinkingChange,
  running = false,
  disabled = false,
  contextProgress = 0,
  contextLabel,
  reasoningHistory = DEFAULT_REASONING_HISTORY,
  onReasoningHistoryChange,
}: ComposerProps) {
  const [text, setText] = useState("");
  const [innerModel, setInnerModel] = useState<ModelSelection | undefined>(
    model ?? defaultModel,
  );
  const [submitting, setSubmitting] = useState(false);
  const [cancelling, setCancelling] = useState(false);
  const [toolsOpen, setToolsOpen] = useState(false);
  const [recentReasoningCount, setRecentReasoningCount] = useState(
    reasoningHistory.mode === "recent" ? reasoningHistory.requestCount : 5,
  );
  const toolsRef = useRef<HTMLDivElement | null>(null);
  const toolsPanelRef = useRef<HTMLDivElement | null>(null);
  const selectedModel = model ?? innerModel ?? defaultModel;
  const safeContextProgress = Math.max(
    0,
    Math.min(100, Number.isFinite(contextProgress) ? contextProgress : 0),
  );

  useLayoutEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);
    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  useEffect(() => {
    if (reasoningHistory.mode === "recent") {
      setRecentReasoningCount(reasoningHistory.requestCount);
    }
  }, [reasoningHistory]);

  useEffect(() => {
    if (!toolsOpen) return;

    const onPointerDown = (event: PointerEvent): void => {
      const root = toolsRef.current;
      const panel = toolsPanelRef.current;
      const target = event.target as Node;
      if (root?.contains(target) || panel?.contains(target)) return;
      setToolsOpen(false);
    };

    const onKeyDown = (event: KeyboardEvent): void => {
      if (event.key === "Escape") setToolsOpen(false);
    };

    document.addEventListener("pointerdown", onPointerDown);
    document.addEventListener("keydown", onKeyDown);
    return () => {
      document.removeEventListener("pointerdown", onPointerDown);
      document.removeEventListener("keydown", onKeyDown);
    };
  }, [toolsOpen]);

  const handleModelChange = (selection: ModelSelection): void => {
    if (model === undefined) setInnerModel(selection);
    onModelChange?.(selection);
  };

  const submit = async (): Promise<void> => {
    const prompt = text.trim();
    if (
      !prompt ||
      !selectedModel ||
      disabled ||
      submitting ||
      running ||
      !onSubmit
    ) {
      return;
    }

    setSubmitting(true);
    try {
      await onSubmit({ text: prompt, model: selectedModel });
      setText("");
    } catch (error) {
      console.error("[Composer] send failed", error);
    } finally {
      setSubmitting(false);
    }
  };

  const cancel = async (): Promise<void> => {
    if (!running || cancelling || !onCancel) return;

    setCancelling(true);
    try {
      await onCancel();
    } catch (error) {
      console.error("[Composer] cancel failed", error);
    } finally {
      setCancelling(false);
    }
  };

  return (
    <section className="ct-composer" aria-label="Soạn tin nhắn">
      <textarea
        className="ct-composer__input"
        aria-label="Nội dung tin nhắn"
        name="message"
        autoComplete="off"
        placeholder={placeholder}
        rows={1}
        value={text}
        disabled={disabled || submitting}
        onChange={(event) => setText(event.target.value)}
        onKeyDown={(event) => {
          if (
            event.key !== "Enter" ||
            event.shiftKey ||
            event.nativeEvent.isComposing
          ) {
            return;
          }

          event.preventDefault();
          void submit();
        }}
      />

      <div className="ct-composer__footer">
        <div className="ct-composer__tools" ref={toolsRef}>
          <button
            className="ct-composer__add"
            type="button"
            aria-label="Tùy chọn"
            title="Tùy chọn"
            aria-expanded={toolsOpen}
            onClick={() => setToolsOpen((current) => !current)}
          >
            <svg viewBox="0 0 24 24" aria-hidden="true">
              <path d="M12 5v14M5 12h14" />
            </svg>
          </button>
        </div>

        <div className="ct-composer__model-cluster">
          <span
            className="ct-composer__context-meter"
            role="img"
            aria-label={contextLabel ?? `Context ${safeContextProgress.toFixed(1)}%`}
            title={contextLabel}
            style={
              {
                "--context-progress": safeContextProgress,
              } as CSSProperties
            }
          >
            <svg viewBox="0 0 24 24">
              <circle
                className="ct-composer__context-meter-track"
                cx="12"
                cy="12"
                r="9"
                pathLength="100"
              />
              <circle
                className="ct-composer__context-meter-value"
                cx="12"
                cy="12"
                r="9"
                pathLength="100"
              />
            </svg>
          </span>

          <PickerModel
            value={selectedModel}
            defaultValue={defaultModel}
            onChange={handleModelChange}
            disabled={disabled || submitting || running}
          />
        </div>

        <button
          className="ct-composer__send"
          type="button"
          aria-label={running ? "Hủy phản hồi" : "Gửi tin nhắn"}
          title={running ? "Hủy phản hồi" : "Gửi tin nhắn"}
          disabled={
            disabled ||
            submitting ||
            cancelling ||
            (running
              ? !onCancel
              : !text.trim() || !onSubmit || !selectedModel)
          }
          onClick={() => {
            if (running) {
              void cancel();
            } else {
              void submit();
            }
          }}
        >
          {running ? (
            <svg
              className="ct-composer__cancel-icon"
              viewBox="0 0 24 24"
              aria-hidden="true"
            >
              <rect x="8" y="8" width="8" height="8" rx="1" />
            </svg>
          ) : (
            <svg viewBox="0 0 24 24" aria-hidden="true">
              <path d="M12 19V5" />
              <path d="m6.5 10.5 5.5-5.5 5.5 5.5" />
            </svg>
          )}
        </button>
      </div>

      {toolsOpen && (
        <div
          ref={toolsPanelRef}
          className="ct-composer__tools-panel"
          role="menu"
        >
          <button
            className="ct-composer__tools-item"
            type="button"
            role="menuitemcheckbox"
            aria-checked={showThinking}
            onClick={() => onShowThinkingChange?.(!showThinking)}
          >
            <span className="ct-composer__tools-item-copy">
              <span className="ct-composer__tools-item-title">Thinking</span>
              <span className="ct-composer__tools-item-description">
                {showThinking ? "Đang hiển thị" : "Đang ẩn"}
              </span>
            </span>
            <span
              className={
                showThinking
                  ? "ct-composer__tools-switch is-on"
                  : "ct-composer__tools-switch"
              }
              aria-hidden="true"
            >
              <span className="ct-composer__tools-switch-thumb" />
            </span>
          </button>

          <div className="ct-composer__reasoning-history" role="group" aria-label="Reasoning context">
            <div className="ct-composer__reasoning-history-copy">
              <span className="ct-composer__tools-item-title">Reasoning context</span>
              <span className="ct-composer__tools-item-description">
                Giữ thinking trong context model
              </span>
            </div>

            <div className="ct-composer__reasoning-history-controls">
              <label
                className={
                  reasoningHistory.mode === "recent"
                    ? "ct-composer__reasoning-history-option is-active"
                    : "ct-composer__reasoning-history-option"
                }
              >
                <span>Recent</span>
                <input
                  className="ct-composer__reasoning-history-input"
                  type="number"
                  min={1}
                  step={1}
                  value={recentReasoningCount}
                  disabled={running || submitting}
                  onFocus={() =>
                    onReasoningHistoryChange?.({
                      mode: "recent",
                      requestCount: recentReasoningCount,
                    })
                  }
                  onChange={(event) => {
                    const value = event.currentTarget.valueAsNumber;
                    if (!Number.isFinite(value)) return;
                    const requestCount = Math.max(1, Math.floor(value));
                    setRecentReasoningCount(requestCount);
                    onReasoningHistoryChange?.({ mode: "recent", requestCount });
                  }}
                />
              </label>

              <button
                className={
                  reasoningHistory.mode === "all"
                    ? "ct-composer__reasoning-history-option is-active"
                    : "ct-composer__reasoning-history-option"
                }
                type="button"
                disabled={running || submitting}
                onClick={() => onReasoningHistoryChange?.({ mode: "all" })}
              >
                All
              </button>
            </div>
          </div>
        </div>
      )}
    </section>
  );
}
