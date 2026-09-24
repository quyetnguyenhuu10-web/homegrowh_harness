import type { ProviderName } from "@hh/provider";

export const CHAT_THREAD_LIST_MODEL_REGISTRY_CHANNEL =
  "homegrowh-chat-thread:model-registry:list";
export const CHAT_THREAD_ADD_CUSTOM_MODEL_CHANNEL =
  "homegrowh-chat-thread:model-registry:add-custom";
export const CHAT_THREAD_DELETE_CUSTOM_MODEL_CHANNEL =
  "homegrowh-chat-thread:model-registry:delete-custom";
export const CHAT_THREAD_GET_CUSTOM_MODEL_CHANNEL =
  "homegrowh-chat-thread:model-registry:get-custom";
export const CHAT_THREAD_UPDATE_CUSTOM_MODEL_CHANNEL =
  "homegrowh-chat-thread:model-registry:update-custom";

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

export interface DeleteCustomModelInput {
  model: string;
}

export interface GetCustomModelInput {
  model: string;
}

export interface CustomModelEditableConfig {
  endpoint: string;
  model: string;
  contextWindowTokens: number;
}

export interface UpdateCustomModelInput {
  originalModel: string;
  apiKey?: string;
  endpoint: string;
  model: string;
  maxContextWindowTokens: number;
}
