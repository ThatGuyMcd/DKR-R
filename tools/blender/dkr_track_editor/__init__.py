"""DKR track editor - a Blender addon for authoring Diddy Kong Racing tracks.

Places objects, items and the AI racing line over a track's geometry and writes
the object map back out, plus the ``.dkrmap`` package DKR-R loads from
``custom-tracks/``.
See docs/BLENDER_ADDON_PLAN.md for the design and what is still Phase 2.

Importing this package must not require Blender: the format, catalogue,
AI-graph, validation and packaging modules are all plain Python so they can be
tested outside it. Only :func:`register` reaches for ``bpy``.
"""

from __future__ import annotations

bl_info = {
    "name": "DKR Track Editor",
    "author": "DKR-R",
    "version": (0, 2, 0),
    "blender": (4, 2, 0),
    "location": "View3D > Sidebar > DKR",
    "description": "Author Diddy Kong Racing tracks and export .dkrmap packages",
    "category": "Import-Export",
    "doc_url": "https://github.com/ThatGuyMcd/DKR-R/blob/main/docs/BLENDER_ADDON_PLAN.md",
}

#: Populated by :func:`register`; kept so :func:`unregister` can undo exactly
#: what was registered even if a later module failed to import.
_REGISTERED = []


def _module_classes():
    """Every class to register, in dependency order.

    Imported here rather than at module scope so that ``import
    dkr_track_editor`` works without Blender, which is what lets the tests run
    on a plain Python.
    """
    from . import prefs, props
    from .operators import (ai, checks, custom_textures, edit, geometry,
                            header, io_objects, level_type, minimap, music,
                            new_track, pack, placeholders, race_ai, rom_assets,
                            skybox, start_grid, textures, water, waterfall)
    from .ui import panels

    classes = []
    classes += list(prefs.CLASSES)
    classes += list(props.CLASSES)
    classes += list(rom_assets.CLASSES)
    classes += list(level_type.CLASSES)
    classes += list(start_grid.CLASSES)
    classes += list(placeholders.CLASSES)
    classes += list(skybox.CLASSES)
    classes += list(io_objects.CLASSES)
    classes += list(geometry.CLASSES)
    classes += list(textures.CLASSES)
    classes += list(custom_textures.CLASSES)
    classes += list(water.CLASSES)
    classes += list(waterfall.CLASSES)
    classes += list(edit.CLASSES)
    classes += list(ai.CLASSES)
    classes += list(race_ai.CLASSES)
    classes += list(minimap.CLASSES)
    classes += list(checks.CLASSES)
    classes += list(header.CLASSES)
    classes += list(music.CLASSES)
    classes += list(new_track.CLASSES)
    classes += list(pack.CLASSES)
    classes += list(panels.CLASSES)
    return classes



def _materialize_blender_properties(cls):
    """Evaluate postponed bpy.props annotations before Blender registers a class.

    This addon uses ``from __future__ import annotations`` in several modules.
    Python therefore stores declarations such as ``foo: EnumProperty(...)`` as
    strings instead of calling ``EnumProperty`` when the class is created.
    Blender 4.5 expects the annotation value to be the ``_PropertyDeferred``
    object returned by bpy.props, so without this step the class registers but
    the RNA property simply does not exist.

    Only annotations whose expression starts with a Blender property factory are
    evaluated. Normal type hints stay postponed.
    """
    import re
    import sys

    annotations = getattr(cls, "__annotations__", None)
    if not annotations:
        return

    module = sys.modules.get(cls.__module__)
    if module is None:
        return

    property_expr = re.compile(
        r"^(?:bpy\.props\.)?(?:Bool|Collection|Enum|Float|FloatVector|Int|"
        r"IntVector|Pointer|String)Property\s*\("
    )
    namespace = vars(module)
    localns = dict(vars(cls))

    for name, value in list(annotations.items()):
        if not isinstance(value, str):
            continue
        expression = value.strip()
        if not property_expr.match(expression):
            continue
        try:
            annotations[name] = eval(expression, namespace, localns)
        except Exception as exc:
            raise RuntimeError(
                f"Could not create Blender property {cls.__name__}.{name}: {exc}"
            ) from exc

def _menu_import(self, context):
    self.layout.operator("dkr.import_level", text="DKR Track (retail)")
    self.layout.operator(
        "dkr.import_object_map", text="DKR Object Map (.gltf)"
    )
    self.layout.operator(
        "dkr.import_geometry", text="DKR Track Geometry (.bin)"
    )


