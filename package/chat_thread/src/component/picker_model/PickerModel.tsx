import { useEffect, useRef, useState } from "react";
import { listModels } from "@homegrowh/provider/models";
import type { ProviderName } from "@homegrowh/provider";
import { acquireStyleTag, releaseStyleTag } from "../style_tag";
import cssText from "./style.css?inline";

export interface PickerModelProps {
  /** Chọn model trong registry của provider này. */
  provider: ProviderName;
  /** Controlled value. */
  value?: string;
  /** Giá trị khởi đầu khi uncontrolled (mặc định: model đầu registry). */
  defaultValue?: string;
  onChange?: (model: string) => void;
  disabled?: boolean;
}

const STYLE_KEY = "picker-model";

// Dropup chọn model: nút hiện model đang chọn, bấm mở panel ngược
// lên trên (vì composer neo đáy) liệt kê đủ id trong registry provider.
// Đổi provider mà model đang chọn không còn thì tự rơi về đầu danh sách.
export default function PickerModel({
  provider,
  value,
  defaultValue,
  onChange,
  disabled,
}: PickerModelProps) {
  const models = listModels(provider);
  const [inner, setInner] = useState(
    value ?? defaultValue ?? models[0] ?? "",
  );
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement>(null);
  const selected = value ?? inner;

  useEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);
    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  useEffect(() => {
    if (!models.includes(selected)) {
      const first = models[0] ?? "";
      setInner(first);
      onChange?.(first);
    }
    // Chỉ chạy khi đổi provider.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [provider]);

  // Bấm ra ngoài / Escape thì đóng panel.
  useEffect(() => {
    if (!open) return;
    const onPointerDown = (e: PointerEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) {
        setOpen(false);
      }
    };
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === "Escape") setOpen(false);
    };
    document.addEventListener("pointerdown", onPointerDown);
    document.addEventListener("keydown", onKeyDown);
    return () => {
      document.removeEventListener("pointerdown", onPointerDown);
      document.removeEventListener("keydown", onKeyDown);
    };
  }, [open ]);

  const choose = (model: string) => {
    setInner(model);
    onChange?.(model);
    setOpen(false);
  };

  return (
    <div className="ct-modelpick" ref={rootRef}>
      <button
        type="button"
        className="ct-modelpick__btn"
        disabled={disabled}
        aria-haspopup="listbox"
        aria-expanded={open}
        onClick={() => setOpen((v) => !v)}
        title={selected}
      >
        {selected || "Chọn model"} ▴
      </button>
      {open && !disabled && (
        <ul className="ct-modelpick__panel" role="listbox">
          {models.map((m) => (
            <li key={m} role="option" aria-selected={m === selected}>
              <button
                type="button"
                className={
                  m === selected
                    ? "ct-modelpick__item ct-modelpick__item--active"
                    : "ct-modelpick__item"
                }
                onClick={() => choose(m)}
              >
                {m}
              </button>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
