# Bounded native profiling-tool validation

Date: 2026-10-25. Result: passed for the stated scope. No renderer optimization
or default-enable decision was made.

A fresh isolated Windows build included the reviewed default-off profiling
producer. Its executable SHA-256 is
`3ee004124022495be2342c5f60ed1d6167074d447a4f2f26cc33a44f4f909343`.
The source worktree and prior binaries were not built over. One drive-selection
preflight refused an unreadable occupied letter before any mapping or compiler
ran. A free transient mapping was then used for one successful full build and
removed after verifying ownership. No existing mapping was changed.

Five foreground runs used a recorded Facility tank-room pose, fixed seed,
isolated configuration/EEPROM and a 1365 by 768 client window. Two runs used
ordinary scripted firing to destroy a tank and fill the room with gas. The
existing invincibility cheat excluded damage/death; exact adjacent destruction
frames were not examined.

| Check | Result |
| --- | --- |
| Intact room, clean vs instrumented | All 3 raw sampled images, capture identities and selected frame identities matched |
| Gas, clean vs instrumented | All 67 raw sampled images matched; RGB/far-fog timelines and capture/frame identities matched |
| Completed GPU queries | All 3,000 selected regular frames in each of 3 instrumented runs had ready results: 9,000 total |
| Native timing path | `GL_TIMESTAMP` pairs on OpenGL 4.6.0, NVIDIA 591.86 |
| Foreground | All 5 runs passed sampled ownership, visibility, obscuration and primary-work-area checks after the startup grace period |
| Normal exit | All 5 runs exited normally |
| Graceful shutdown | An owned window-close request exercised explicit teardown: 9,657 ready rows, 1 pending row outside the selected window, 0 unsupported rows, 0 ring/duplicate/error drops and 0 misuse |

The four frame-limited runs exit without the renderer's explicit shutdown
summary. The fifth run therefore used a normal window-close request, after the
selected interval, to exercise that path separately. The pending result remained
blank rather than becoming zero. GPU resources were released while the context
was valid by the reviewed shutdown implementation; the live check observed its
normal completion, not a separate driver allocation audit.

The logging preflight's `SL_RUN=1` override and private `SL_RUN_DIR` were used.
Both gas runs produced matching timelines: 13 RGB records and 3,601 far-fog
records, with sampled before/buildup/full coverage. This resolves the logging
prerequisite for this validation. It does not repair the missing logs in the
closed earlier fog experiment or reopen its result.

At the intact pose, the clean sample averaged 1.3920 ms per present (718.38 FPS)
and the instrumented sample 1.4132 ms (707.62 FPS). This is one pair, not a
repeatable overhead estimate, a gain claim or a frame-budget qualification.
The instrumented clear-to-present CPU wall span averaged 1.2122 ms and GPU
timeline elapsed 0.1578 ms. The spans overlap and do not measure exclusive CPU
execution or GPU-busy time. Gas runs had readbacks throughout their sequence;
their timing is diagnostic only.

Local evidence is preserved under
`.sightline/chat-handoff/artifacts/v040/live-readiness-validation-r314/`, including
the exact source-input pins, compiler logs, plans, controls, per-run validations,
`validation-report.json` and inspection images. Raw native run files remain at
the declared roots in those plans. No optimization experiment was adopted, no
permanent privilege changed, no WPR recording started and no commit or push was
performed.

This validates the tool path on one driver and one pose. It does not qualify
Frigate, Statue, combat effects, 1080p60, other hardware, timer-query fallback
paths, extra presentations or a complete campaign. Those remain separate
coverage questions; another general optimization search is not implied.
