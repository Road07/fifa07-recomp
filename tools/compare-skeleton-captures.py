"""Compare pose invariants without requiring the captures to share a frame."""
import json
import math
from pathlib import Path
import struct

root = Path(__file__).resolve().parent.parent
report = {}
for label, directory in [
    ('port', 'port-active-resume-1'),
    ('xenia', 'xenia-skinning-reference-fixed'),
    ('fixed_port', 'vpk-alias-fixed-pose-1'),
]:
    path = root / 'logs' / directory
    if not (path / 'world-24.bin').exists():
        continue
    matrices = list(struct.iter_unpack('>16f', (path / 'world-24.bin').read_bytes()))
    pairs = [(8, 9), (9, 10), (112, 113), (113, 114), (8, 112), (7, 111)]
    report[label] = {
        'source': str(path.relative_to(root)),
        'joint_distances_cm': {
            f'{i}-{j}': math.dist(matrices[i][12:15], matrices[j][12:15])
            for i, j in pairs
        },
    }
products = root / 'logs/vpk-alias-fixed-pose-1/bone-products.bin'
if products.exists():
    # This hook records each A, B and output synchronously around the guest call.
    records = list(struct.iter_unpack('<44f', products.read_bytes()))
    if len(records) == 502:
        matrices = [record[16:32] for record in records[:251]]
        report['fixed_port_synchronous'] = {
            'source': 'logs/vpk-alias-fixed-pose-1/bone-products.bin',
            'joint_distances_cm': {
                f'{i}-{j}': math.dist(matrices[i][12:15], matrices[j][12:15])
                for i, j in pairs
            },
        }
report['limitations'] = [
    'Captures use different poses; absolute positions and rotations are not directly comparable.',
    'External RAM captures are asynchronous; the synchronous hook provides a separate port observation.',
]
destination = root / 'logs/skeleton-comparison.json'
destination.write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report, indent=2))
