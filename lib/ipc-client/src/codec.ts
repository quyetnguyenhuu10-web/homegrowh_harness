import {
    Decoder,
    Encoder,
} from "cbor-x";

const encoder = new Encoder({
    useRecords: false,
    structuredClone: false,
    pack: false,
});

const decoder = new Decoder({
    useRecords: false,
    mapsAsObjects: true,
});

export function encode(value: unknown): Uint8Array {
    return encoder.encode(value);
}

export function decode(data: Uint8Array): unknown {
    return decoder.decode(data);
}
