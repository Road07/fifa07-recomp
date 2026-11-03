"""Summarize observed physical allocator results; no inferred live-byte totals."""
from pathlib import Path
import json
import re
import sys

root = Path(__file__).resolve().parent.parent
for attempt in sys.argv[1:]:
    lines = (root / 'logs' / f'{attempt}.log').read_text(encoding='utf-8-sig').splitlines()
    policy = None
    allocations = []
    pool_index = None
    for index, line in enumerate(lines):
        match = re.search(r'available_mib=(\d+) reserve_mib=(\d+) preallocate_mib=(\d+) physical_free_pages=(\d+)', line)
        if match:
            policy = dict(zip(('available_mib', 'reserve_mib', 'preallocate_mib', 'physical_free_pages'), map(int, match.groups())))
        match = re.search(r'size=(\d+) caller=([\dA-F]+) result=([\dA-F]+) free_before_pages=(\d+) free_after_pages=(\d+)', line)
        if match:
            size, caller, address, before, after = match.groups()
            item = dict(size=int(size), caller=caller, result=address, free_before_pages=int(before), free_after_pages=int(after), line=index + 1)
            allocations.append(item)
            if caller == '825C0028':
                pool_index = len(allocations) - 1
    later = allocations[pool_index:] if pool_index is not None else []
    failures = [item for item in allocations if int(item['result'], 16) == 0]
    result_path = root / 'logs' / f'{attempt}.json'
    result = json.loads(result_path.read_text(encoding='utf-8-sig')) if result_path.exists() else None
    report = dict(attempt=attempt, process_result=result, policy=policy, allocation_count=len(allocations), pool_allocation=allocations[pool_index] if pool_index is not None else None, minimum_observed_free_pages_after_pool=min((item['free_after_pages'] for item in later), default=None), failed_allocations=failures, allocator_errors=[line for line in lines if 'unable to alloc physical memory' in line or 'failed to find contiguous range' in line])
    (root / 'logs' / f'{attempt}.memory.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
