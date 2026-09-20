import {
  existsSync,
  mkdirSync,
  readFileSync,
  renameSync,
  writeFileSync,
} from "node:fs";
import { dirname, join, resolve } from "node:path";

import {
  getDefaultModelSelection,
  getModelContextLimit,
  listModelGroups,
  type ProviderName,
} from "@homegrowh/provider";

import { defaultProjectsDir } from "../../history_conversation/main/repository";
import type {
  AddCustomModelInput,
  ModelRegistryItem,
  ModelRegistrySelection,
  ModelRegistrySnapshot,
} from "../model_registry_contract";

export interface ModelRegistryOptions {
  projectsDir?: string;
}

interface CustomModelFileEntry {
  apiKey: string;
  endpoint: string;
  model: string;
  contextWindowTokens: number;
}

interface CustomModelRegistryFile {
  version: 1;
  models: CustomModelFileEntry[];
}

export interface ResolvedModelRuntime {
  provider: ProviderName;
  model: string;
  contextWindowTokens: number;
  apiKey?: string;
  baseUrl?: string;
}

const CUSTOM_MODEL_FILE = ".custom_models.json";

function projectsDir(options: ModelRegistryOptions): string {
  return resolve(options.projectsDir ?? defaultProjectsDir());
}

function customModelFile(options: ModelRegistryOptions): string {
  return join(projectsDir(options), CUSTOM_MODEL_FILE);
}

function emptyCustomRegistry(): CustomModelRegistryFile {
  return { version: 1, models: [] };
}

function isValidCustomEntry(value: unknown): value is CustomModelFileEntry {
  if (typeof value !== "object" || value === null) return false;
  const entry = value as Record<string, unknown>;
  return (
    typeof entry.apiKey === "string" &&
    entry.apiKey.trim().length > 0 &&
    typeof entry.endpoint === "string" &&
    entry.endpoint.trim().length > 0 &&
    typeof entry.model === "string" &&
    entry.model.trim().length > 0 &&
    typeof entry.contextWindowTokens === "number" &&
    Number.isInteger(entry.contextWindowTokens) &&
    entry.contextWindowTokens > 0
  );
}

function readCustomRegistry(options: ModelRegistryOptions): CustomModelRegistryFile {
  const file = customModelFile(options);
  if (!existsSync(file)) return emptyCustomRegistry();
  const text = readFileSync(file, "utf8").trim();
  if (!text) return emptyCustomRegistry();
  const parsed = JSON.parse(text) as Partial<CustomModelRegistryFile>;
  return {
    version: 1,
    models: Array.isArray(parsed.models)
      ? parsed.models.filter(isValidCustomEntry).map((entry) => ({
          apiKey: entry.apiKey.trim(),
          endpoint: entry.endpoint.trim(),
          model: entry.model.trim(),
          contextWindowTokens: entry.contextWindowTokens,
        }))
      : [],
  };
}

function writeCustomRegistry(
  registry: CustomModelRegistryFile,
  options: ModelRegistryOptions,
): void {
  const file = customModelFile(options);
  mkdirSync(dirname(file), { recursive: true });
  const temp = `${file}.tmp`;
  writeFileSync(temp, `${JSON.stringify(registry, null, 2)}\n`, "utf8");
  renameSync(temp, file);
}

function normalizeEndpoint(value: string): string {
  const trimmed = value.trim();
  if (!trimmed) throw new Error("Endpoint không được để trống.");

  let url: URL;
  try {
    url = new URL(trimmed);
  } catch {
    throw new Error("Endpoint phải là URL hợp lệ.");
  }
  if (url.protocol !== "http:" && url.protocol !== "https:") {
    throw new Error("Endpoint chỉ hỗ trợ http hoặc https.");
  }

  url.hash = "";
  url.search = "";
  let pathname = url.pathname.replace(/\/+$/, "");
  if (pathname.endsWith("/chat/completions")) {
    pathname = pathname.slice(0, -"/chat/completions".length);
  }
  url.pathname = pathname || "/";
  return url.toString().replace(/\/+$/, "");
}

function customItem(entry: CustomModelFileEntry): ModelRegistryItem {
  return {
    provider: "custom",
    model: entry.model,
    label: entry.model,
    note: entry.endpoint,
    contextWindowTokens: entry.contextWindowTokens,
    custom: true,
  };
}

export function listModelRegistry(
  options: ModelRegistryOptions = {},
): ModelRegistrySnapshot {
  const builtInGroups = listModelGroups().map((group) => ({
    provider: group.provider,
    label: group.label,
    models: group.models.map((model) => ({
      provider: group.provider,
      model: model.id,
      label: model.label,
      ...(model.note ? { note: model.note } : {}),
      contextWindowTokens: model.contextWindowTokens,
      custom: false,
    })),
  }));
  const customModels = readCustomRegistry(options).models.map(customItem);
  return {
    version: 1,
    defaultModel: getDefaultModelSelection(),
    groups: [
      ...builtInGroups,
      {
        provider: "custom",
        label: "Custom",
        models: customModels,
      },
    ],
  };
}

export function addCustomModel(
  input: AddCustomModelInput,
  options: ModelRegistryOptions = {},
): ModelRegistryItem {
  const apiKey = input.apiKey.trim();
  const model = input.model.trim();
  const contextWindowTokens = Math.floor(input.maxContextWindowTokens);
  if (!apiKey) throw new Error("API key không được để trống.");
  if (!model) throw new Error("Tên model không được để trống.");
  if (!Number.isFinite(input.maxContextWindowTokens) || contextWindowTokens <= 0) {
    throw new Error("Max context window phải là số nguyên lớn hơn 0.");
  }

  const endpoint = normalizeEndpoint(input.endpoint);
  const registry = readCustomRegistry(options);
  if (registry.models.some((entry) => entry.model === model)) {
    throw new Error(`Custom model đã tồn tại: ${model}`);
  }

  const entry: CustomModelFileEntry = {
    apiKey,
    endpoint,
    model,
    contextWindowTokens,
  };
  registry.models.push(entry);
  writeCustomRegistry(registry, options);
  return customItem(entry);
}

export function resolveModelRuntime(
  selection: ModelRegistrySelection,
  options: ModelRegistryOptions = {},
): ResolvedModelRuntime {
  const model = selection.model.trim();
  if (!model) throw new Error("Model không được để trống.");

  if (selection.provider === "custom") {
    const entry = readCustomRegistry(options).models.find(
      (candidate) => candidate.model === model,
    );
    if (!entry) throw new Error(`Custom model không tồn tại: ${model}`);
    return {
      provider: "custom",
      model: entry.model,
      contextWindowTokens: entry.contextWindowTokens,
      apiKey: entry.apiKey,
      baseUrl: entry.endpoint,
    };
  }

  const contextLimit = getModelContextLimit(selection.provider, model);
  return {
    provider: selection.provider,
    model,
    contextWindowTokens: contextLimit.tokens,
  };
}
