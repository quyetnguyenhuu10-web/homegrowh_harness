import type { ProviderName } from "@homegrowh/provider";

export const CHAT_THREAD_LIST_MODEL_REGISTRY_CHANNEL =
  "homegrowh-chat-thread:model-registry:list";
export const CHAT_THREAD_ADD_CUSTOM_MODEL_CHANNEL =
  "homegrowh-chat-thread:model-registry:add-custom";

export interface ModelRegistrySelection {
  provider: ProviderName;
  model: string;
}

export interface ModelRegistryItem extends ModelRegistrySelection {
  label: string;
  note?: string;
  contextWindowTokens: number;
  custom: boolean;
}

export interface ModelRegistryGroup {
  provider: ProviderName;
  label: string;
  models: readonly ModelRegistryItem[];
}

export interface ModelRegistrySnapshot {
  version: 1;
  defaultModel: ModelRegistrySelection | null;
  groups: readonly ModelRegistryGroup[];
}

export interface AddCustomModelInput {
  apiKey: string;
  endpoint: string;
  model: string;
  maxContextWindowTokens: number;
}
