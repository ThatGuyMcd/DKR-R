# Launcher copy review (Phase C)

`runtime-recomp/tests/launcher_copy_inventory.json` pins reviewed launcher copy. Before the fold it was checked against main's launcher sources, which v1.0.11 did not ship: the release layers replaced parts of the launcher, and the fold wrote the shipped bytes back. The 71 entries below describe copy the shipped launcher no longer has.

Until this review, `launcher_copy_source_tests.py` lists them in `SUPERSEDED` and asserts that each string stays absent, so none can come back unreviewed. Every other entry is still enforced. Each entry is attributed to the first release layer, in chain order, whose staged copy of the file no longer held the string.

The maintainer owns the review: for each entry, find the shipped string that replaced it, re-baseline the inventory entry against it (or retire it), and remove it from `SUPERSEDED`.

## shared-saves (34)

| Entry | File | Reviewed copy (pre-v1.0.11) |
|---|---|---|
| COPY-157 | `runtime_saves_ui.inl` | Back up, transfer and edit your saves. |
| COPY-158 | `runtime_saves_ui.inl` | Includes all three slots, unlocks and records. |
| COPY-159 | `runtime_saves_ui.inl` | A backup is created before replacing your save. |
| COPY-160 | `runtime_saves_ui.inl` | Includes offline Adventure progress and Controller Paks. Online and modded saves are not included. |
| COPY-161 | `runtime_saves_ui.inl` | EXPORT OFFLINE SAVE BUNDLE |
| COPY-162 | `runtime_saves_ui.inl` | IMPORT OFFLINE SAVE BUNDLE |
| COPY-163 | `runtime_saves_ui.inl` | Four virtual memory cards. Included in offline save bundles when available. |
| COPY-164 | `runtime_saves_ui.inl` | Repair this save? Your progress won't be edited. |
| COPY-165 | `runtime_saves_ui.inl` | The original will be backed up. |
| COPY-166 | `runtime_saves_ui.inl` | Reset all three slots, unlocks and records? A backup will be created first. |
| COPY-167 | `runtime_saves_ui.inl` | Unsaved editor changes will be discarded. |
| COPY-168 | `runtime_saves_ui.inl` | Complete this Adventure slot? |
| COPY-169 | `runtime_saves_ui.inl` | Use Apply changes to save this edit. |
| COPY-170 | `runtime_saves_ui.inl` | Unlock Adventure Two, Drumstick and all T.T. victories? |
| COPY-171 | `runtime_saves_ui.inl` | Discard unsaved changes? |
| COPY-172 | `runtime_saves_ui.inl` | Discard edits to all three slots and shared unlocks? |
| COPY-173 | `runtime_saves_ui.inl` | Apply these edits to your offline save? A backup will be created first. |
| COPY-175 | `runtime_saves_ui.inl` | Offline Adventure save. Online and modded saves aren't affected. |
| COPY-176 | `runtime_saves_ui.inl` | Save repaired. Original kept in Backups. |
| COPY-177 | `runtime_saves_ui.inl` | This save doesn't need repair. |
| COPY-178 | `runtime_saves_ui.inl` | Changes saved. Previous progress kept in Backups. |
| COPY-179 | `runtime_saves_ui.inl` | Reading saves... |
| COPY-180 | `runtime_saves_ui.inl` | Online progress is separate from offline saves. |
| COPY-181 | `runtime_saves_ui.inl` | Each mod selection keeps separate progress. |
| COPY-182 | `runtime_saves_ui.inl` | Restore a surviving copy to recover progress. Recovery needs an existing save or backup. |
| COPY-183 | `runtime_saves_ui.inl` | No saves found in this category. |
| COPY-184 | `runtime_saves_ui.inl` | SAVE EDITING UNAVAILABLE |
| COPY-185 | `runtime_saves_ui.inl` | Return to the launcher before importing, restoring or resetting saves. |
| COPY-186 | `runtime_saves_ui.inl` | Leave the lobby before changing offline saves. |
| COPY-187 | `runtime_saves_ui.inl` | Start an Adventure, import a save or create one below. |
| COPY-188 | `runtime_saves_ui.inl` | Repair this save or restore a working backup. |
| COPY-397 | `runtime_ui.cpp` | Legacy mod selections keep separate saves. |
| COPY-413 | `runtime_ui.cpp` | Offline save bundle imported. Previous saves were backed up. |
| COPY-414 | `runtime_ui.cpp` | Offline save bundle exported. |

## camera-plus (14)

