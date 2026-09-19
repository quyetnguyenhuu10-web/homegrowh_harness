import { useEffect, useMemo, useRef, useState } from "react";
import {
  DEEPSEEK_MODELS,
  OLLAMA_MODELS,
  OPENAI_MODELS,
  type ModelInfo,
} from "@homegrowh/provider/models";
import type { ProviderName } from "@homegrowh/provider";

import { acquireStyleTag, releaseStyleTag } from "../style_tag";

import cssText from "./style.css?inline";

export interface ModelSelection {
  provider: ProviderName;
  model: string;
}

export interface PickerModelProps {
  value?: ModelSelection;
  defaultValue?: ModelSelection;
  onChange?: (selection: ModelSelection) => void;
  disabled?: boolean;
}

interface ModelGroup {
  provider: ProviderName;
  label: string;
  models: readonly ModelInfo[];
}

const STYLE_KEY = "picker-model";

const MODEL_GROUPS: readonly ModelGroup[] = [
  { provider: "openai", label: "OpenAI", models: OPENAI_MODELS },
  { provider: "deepseek", label: "DeepSeek", models: DEEPSEEK_MODELS },
  { provider: "ollama", label: "Ollama", models: OLLAMA_MODELS },
];

const FIRST_MODEL: ModelSelection | undefined = (() => {
  for (const group of MODEL_GROUPS) {
    const first = group.models[0];
    if (first) return { provider: group.provider, model: first.id };
  }
  return undefined;
})();

function sameSelection(
  left: ModelSelection | undefined,
  right: ModelSelection | undefined,
): boolean {
  return left?.provider === right?.provider && left?.model === right?.model;
}

function isRegistered(selection: ModelSelection | undefined): boolean {
  if (!selection) return false;
  const group = MODEL_GROUPS.find(
    (candidate) => candidate.provider === selection.provider,
  );
  return group?.models.some((model) => model.id === selection.model) ?? false;
}

function findModel(selection: ModelSelection | undefined): ModelInfo | undefined {
  if (!selection) return undefined;
  return MODEL_GROUPS
    .find((group) => group.provider === selection.provider)
    ?.models.find((model) => model.id === selection.model);
}

/**
 * Unified dropup over every model registered in provider/src/models.ts.
 * It only selects { provider, model }; it does not invoke any AI API.
 */
export default function PickerModel({
  value,
  defaultValue,
  onChange,
  disabled = false,
}: PickerModelProps) {
  const fallback =
    (isRegistered(defaultValue) ? defaultValue : undefined) ?? FIRST_MODEL;

  const [inner, setInner] = useState<ModelSelection | undefined>(
    (isRegistered(value) ? value : undefined) ?? fallback,
  );
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement>(null);

  const selected =
    (value && isRegistered(value) ? value : undefined) ?? inner ?? fallback;
  const selectedInfo = useMemo(() => findModel(selected), [selected]);

  useEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);
    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  useEffect(() => {
    if (!open) return;

    const onPointerDown = (event: PointerEvent) => {
      if (rootRef.current && !rootRef.current.contains(event.target as Node)) {
        setOpen(false);
      }
    };

    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") setOpen(false);
    };

    document.addEventListener("pointerdown", onPointerDown);
    document.addEventListener("keydown", onKeyDown);

    return () => {
      document.removeEventListener("pointerdown", onPointerDown);
      document.removeEventListener("keydown", onKeyDown);
    };
  }, [open]);

  const choose = (next: ModelSelection): void => {
    if (!sameSelection(next, selected)) {
      setInner(next);
      onChange?.(next);
    }
    setOpen(false);
  };

  return (
    <div className="ct-modelpick" ref={rootRef}>
      <button
        type="button"
        className="ct-modelpick__trigger"
        disabled={disabled}
        aria-haspopup="listbox"
        aria-expanded={open}
        onClick={() => setOpen((current) => !current)}
        title={selectedInfo?.label ?? "Chọn mô hình"}
      >
        <span className="ct-modelpick__trigger-label">
          {selectedInfo?.label ?? "Mô hình"}
        </span>
        <ChevronUp open={open} />
      </button>

      {open && !disabled && (
        <div className="ct-modelpick__dropup" role="listbox">
          {MODEL_GROUPS.map((group) => (
            <section className="ct-modelpick__group" key={group.provider}>
              <div className="ct-modelpick__group-label">{group.label}</div>

              {group.models.map((model) => {
                const option: ModelSelection = {
                  provider: group.provider,
                  model: model.id,
                };
                const active = sameSelection(option, selected);

                return (
                  <button
                    key={`${group.provider}:${model.id}`}
                    type="button"
                    role="option"
                    aria-selected={active}
                    className={
                      active
                        ? "ct-modelpick__option ct-modelpick__option--active"
                        : "ct-modelpick__option"
                    }
                    onClick={() => choose(option)}
                  >
                    <span className="ct-modelpick__option-copy">
                      <span className="ct-modelpick__option-label">
                        {model.label}
                      </span>
                    </span>

                    <span
                      className="ct-modelpick__check"
                      aria-hidden="true"
                    >
                      ✓
                    </span>
                  </button>
                );
              })}
            </section>
          ))}
        </div>
      )}
    </div>
  );
}

function ChevronUp({ open }: { open: boolean }) {
  return (
    <svg
      className={
        open
          ? "ct-modelpick__chevron ct-modelpick__chevron--open"
          : "ct-modelpick__chevron"
      }
      viewBox="0 0 24 24"
      aria-hidden="true"
    >
      <path d="m7 14 5-5 5 5" />
    </svg>
  );
}
