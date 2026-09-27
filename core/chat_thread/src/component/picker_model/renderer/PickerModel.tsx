import {
  useCallback,
  useEffect,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import { Ellipsis, Trash2, X } from "lucide-react";

import { getChatThreadDesktopBridge } from "../../history_conversation/renderer";
import { acquireStyleTag, releaseStyleTag } from "../../style_tag/renderer";
import type {
  ModelRegistryItem,
  ModelRegistrySelection,
  ModelRegistrySnapshot,
} from "../model_registry_contract";

import cssText from "./style.css?inline";

export type ModelSelection = ModelRegistrySelection;

export interface PickerModelProps {
  value?: ModelSelection;
  defaultValue?: ModelSelection;
  onChange?: (selection: ModelSelection) => void;
  disabled?: boolean;
}

interface CustomFormState {
  apiKey: string;
  endpoint: string;
  model: string;
  maxContextWindowTokens: string;
}

const STYLE_KEY = "picker-model";
const EMPTY_CUSTOM_FORM: CustomFormState = {
  apiKey: "",
  endpoint: "",
  model: "",
  maxContextWindowTokens: "",
};

function sameSelection(
  left: ModelSelection | undefined | null,
  right: ModelSelection | undefined | null,
): boolean {
  return left?.provider === right?.provider && left?.model === right?.model;
}

function isRegistered(
  registry: ModelRegistrySnapshot | null,
  selection: ModelSelection | undefined | null,
): boolean {
  if (!registry || !selection) return false;
  return registry.groups.some(
    (group) =>
      group.provider === selection.provider &&
      group.models.some((model) => model.model === selection.model),
  );
}

function findModel(
  registry: ModelRegistrySnapshot | null,
  selection: ModelSelection | undefined | null,
): ModelRegistryItem | undefined {
  if (!registry || !selection) return undefined;
  return registry.groups
    .find((group) => group.provider === selection.provider)
    ?.models.find((model) => model.model === selection.model);
}

function firstModel(registry: ModelRegistrySnapshot | null): ModelSelection | undefined {
  if (!registry) return undefined;
  for (const group of registry.groups) {
    const first = group.models[0];
    if (first) return { provider: first.provider, model: first.model };
  }
  return undefined;
}

export default function PickerModel({
  value,
  defaultValue,
  onChange,
  disabled = false,
}: PickerModelProps) {
  const [registry, setRegistry] = useState<ModelRegistrySnapshot | null>(null);
  const [registryError, setRegistryError] = useState<string | null>(null);
  const [inner, setInner] = useState<ModelSelection | undefined>(
    value ?? defaultValue,
  );
  const [open, setOpen] = useState(false);
  const [addingCustom, setAddingCustom] = useState(false);
  const [customForm, setCustomForm] = useState<CustomFormState>(EMPTY_CUSTOM_FORM);
  const [customError, setCustomError] = useState<string | null>(null);
  const [savingCustom, setSavingCustom] = useState(false);
  const [editingCustomModel, setEditingCustomModel] = useState<string | null>(
    null,
  );
  const [customMenuModel, setCustomMenuModel] = useState<string | null>(null);
  const [loadingCustomEdit, setLoadingCustomEdit] = useState(false);
  const [deletingCustomModel, setDeletingCustomModel] = useState<string | null>(
    null,
  );
  const rootRef = useRef<HTMLDivElement>(null);

  const refreshRegistry = useCallback(async (): Promise<ModelRegistrySnapshot> => {
    const bridge = getChatThreadDesktopBridge();
    if (!bridge || typeof bridge.listModelRegistry !== "function") {
      throw new Error("Model registry bridge chưa sẵn sàng.");
    }
    const next = await bridge.listModelRegistry();
    setRegistry(next);
    setRegistryError(null);
    return next;
  }, []);

  useLayoutEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);
    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  useEffect(() => {
    let cancelled = false;
    void refreshRegistry().catch((error) => {
      if (cancelled) return;
      setRegistryError(error instanceof Error ? error.message : String(error));
    });
    return () => {
      cancelled = true;
    };
  }, [refreshRegistry]);

  useEffect(() => {
    if (!open) return;
    void refreshRegistry().catch((error) => {
      setRegistryError(error instanceof Error ? error.message : String(error));
    });
  }, [open, refreshRegistry]);

  const fallback = useMemo(() => {
    if (isRegistered(registry, defaultValue)) return defaultValue;
    if (isRegistered(registry, registry?.defaultModel)) {
      return registry?.defaultModel ?? undefined;
    }
    return firstModel(registry);
  }, [defaultValue, registry]);

  const selected =
    (isRegistered(registry, value) ? value : undefined) ??
    (isRegistered(registry, inner) ? inner : undefined) ??
    fallback ??
    value ??
    inner ??
    defaultValue;
  const selectedInfo = useMemo(
    () => findModel(registry, selected),
    [registry, selected],
  );

  useEffect(() => {
    if (!open && !addingCustom && !customMenuModel) return;

    const onPointerDown = (event: PointerEvent) => {
      if (
        customMenuModel &&
        event.target instanceof Element &&
        !event.target.closest(".ct-modelpick__more-wrap")
      ) {
        setCustomMenuModel(null);
      }
      if (
        open &&
        rootRef.current &&
        !rootRef.current.contains(event.target as Node)
      ) {
        setOpen(false);
      }
    };

    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        if (addingCustom) {
          setAddingCustom(false);
          setEditingCustomModel(null);
          setCustomError(null);
        } else if (customMenuModel) {
          setCustomMenuModel(null);
        } else {
          setOpen(false);
        }
      }
    };

    document.addEventListener("pointerdown", onPointerDown);
    document.addEventListener("keydown", onKeyDown);

    return () => {
      document.removeEventListener("pointerdown", onPointerDown);
      document.removeEventListener("keydown", onKeyDown);
    };
  }, [addingCustom, customMenuModel, open]);

  const choose = (next: ModelSelection): void => {
    if (!sameSelection(next, selected)) {
      setInner(next);
      onChange?.(next);
    }
    setAddingCustom(false);
    setEditingCustomModel(null);
    setCustomMenuModel(null);
    setOpen(false);
  };

  const submitCustom = async (
    event: React.FormEvent<HTMLFormElement>,
  ): Promise<void> => {
    event.preventDefault();
    if (savingCustom) return;

    const maxContextWindowTokens = Number(customForm.maxContextWindowTokens);
    if (!Number.isInteger(maxContextWindowTokens) || maxContextWindowTokens <= 0) {
      setCustomError("Max context window phải là số nguyên lớn hơn 0.");
      return;
    }

    const bridge = getChatThreadDesktopBridge();
    if (
      !bridge ||
      typeof bridge.addCustomModel !== "function" ||
      typeof bridge.updateCustomModel !== "function"
    ) {
      setCustomError("Custom model bridge chưa sẵn sàng.");
      return;
    }

    setSavingCustom(true);
    setCustomError(null);
    try {
      const originalModel = editingCustomModel;
      const created = originalModel
        ? await bridge.updateCustomModel({
            originalModel,
            apiKey: customForm.apiKey || undefined,
            endpoint: customForm.endpoint,
            model: customForm.model,
            maxContextWindowTokens,
          })
        : await bridge.addCustomModel({
            apiKey: customForm.apiKey,
            endpoint: customForm.endpoint,
            model: customForm.model,
            maxContextWindowTokens,
          });
      await refreshRegistry();
      setCustomForm(EMPTY_CUSTOM_FORM);
      setAddingCustom(false);
      setEditingCustomModel(null);
      if (originalModel) {
        if (
          selected?.provider === "custom" &&
          selected.model === originalModel &&
          created.model !== originalModel
        ) {
          const nextSelection = {
            provider: created.provider,
            model: created.model,
          };
          setInner(nextSelection);
          onChange?.(nextSelection);
        }
      } else {
        choose({ provider: created.provider, model: created.model });
      }
    } catch (error) {
      setCustomError(error instanceof Error ? error.message : String(error));
    } finally {
      setSavingCustom(false);
    }
  };

  const openCustomEditor = async (model: ModelRegistryItem): Promise<void> => {
    if (!model.custom || loadingCustomEdit) return;

    const bridge = getChatThreadDesktopBridge();
    if (!bridge || typeof bridge.getCustomModel !== "function") {
      setRegistryError("Custom model bridge chưa hỗ trợ tùy chỉnh.");
      return;
    }

    setLoadingCustomEdit(true);
    setRegistryError(null);
    try {
      const config = await bridge.getCustomModel({ model: model.model });
      setCustomForm({
        apiKey: "",
        endpoint: config.endpoint,
        model: config.model,
        maxContextWindowTokens: String(config.contextWindowTokens),
      });
      setEditingCustomModel(model.model);
      setCustomError(null);
      setCustomMenuModel(null);
      setOpen(false);
      setAddingCustom(true);
    } catch (error) {
      setRegistryError(error instanceof Error ? error.message : String(error));
    } finally {
      setLoadingCustomEdit(false);
    }
  };

  const deleteCustom = async (model: ModelRegistryItem): Promise<void> => {
    if (!model.custom || deletingCustomModel !== null) return;

    const bridge = getChatThreadDesktopBridge();
    if (!bridge || typeof bridge.deleteCustomModel !== "function") {
      setRegistryError("Custom model bridge chưa hỗ trợ xóa.");
      return;
    }

    setDeletingCustomModel(model.model);
    setRegistryError(null);
    try {
      const deleted = await bridge.deleteCustomModel({ model: model.model });
      if (!deleted) {
        throw new Error(`Custom model không tồn tại: ${model.model}`);
      }

      const nextRegistry = await refreshRegistry();
      const deletedSelection: ModelSelection = {
        provider: model.provider,
        model: model.model,
      };
      if (sameSelection(deletedSelection, selected)) {
        const nextSelection =
          nextRegistry.defaultModel ?? firstModel(nextRegistry);
        if (nextSelection) {
          setInner(nextSelection);
          onChange?.(nextSelection);
        }
      }
    } catch (error) {
      setRegistryError(error instanceof Error ? error.message : String(error));
    } finally {
      setDeletingCustomModel(null);
    }
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
          {selectedInfo?.label ?? selected?.model ?? "Mô hình"}
        </span>
        <ChevronUp open={open} />
      </button>

      {open && !disabled && (
        <div className="ct-modelpick__dropup">
          {registryError && (
            <div className="ct-modelpick__status ct-modelpick__status--error">
              {registryError}
            </div>
          )}

          {!registry && !registryError && (
            <div className="ct-modelpick__status">Đang tải model…</div>
          )}

          {registry?.groups.map((group) => (
            <section className="ct-modelpick__group" key={group.provider}>
              <div className="ct-modelpick__group-header">
                <div className="ct-modelpick__group-label">{group.label}</div>
                {group.provider === "custom" && (
                  <button
                    type="button"
                    className="ct-modelpick__custom-add"
                    aria-label="Thêm custom model"
                    title="Thêm custom model"
                    onClick={() => {
                      setOpen(false);
                      setEditingCustomModel(null);
                      setCustomForm(EMPTY_CUSTOM_FORM);
                      setAddingCustom(true);
                      setCustomError(null);
                    }}
                  >
                    +
                  </button>
                )}
              </div>

              {group.models.map((model) => {
                const option: ModelSelection = {
                  provider: model.provider,
                  model: model.model,
                };
                const active = sameSelection(option, selected);

                return (
                  <div
                    className="ct-modelpick__option-row"
                    key={`${model.provider}:${model.model}`}
                  >
                    <button
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

                      <span className="ct-modelpick__check" aria-hidden="true">
                        ✓
                      </span>
                    </button>

                    {model.custom && (
                      <>
                        <div className="ct-modelpick__more-wrap">
                          <button
                            type="button"
                            className="ct-modelpick__more"
                            aria-label={`Tùy chọn cho ${model.label}`}
                            title="Tùy chọn"
                            aria-expanded={customMenuModel === model.model}
                            onClick={() =>
                              setCustomMenuModel((current) =>
                                current === model.model ? null : model.model,
                              )
                            }
                          >
                            <Ellipsis aria-hidden="true" strokeWidth={1.8} />
                          </button>

                          {customMenuModel === model.model && (
                            <div className="ct-modelpick__context-menu">
                              <button
                                type="button"
                                disabled={loadingCustomEdit}
                                onClick={() => void openCustomEditor(model)}
                              >
                                Tùy chỉnh
                              </button>
                            </div>
                          )}
                        </div>

                        <button
                          type="button"
                          className="ct-modelpick__delete"
                          aria-label={`Xóa custom model ${model.label}`}
                          title="Xóa custom model"
                          disabled={deletingCustomModel !== null}
                          onClick={() => void deleteCustom(model)}
                        >
                          <Trash2 aria-hidden="true" strokeWidth={1.8} />
                        </button>
                      </>
                    )}
                  </div>
                );
              })}
            </section>
          ))}
        </div>
      )}

      {addingCustom && !disabled && (
        <div
          className="ct-modelpick__modal-backdrop"
          role="presentation"
          onMouseDown={(event) => {
            if (event.target !== event.currentTarget || savingCustom) return;
            setAddingCustom(false);
            setEditingCustomModel(null);
            setCustomError(null);
          }}
        >
          <div
            className="ct-modelpick__modal"
            role="dialog"
            aria-modal="true"
            aria-labelledby="ct-modelpick-custom-title"
          >
            <div className="ct-modelpick__modal-header">
              <div>
                <div
                  className="ct-modelpick__modal-title"
                  id="ct-modelpick-custom-title"
                >
                  {editingCustomModel ? "Tùy chỉnh custom model" : "Add custom model"}
                </div>
                <div className="ct-modelpick__modal-subtitle">
                  OpenAI-compatible endpoint
                </div>
              </div>
              <button
                type="button"
                className="ct-modelpick__modal-close"
                aria-label="Đóng"
                title="Đóng"
                disabled={savingCustom}
                onClick={() => {
                  setAddingCustom(false);
                  setEditingCustomModel(null);
                  setCustomError(null);
                }}
              >
                <X aria-hidden="true" strokeWidth={1.8} />
              </button>
            </div>

            <form className="ct-modelpick__custom-form" onSubmit={submitCustom}>
              <label className="ct-modelpick__field">
                <span>API key</span>
                <input
                  type="password"
                  autoComplete="off"
                  value={customForm.apiKey}
                  onChange={(event) => {
                    const value = event.currentTarget.value;
                    setCustomForm((current) => ({
                      ...current,
                      apiKey: value,
                    }));
                  }}
                  placeholder={
                    editingCustomModel ? "Để trống để giữ API key hiện tại" : "sk-…"
                  }
                  disabled={savingCustom}
                />
              </label>

              <label className="ct-modelpick__field">
                <span>Endpoint OpenAI-compatible</span>
                <input
                  type="url"
                  value={customForm.endpoint}
                  onChange={(event) => {
                    const value = event.currentTarget.value;
                    setCustomForm((current) => ({
                      ...current,
                      endpoint: value,
                    }));
                  }}
                  placeholder="https://host/v1"
                  disabled={savingCustom}
                />
              </label>

              <label className="ct-modelpick__field">
                <span>Model name</span>
                <input
                  type="text"
                  value={customForm.model}
                  onChange={(event) => {
                    const value = event.currentTarget.value;
                    setCustomForm((current) => ({
                      ...current,
                      model: value,
                    }));
                  }}
                  placeholder="model-id"
                  disabled={savingCustom}
                />
              </label>

              <label className="ct-modelpick__field">
                <span>Max context window</span>
                <input
                  type="number"
                  min={1}
                  step={1}
                  value={customForm.maxContextWindowTokens}
                  onChange={(event) => {
                    const value = event.currentTarget.value;
                    setCustomForm((current) => ({
                      ...current,
                      maxContextWindowTokens: value,
                    }));
                  }}
                  placeholder="64000"
                  disabled={savingCustom}
                />
              </label>

              {customError && (
                <div className="ct-modelpick__custom-error">{customError}</div>
              )}

              <div className="ct-modelpick__custom-actions">
                <button
                  type="button"
                  onClick={() => {
                    setAddingCustom(false);
                    setEditingCustomModel(null);
                    setCustomError(null);
                  }}
                  disabled={savingCustom}
                >
                  Hủy
                </button>
                <button type="submit" disabled={savingCustom}>
                  {savingCustom
                    ? "Đang lưu…"
                    : editingCustomModel
                      ? "Lưu thay đổi"
                      : "Lưu"}
                </button>
              </div>
            </form>
          </div>
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