def _menu_export(self, context):
    self.layout.operator(
        "dkr.export_object_map", text="DKR Object Map (.gltf)"
    )


def _same_addon_registered_class(bpy, cls):
    """Return a stale registered class owned by this addon, if one exists.

    Blender can keep the old Python class registered when an extension reloads
    after a failed enable.  The freshly imported class object is then different
    even though it has the same name, and register_class() raises "already
    registered as a subclass".  Only classes from this addon package are
    eligible for cleanup so we never unregister another addon's type.
    """
    old = getattr(bpy.types, cls.__name__, None)
    if old is None:
        return None
    module = getattr(old, "__module__", "") or ""
    package = __package__ or "dkr_track_editor"
    if module == package or module.startswith(package + "."):
        return old
    return None


def _safe_remove_menu(menu, callback):
    """Remove a menu callback without letting cleanup abort."""
    try:
        menu.remove(callback)
    except (RuntimeError, ValueError):
        pass


def _rollback_classes(bpy):
    """Undo every class registered by the current enable attempt."""
    while _REGISTERED:
        cls = _REGISTERED.pop()
        try:
            bpy.utils.unregister_class(cls)
        except RuntimeError:
            pass


def register():
    import bpy

    from . import props

    # A previous failed enable/reload may have left types behind.  Clear only
    # stale classes that demonstrably belong to this addon before registering
    # the fresh class objects.
    classes = _module_classes()
    for cls in classes:
        stale = _same_addon_registered_class(bpy, cls)
        if stale is not None and stale is not cls:
            try:
                bpy.utils.unregister_class(stale)
            except RuntimeError:
                pass

    try:
        for cls in classes:
            # Covers register() being invoked twice on the same module object.
            if getattr(cls, "is_registered", False):
                continue
            _materialize_blender_properties(cls)
            bpy.utils.register_class(cls)
            _REGISTERED.append(cls)

        props.register_pointers()
        bpy.types.TOPBAR_MT_file_import.append(_menu_import)
        bpy.types.TOPBAR_MT_file_export.append(_menu_export)

        # The bot lines and the minimap are draw handlers, not registered
        # classes, so they need explicit lifecycle handling.
        from .operators import minimap, race_ai

        race_ai.register_overlay()
        minimap.register_overlay()
    except Exception:
        # Never leave Blender half-registered: otherwise the next enable fails
        # on the first surviving class with "already registered as a subclass".
        try:
            from .operators import minimap, race_ai

            race_ai.unregister_overlay()
            minimap.teardown()
        except Exception:  # noqa: BLE001 - rollback must continue
            pass
        _safe_remove_menu(bpy.types.TOPBAR_MT_file_export, _menu_export)
        _safe_remove_menu(bpy.types.TOPBAR_MT_file_import, _menu_import)
        try:
            props.unregister_pointers()
        except Exception:  # noqa: BLE001 - rollback must continue
            pass
        _rollback_classes(bpy)
        raise


def unregister():
    import bpy

    from . import props

    try:
        from .operators import minimap, race_ai

        race_ai.unregister_overlay()
        minimap.teardown()
    except Exception:  # noqa: BLE001 - unregistering must not fail
        pass

    _safe_remove_menu(bpy.types.TOPBAR_MT_file_export, _menu_export)
    _safe_remove_menu(bpy.types.TOPBAR_MT_file_import, _menu_import)
    try:
        props.unregister_pointers()
    except Exception:  # noqa: BLE001 - unregistering must not fail
        pass

    # A running extraction keeps a timer; stop both with the addon.
    try:
        from .operators import rom_assets

        rom_assets.teardown()
    except Exception:  # noqa: BLE001 - unregistering must not fail
        pass

    # The texture browser holds Blender preview collections, which are not
    # registered classes and therefore need their own teardown.
    try:
        from .operators import skybox, textures

        textures.teardown()
        skybox.teardown()
    except Exception:  # noqa: BLE001 - unregistering must not fail
        pass

    _rollback_classes(bpy)

    # If Blender reloaded the Python modules, _REGISTERED may belong to the
    # previous module instance and be empty here.  Remove stale classes from
    # this addon by name as a final fallback, in reverse dependency order.
    try:
        classes = _module_classes()
    except Exception:  # noqa: BLE001
        classes = ()
    for cls in reversed(classes):
        stale = _same_addon_registered_class(bpy, cls)
        if stale is not None:
            try:
                bpy.utils.unregister_class(stale)
            except RuntimeError:
                pass
