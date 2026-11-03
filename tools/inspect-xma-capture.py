"""Inspect the bounded opt-in packet/float capture, without changing game data."""
import argparse
import json
import struct
import wave
from collections import defaultdict
from pathlib import Path

import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('capture', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
blob = args.capture.read_bytes()
groups = defaultdict(list)
position = 0
while position + 32 <= len(blob):
    magic, context, sequence, packet_bytes, rate, channels, samples, status = struct.unpack_from('<8I', blob, position)
    if magic != 0x31414D58 or channels not in (1, 2) or samples > 4096 or packet_bytes > 8192:
        raise ValueError(f'Invalid record at {position}')
    end = position + 32 + packet_bytes + channels * samples * 4
    if end > len(blob):
        break  # The live writer may not have flushed a complete last record.
    values = np.frombuffer(blob, '<f4', channels * samples, position + 32 + packet_bytes)
    groups[context, rate, channels].append(values.reshape(channels, samples).T.copy())
    position = end

args.output.mkdir(parents=True, exist_ok=True)
summary = []
for (context, rate, channels), blocks in sorted(groups.items()):
    pcm = np.concatenate(blocks)
    filename = f'context-{context}-{rate}-{channels}ch.wav'
    # Unity gain, clipping only for the int16 listening copy. Report raw range.
    with wave.open(str(args.output / filename), 'wb') as output:
        output.setnchannels(channels)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes((np.clip(pcm, -1, 1) * 32767).astype('<i2').tobytes())
    summary.append(dict(context=context, rate=rate, channels=channels,
                        frames=len(blocks), seconds=len(pcm)/rate,
                        peak=float(np.max(np.abs(pcm))),
                        dc=pcm.mean(axis=0).tolist(),
                        rms=float(np.sqrt(np.mean(pcm.astype(float)**2))), file=filename))
result = dict(completeBytes=position, trailingBytes=len(blob)-position, contexts=summary)
(args.output / 'summary.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
