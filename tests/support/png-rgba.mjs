import { inflateSync } from 'node:zlib';

const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

export function decodeRgba8Png(source) {
  if (!source.subarray(0, 8).equals(signature)) throw new Error('invalid PNG signature');
  let offset = 8;
  let width = 0;
  let height = 0;
  const compressed = [];
  while (offset + 12 <= source.length) {
    const length = source.readUInt32BE(offset);
    const type = source.toString('ascii', offset + 4, offset + 8);
    const dataStart = offset + 8;
    const dataEnd = dataStart + length;
    if (dataEnd + 4 > source.length) throw new Error(`truncated PNG ${type} chunk`);
    if (type === 'IHDR') {
      width = source.readUInt32BE(dataStart);
      height = source.readUInt32BE(dataStart + 4);
      if (source[dataStart + 8] !== 8 || source[dataStart + 9] !== 6 ||
          source[dataStart + 12] !== 0) throw new Error('PNG must be non-interlaced RGBA8');
    } else if (type === 'IDAT') compressed.push(source.subarray(dataStart, dataEnd));
    else if (type === 'IEND') break;
    offset = dataEnd + 4;
  }
  if (!width || !height || !compressed.length) throw new Error('incomplete PNG');
  const filtered = inflateSync(Buffer.concat(compressed));
  const stride = width * 4;
  if (filtered.length !== (stride + 1) * height) throw new Error('unexpected decoded PNG size');
  const pixels = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y += 1) {
    const filter = filtered[y * (stride + 1)];
    const row = filtered.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; x += 1) {
      const left = x >= 4 ? pixels[y * stride + x - 4] : 0;
      const up = y > 0 ? pixels[(y - 1) * stride + x] : 0;
      const upperLeft = y > 0 && x >= 4 ? pixels[(y - 1) * stride + x - 4] : 0;
      let predictor = 0;
      if (filter === 1) predictor = left;
      else if (filter === 2) predictor = up;
      else if (filter === 3) predictor = Math.floor((left + up) / 2);
      else if (filter === 4) predictor = paeth(left, up, upperLeft);
      else if (filter !== 0) throw new Error(`unsupported PNG filter ${filter}`);
      pixels[y * stride + x] = (row[x] + predictor) & 0xff;
    }
  }
  return { width, height, pixels };
}

export function alphaBounds({ width, height, pixels }) {
  let minX = width;
  let minY = height;
  let maxX = -1;
  let maxY = -1;
  let count = 0;
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      if (pixels[(y * width + x) * 4 + 3] === 0) continue;
      minX = Math.min(minX, x);
      minY = Math.min(minY, y);
      maxX = Math.max(maxX, x);
      maxY = Math.max(maxY, y);
      count += 1;
    }
  }
  return { count, bounds: count ? [minX, minY, maxX - minX + 1, maxY - minY + 1] : null };
}

function paeth(left, up, upperLeft) {
  const estimate = left + up - upperLeft;
  const leftDistance = Math.abs(estimate - left);
  const upDistance = Math.abs(estimate - up);
  const diagonalDistance = Math.abs(estimate - upperLeft);
  if (leftDistance <= upDistance && leftDistance <= diagonalDistance) return left;
  return upDistance <= diagonalDistance ? up : upperLeft;
}
