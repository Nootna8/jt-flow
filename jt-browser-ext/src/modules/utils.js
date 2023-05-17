import {Base64} from 'js-base64';

export async function unzip(data) {
    let ds = new DecompressionStream("gzip");
    const writer = ds.writable.getWriter();
    writer.write(data);
    writer.close();
    
    const reader = ds.readable.getReader();
    let totalSize = 0
    const output = []
    
    while (true) {
        const { value, done } = await reader.read();
        if (done) break;
            output.push(value);
        totalSize += value.byteLength;
    }

    const concatenated = new Uint8Array(totalSize);
    let offset = 0;
    for (const array of output) {
        concatenated.set(array, offset);
        offset += array.byteLength;
    }

    return concatenated
}

export async function unzipBase64(data) {
    var uint8Array = Base64.toUint8Array(data);

    return unzip(uint8Array);
}

