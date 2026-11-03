"""Verify the real loading-time OOM A/B run and unchanged input/runtime files."""
from pathlib import Path
import hashlib
import json
import re

root = Path(__file__).resolve().parent.parent
def read_json(path):
    return json.loads((root / path).read_text(encoding='utf-8-sig'))
def sha(path):
    return hashlib.sha256((root / path).read_bytes()).hexdigest()

off = read_json('logs/memory-final-control-off.json')
on = read_json('logs/memory-final-sharing-on.json')
off_log = (root / off['log']).read_text(encoding='utf-8-sig')
on_log = (root / on['log']).read_text(encoding='utf-8-sig')
assert off['exeSha256'] == on['exeSha256'], 'A/B executable differs'
assert off['runtimeSha256'] == on['runtimeSha256'], 'A/B runtime differs'
assert off['reserveMiB'] == on['reserveMiB'] == 14
assert not off['shareCubeTextures'] and on['shareCubeTextures']
assert off['exitedBeforeTimeout'] and off['exitCode'] != 0
assert re.search(r'size=196608 .*result=00000000 free_before_pages=(?:[0-9]|[1-3][0-9]|4[0-7]) ', off_log)
assert not on['exitedBeforeTimeout'] and on['durationSeconds'] >= 300
assert not re.search(r'unable to alloc physical memory|BaseHeap::Alloc failed|\[FATAL\]', on_log)
insertions = [(int(a), int(b)) for a, b in re.findall(r'\[cube-share\] inserted=(\d+).*total_physical_bytes=(\d+)', on_log)]
hits = [int(value) for value in re.findall(r'\[cube-share\] hit=(\d+)', on_log)]
assert insertions and max(a for a, _ in insertions) <= 8
assert max(b for _, b in insertions) <= 4 * 1024 * 1024
assert hits and max(hits) >= 256
inventory = {item['path']: item for item in read_json('tools/game-files.json')}
for name in ('default.xex', 'memory.ini'):
    assert sha('game/' + name) == inventory[name]['sha256'], name + ' changed'
assert sha('tools/rexglue/win-amd64/bin/rexruntime.dll') == sha('port/out/build/win-release/rexruntime.dll')
assert sha('port/out/build/win-release/fifa07.exe').upper() == on['exeSha256']
report = dict(passed=True, control=off, fixed=on, maximum_cache_entries=max(a for a, _ in insertions),
              maximum_pinned_physical_bytes=max(b for _, b in insertions), minimum_cache_hits=max(hits),
              xex_unchanged=True, memory_ini_unchanged=True, official_runtime_unchanged=True,
              cache_source_sha256=sha('port/src/cube_texture_cache.cpp'))
(root / 'logs/memory-fix-verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
