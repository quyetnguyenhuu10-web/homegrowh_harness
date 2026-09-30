import type { ValidateFunction } from "ajv";
import type { Plugin, PluginApi, PluginManifest } from "../plugin.js";

export type LoadedApi = {
    descriptor: PluginApi;
    validateInput: ValidateFunction;
    validateOutput: ValidateFunction;
};

export type LoadedPlugin = {
    manifest: PluginManifest;
    plugin: Plugin;
    apis: Map<number, LoadedApi>;
};

