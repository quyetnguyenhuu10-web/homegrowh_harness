import { useEffect, useState } from "react";
import type { ProviderName } from "@homegrowh/provider";
import { acquireStyleTag, releaseStyleTag } from "../style_tag";
import PickerModel from "../picker_model/PickerModel";
import cssText from "./style.css?inline";

export interface ComposerProps {
  /** Gọi khi user gửi. Không truyền thì nút chỉ xóa input. */
  onSend?: (text: string) => void;
  placeholder?: string;
  /** Provider để picker nhặt model (mặc định ollama local, khỏi cần key). */
  provider?: ProviderName;
  /** Model đang chọn (controlled). */
  model?: string;
  /** Model khởi đầu khi uncontrolled (mặc định: đầu registry). */
  defaultModel?: string;
  /** Gọi khi user đổi model trong picker. */
  onModelChange?: (model: string) => void;
}

const STYLE_KEY = "composer";

// Overlay tuyệt đối: absolute trong khung App (position: relative),
// không chiếm layout, không portal ra body. CSS gắn qua helper dùng
// chung nên nhiều Composer cũng chỉ 1 thẻ <style>.
export default function Composer({
  onSend,
  placeholder = "Nhập tin nhắn…",
  provider = "ollama",
  model,
  defaultModel,
  onModelChange,
}: ComposerProps) {
  const [value, setValue] = useState("");

  useEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);
    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  const send = () => {
    const text = value.trim();
    if (!text) return;
    setValue("");
    onSend?.(text);
  };

  return (
    <div className="ct-composer">
      <PickerModel
        provider={provider}
        value={model}
        defaultValue={defaultModel}
        onChange={onModelChange}
      />
      <input
        type="text"
        value={value}
        placeholder={placeholder}
        onChange={(e) => setValue(e.target.value)}
        onKeyDown={(e) => {
          if (e.key === "Enter") send();
        }}
        className="ct-composer__input"
      />
      <button
        type="button"
        onClick={send}
        disabled={!value.trim()}
        className="ct-composer__send"
      >
        Send
      </button>
    </div>
  );
}
