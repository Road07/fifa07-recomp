# Loading-time physical memory exhaustion

The failing request is 196,608 bytes. With sharing disabled, the final control
run has only 36,864 physical bytes free and exits with `0x80000003` at 68.12 s.

Guest return-address chain:

```text
8237AD24 -> 821EB27C -> 821CF138 / 821CF164
         -> 822A7560 -> 822A7AB0 -> 825B68D0
```

The asset loader repeatedly creates GPU cubes from identical small character
environment DDS files (`data/sceneassets/charactercmn/env*.dds`). Each observed
cube creation allocates two 192 KiB physical blocks. Resource identity and full
DDS contents, rather than filenames or reused guest buffer addresses, informed
the fix.

`port/src/cube_texture_cache.cpp` wraps the original cube creation entrypoint
`0x822A7A50`. It shares only byte-for-byte identical complete DDS data on the
same guest device, with file size at most 256 KiB and dimensions at most 256.
A hash only selects candidates; complete byte comparison confirms identity.
Other creation requests follow the original function.

Each cached texture holds one original guest AddRef. Each reuse gives the
caller its own AddRef and a valid texture result. Publication races release
only the unused extra reference. Guest calls run outside the cache mutex.
The cache is bounded to eight entries and 4 MiB of measured GPU backing
allocations, retains entries for the runtime lifetime, and resets before a
new XEX starts. It does not grow with repeated loads.

The observed working set is four shared textures / 1.5 MiB. At least 256
creation requests were reused, avoiding 96 MiB of repeated physical allocation.
The first enabled trial survived its complete 300.44 s observation. The final
same-EXE A/B reports and automated checks are in:

- `logs/memory-final-control-off.json` and `.log`
- `logs/memory-final-sharing-on.json` and `.log`
- `logs/memory-fix-verification.json`

The final enabled run survived all 300.37 s; sharing OFF in that exact same EXE
failed at 68.12 s. The automated verification passed. An advancing
Arsenal–Tottenham match scene was visually observed in the enabled run.

The official runtime DLL, original XEX and `memory.ini` remain unchanged.
The successful tests use the original 14 MiB preallocation policy. Earlier
64/112 MiB reserve experiments still exhausted memory; a 128 MiB reserve
skipped the pool and encountered a different early breakpoint. None of these
reserve experiments is enabled by the normal launcher.

Reproduce the comparison with unused attempt names:

```powershell
.\tools\test-startup.ps1 -Attempt control-new -CubeSharing off -DurationSeconds 180 -ShowWindow
.\tools\test-startup.ps1 -Attempt fixed-new -CubeSharing on -DurationSeconds 300 -ShowWindow
```

Normal launch enables sharing. Set `FIFA07_SHARE_CUBE_TEXTURES=0` to revert it.
`tools/verify-memory-fix.py` checks the recorded final A/B results, binary
identity, cache bounds and unchanged input/runtime hashes. Allocation tracing
is available with `-TracePhysical`; the verbose asset-identity investigation
hook is retained in `tools/diagnostics/` and excluded from normal builds.

Validation concerns the reproduced loading-time physical OOM. The match scene
was visually observed progressing; a full match, saves and online behavior
were not part of this fix.
