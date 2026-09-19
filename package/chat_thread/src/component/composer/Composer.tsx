import { useEffect } from "react";

import { acquireStyleTag, releaseStyleTag } from "../style_tag";
import PickerModel, {
  type ModelSelection,
} from "../picker_model/PickerModel";

import cssText from "./style.css?inline";

export interface ComposerProps {
  placeholder?: string;
  model?: ModelSelection;
  defaultModel?: ModelSelection;
  onModelChange?: (selection: ModelSelection) => void;
}

const STYLE_KEY = "composer";

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
}: ComposerProps) {
  useEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);
    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  return (
    <section className="ct-composer" aria-label="Soạn tin nhắn">
      <textarea
        className="ct-composer__input"
        aria-label="Nội dung tin nhắn"
        name="message"
        autoComplete="off"
        placeholder={placeholder}
        rows={1}
      />

      <div className="ct-composer__footer">
        <button
          className="ct-composer__add"
          type="button"
          aria-label="Thêm"
          title="Thêm"
        >
          <svg viewBox="0 0 24 24" aria-hidden="true">
            <path d="M12 5v14M5 12h14" />
          </svg>
        </button>

        <div className="ct-composer__model-cluster">
          <span
            className="ct-composer__context-meter"
            aria-hidden="true"
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
            value={model}
            defaultValue={defaultModel}
            onChange={onModelChange}
          />
        </div>

        <button
          className="ct-composer__send"
          type="button"
          aria-label="Gửi tin nhắn"
          title="Gửi tin nhắn"
        >
          <svg viewBox="0 0 24 24" aria-hidden="true">
            <path d="M12 19V5" />
            <path d="m6.5 10.5 5.5-5.5 5.5 5.5" />
          </svg>
        </button>
      </div>
    </section>
  );
}
