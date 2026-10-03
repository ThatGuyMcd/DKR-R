"""Run the pre-export checks and show what they found."""

from __future__ import annotations

import traceback

import bpy

from .. import catalog as catalog_module, level_types, scene, validate


def grid_issues(context, key) -> list:
    """Start grids built for another level type, which the object map cannot see.

    A grid root is not exported, so :mod:`validate` never meets one; but a root
    built for a race that now sits in a boss race holds eight start positions
    where the game reads two, and the author should hear it here.
    """
    issues = []
    wanted = level_types.spawn_count(key)
    for root in scene.grid_roots(context):
        built = root.get(scene.PROP_GRID_KEY)
        if built == key or level_types.spawn_count(built) == wanted:
            continue
        orders = [int(c.get("dkr_order", -1)) for c in root.children
                  if scene.is_dkr_object(c)]
        issues.append(validate.Issue(
            validate.WARNING,
            '"%s" was built for %s: %d positions, this level uses %d.'
            % (root.name, level_types.label(built),
               level_types.spawn_count(built), wanted),
            level_types.SETUPPOINT, objects=orders,
        ))
    return issues


class DKR_OT_validate(bpy.types.Operator):
    """Check the track for problems that are hard to diagnose in game, using the
    rules of its level type"""

    bl_idname = "dkr.validate"
    bl_label = "Validate Track"
    bl_options = {"REGISTER"}

    def execute(self, context):
        settings = context.scene.dkr
        key = level_types.current_key(settings)
        try:
            catalog = catalog_module.load()
            object_map = scene.export_object_map(context, catalog, resolve_scroll=False)
            report = validate.validate(
                object_map, catalog, level_key=key or level_types.NONE,
            )
            if key:
                report = validate.Report(list(report) + grid_issues(context, key))
            from . import minimap, waterfall
            report = validate.Report(list(report) + waterfall.issues(context)
                                     + minimap.issues(context))
        except Exception as error:  # noqa: BLE001
            traceback.print_exc()
            self.report({"ERROR"}, "validation failed: %s" % error)
            return {"CANCELLED"}

        settings.results.clear()
        for issue in report:
            entry = settings.results.add()
            entry.severity = issue.severity
            entry.message = issue.message
            entry.object_id = issue.object_id
            entry.objects = ",".join(
                str(o) for o in issue.objects if o is not None
            )
        settings.has_validated = True

        if report.errors:
            self.report(
                {"ERROR"},
                "%d error(s), %d warning(s)" % (len(report.errors), len(report.warnings)),
            )
        elif report.warnings:
            self.report({"WARNING"}, "%d warning(s)" % len(report.warnings))
        else:
            self.report({"INFO"}, "no problems found")
        return {"FINISHED"}


class DKR_OT_select_issue(bpy.types.Operator):
    """Select the objects a validation result is about"""

    bl_idname = "dkr.select_issue"
    bl_label = "Select"
    bl_options = {"REGISTER", "UNDO"}

    objects: bpy.props.StringProperty(options={"HIDDEN"})

    def execute(self, context):
        wanted = {int(p) for p in self.objects.split(",") if p.strip().isdigit()}
        if not wanted:
            self.report({"WARNING"}, "this result does not name any object")
            return {"CANCELLED"}

        found = None
        for obj in scene.iter_dkr_objects(context):
            match = int(obj.get("dkr_order", -1)) in wanted
            obj.select_set(match)
            if match:
                found = obj
        if found is None:
            self.report({"ERROR"}, "those objects are no longer in the scene")
            return {"CANCELLED"}

        context.view_layer.objects.active = found
        # Frame them, since a duplicate is usually sitting exactly on top of the
        # object it was copied from and is invisible until the view moves.
        for area in context.screen.areas:
            if area.type == "VIEW_3D":
                with context.temp_override(area=area, region=area.regions[-1]):
                    try:
                        bpy.ops.view3d.view_selected()
                    except RuntimeError:
                        pass
                break
        self.report({"INFO"}, "selected %d object(s)" % len(wanted))
        return {"FINISHED"}


CLASSES = (DKR_OT_validate, DKR_OT_select_issue)
