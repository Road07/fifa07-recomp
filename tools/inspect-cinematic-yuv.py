"""Render diagnostic XMedia YUV420 samples (8-byte BE millisecond timestamp)."""
from pathlib import Path
import argparse
import struct
import numpy as np
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
args = parser.parse_args()
width, height = 1280, 720
area = width * height
frames = []
for path in args.directory.glob('cinematic-yuv-*.bin'):
    raw = path.read_bytes()
    if len(raw) != 8 + area * 3 // 2:
        raise ValueError(f'Unexpected frame size: {path}')
    timestamp, = struct.unpack_from('>Q', raw)
    y = np.frombuffer(raw, np.uint8, area, 8).reshape(height, width)
    preview = Image.fromarray(y).resize((640, 360))
    label = f'{path.stem}  Y mean={y.mean():.2f} min={y.min()} max={y.max()}'
    tile = Image.new('RGB', (640, 392))
    tile.paste(preview, (0, 32))
    ImageDraw.Draw(tile).text((4, 8), label, fill='white')
    frames.append((str(path).split('-')[-2], timestamp, tile))
if not frames:
    raise SystemExit('No complete YUV samples yet')
frames.sort(key=lambda item: (item[0], item[1]))
montage = Image.new('RGB', (1280, 392 * ((len(frames) + 1)//2)))
for index, (_, timestamp, tile) in enumerate(frames):
    montage.paste(tile, (640 * (index % 2), 392 * (index//2)))
output = args.directory / 'cinematic-yuv-luma.png'
montage.save(output)
print(output.resolve())
