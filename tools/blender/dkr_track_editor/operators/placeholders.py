"""Buttons for what the sidebar shows but the addon cannot do yet.

Drawn as real buttons, greyed out, rather than left out: the layout is the one
the finished feature will have, and a disabled button's tooltip says why it is
disabled, which a missing one cannot. Each ``poll`` is false and says so.
"""

from __future__ import annotations

import bpy


class _Unavailable:
    bl_options = {"INTERNAL"}
    reason = "Not available yet"

    @classmethod
    def poll(cls, context):
        cls.poll_message_set(cls.reason)
        return False

    def execute(self, context):
        return {"CANCELLED"}


class DKR_OT_play_music(_Unavailable, bpy.types.Operator):
    """Listen to the track this level plays"""

    bl_idname = "dkr.play_music"
    bl_label = "Play"
    reason = "Listening is not available yet: the music is not loaded from the assets"


CLASSES = (
    DKR_OT_play_music,
)
