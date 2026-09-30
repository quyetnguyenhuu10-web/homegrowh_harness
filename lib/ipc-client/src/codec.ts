import {
    Decoder,
    Encoder,
} from "cbor-x";
import { failure, normalizeError, success, type Result } from "./error.js";

const encoder = new Encoder({
    useRecords: false,
    structuredClone: false,
    pack: false,
});

const decoder = new Decoder({
    useRecords: false,
    mapsAsObjects: true,
});

export function encode(value: unknown): Result<Uint8Array> {
    try {
        return success(encoder.encode(value));
    } catch (error) {
        return failure(normalizeError(error, "encode", "protocol_error", [
            { api: "cbor-x.Encoder.encode" },
        ]));
    }
}

export function decode(data: Uint8Array): Result<unknown> {
    let frameSize: number | undefined;
    try {
        frameSize = data.byteLength;
        return success(decoder.decode(data));
    } catch (error) {
        return failure(normalizeError(error, "decode", "protocol_error", [
            {
                api: "cbor-x.Decoder.decode",
                ...(frameSize === undefined ? {} : { frame_size: frameSize }),
            },
        ]));
    }
}