| Entry | File | Reviewed copy (pre-v1.0.11) |
|---|---|---|
| COPY-053 | `runtime_graphics_ui.inl` | A smoother chase camera. The right stick turns the camera instead of sending C-buttons. Offline only. |
| COPY-054 | `runtime_graphics_ui.inl` | Hold Look back to see behind you. Change its binding in Controls. Offline only. |
| COPY-055 | `runtime_graphics_ui.inl` | Adjust camera (F7) |
| COPY-056 | `runtime_graphics_ui.inl` | Adjust the camera while driving. Use the mouse. |
| COPY-314 | `runtime_camera_tuning_ui.inl` | How closely the camera follows the racer through turns and drifts. |
| COPY-315 | `runtime_camera_tuning_ui.inl` | How smoothly the camera turns. Damping below 1 adds overshoot. |
| COPY-316 | `runtime_camera_tuning_ui.inl` | Turn the camera with the right stick or mouse. Hold the right mouse button to keep the view; Escape frees the pointer. |
| COPY-317 | `runtime_camera_tuning_ui.inl` | Shake from boosts, landings and impacts. Zero turns shake off. |
| COPY-318 | `runtime_camera_tuning_ui.inl` | Lower values smooth the camera after collisions. 100% follows the impact immediately. |
| COPY-319 | `runtime_camera_tuning_ui.inl` | Camera settings updated. Your custom values are kept. |
| COPY-321 | `runtime_camera_tuning_ui.inl` | Start an offline race to adjust the camera. |
| COPY-322 | `runtime_camera_tuning_ui.inl` | Custom. Gold labels mark changed settings. |
| COPY-323 | `runtime_camera_tuning_ui.inl` | Records camera movement in the log until turned off or DKR-R closes. |
| COPY-324 | `runtime_camera_tuning_ui.inl` | Player %u camera. Changes apply while driving. |

## rollback-only (10)

| Entry | File | Reviewed copy (pre-v1.0.11) |
|---|---|---|
| COPY-196 | `runtime_online_ui.inl` | Host Prediction |
| COPY-197 | `runtime_online_ui.inl` | Strict Input Sync |
| COPY-198 | `runtime_online_ui.inl` | Experimental Rollback |
| COPY-199 | `runtime_online_ui.inl` | Experimental Rollback |
| COPY-200 | `runtime_online_ui.inl` | Uses Experimental Rollback. |
| COPY-202 | `runtime_online_ui.inl` | Higher values allow more time for late inputs, but can increase replay work. Automatic input delay is a good starting point. |
| COPY-203 | `runtime_online_ui.inl` | Use Host Prediction |
| COPY-205 | `runtime_online_ui.inl` | The host handles missing inputs. Response time depends on the connection. |
| COPY-206 | `runtime_online_ui.inl` | Waits for every player's input. Best for stable, low-latency connections. |
| COPY-250 | `runtime_online_ui.inl` | Everyone selects Ready, then the host starts the game. Auto uses Experimental Rollback. |

## rollback-connection (7)

| Entry | File | Reviewed copy (pre-v1.0.11) |
|---|---|---|
| COPY-195 | `runtime_online_ui.inl` | Online mode |
| COPY-229 | `runtime_online_ui.inl` | Game in sync |
| COPY-230 | `runtime_online_ui.inl` | Checking game sync... |
| COPY-232 | `runtime_online_ui.inl` | Game input recording enabled. |
| COPY-242 | `runtime_online_ui.inl` | Connection test results |
| COPY-246 | `runtime_online_ui.inl` | A 7-second connection check. Results don't measure game performance. |
| COPY-247 | `runtime_online_ui.inl` | Good - fewer network interruptions expected |

## online-directory (3)

| Entry | File | Reviewed copy (pre-v1.0.11) |
|---|---|---|
| COPY-248 | `runtime_online_ui.inl` | Create a lobby and share its 5-character code, or enter a friend's code to join. |
| COPY-249 | `runtime_online_ui.inl` | The host approves joining players. You can also join friends through Open Lobbies. |
| COPY-253 | `runtime_online_ui.inl` | Create a lobby or join with a code. |

## all-player-detail (1)

| Entry | File | Reviewed copy (pre-v1.0.11) |
|---|---|---|
| COPY-063 | `runtime_graphics_ui.inl` | Use the most detailed racer models. |

## lobby-stability (1)

| Entry | File | Reviewed copy (pre-v1.0.11) |
|---|---|---|
| COPY-268 | `runtime_online_mods_ui.inl` | New offline copies start disabled. Your existing mod selections and saves are kept. |

## launcher-polish (1)

| Entry | File | Reviewed copy (pre-v1.0.11) |
|---|---|---|
| COPY-399 | `runtime_ui.cpp` | Legacy patches need the ROM version they were made for. Native tracks don't need patch preparation. |
