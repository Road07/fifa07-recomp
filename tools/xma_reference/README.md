Source snapshot: Xenia Canary f448664, `src/xenia/apu/xma_context_new.cc`.
Copyright 2024 Xenia Canary. The original BSD license is included in LICENSE.
Source: https://github.com/xenia-canary/xenia-canary/tree/f448664

This is the reference input to `../prepare-xma-fix.py`. It is adapted to the
pinned ReXGlue class layout and older FFmpeg ABI, not built directly.
The complete state machine fixed FIFA 07's menu effects in the user-verified
trial `xma-full-reference-1`. Partial backports did not.

The resulting `port/src/xma_fix/xma_context.cpp` incorporates Work, Decode,
UpdateLoopStatus and GetPacketInfo from this snapshot, along with the reference
packet lookup rules. Other code in that directory derives from the pinned
ReXGlue SDK, whose original copyright notices remain in those files.
