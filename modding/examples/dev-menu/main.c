#include "dkr/development.h"
#include "dkr/game.h"
#include "dkr/json.h"
#include "dkr/json_read.h"

/* This is a normal portable package. Every tool mutation goes through the
 * public SDK; there are no host exports or special launcher package IDs. */
enum { HISTORY = 32 };
typedef union Word {
    float real;
    unsigned bits;
    int integer;
} Word;
typedef struct Identity {
    int racer;
    unsigned object_id, behaviour;
    float position[3];
} Identity;
typedef struct Change {
    Dkr2Edit before, after;
    Identity identity;
} Change;
static Dkr2Session session = {.size = sizeof(Dkr2Session)};
static Dkr2Entity racers[8];
static Dkr2Handle target;
static unsigned first, history_count, history_cursor, catalogue_first, packages_first;
static Change history[HISTORY];
static unsigned scene, last_action, last_status, watch_property = 102;
static int freeze_cpu, unlimited_boost, shield, keep_bananas;
static float bookmark[3];
static unsigned bookmark_scene;
static char search[53], report[16384], message[160];
static unsigned tool_pulses, route_page, symbol_page, trace_count;
static unsigned resource_kind, resource_page;
static const char* resource_names[] = {"characters", "character-assets", "models",       "textures-3d", "textures-2d",
                                       "sprites",    "object-templates", "level-models", "cups",        "sounds",
                                       "sequences",  "all-settings",     "hooks",        "callbacks",   "resources"};
static Dkr2Event trace[8];
static Dkr2Hit probe = {.size = sizeof(Dkr2Hit)};
static Dkr2Route route = {.size = sizeof(Dkr2Route)};
static Dkr2Edit persistent[32];
static unsigned persistent_count;
static int apply_document(void);
static void publish_document(void);
static unsigned track_kind, track_page, track_segment;
static const char* track_names[] = {"scene", "segments", "ai-nodes", "spawn-points", "triggers"};
static Dkr2Debug geometry[4];
static unsigned geometry_count;
static int inspect_geometry(unsigned segment, unsigned triangle, unsigned mode);
static int apply_route_document(void);
static void publish_route_document(void);

static int same(Dkr2Handle a, Dkr2Handle b) {
    return a.slot == b.slot && a.generation == b.generation;
}
static void copy(char* to, const char* from, unsigned size) {
    unsigned i = 0;
    for (; i + 1 < size && from[i]; ++i)
        to[i] = from[i];
    to[i] = 0;
}
static const char* status(int result) {
    switch (result) {
    case DKR2_OK:
        return "Done.";
    case DKR2_END:
        return "No entry in this scene.";
    case DKR2_INVALID:
        return "That value or destination is outside the supported range.";
    case DKR2_STALE:
        return "That object has left the scene. Select it again.";
    case DKR2_PHASE:
        return "This needs an active race or a confirmed tool boundary.";
    case DKR2_AUTHORITY:
        return "This needs development-session host authority or a tool-owned object.";
    case DKR2_UNAVAILABLE:
        return "That object type or native operation is not available here.";
    case DKR2_BUDGET:
        return "The operation reached its declared work or object capacity.";
    case DKR2_CONFLICT:
        return "That edit conflicts with another edit or is already queued.";
    case DKR2_RELOAD:
        return "This needs resource preparation and a scene reload.";
    default:
        return "The operation was not accepted.";
    }
}
static void done(int result) {
    last_status = (unsigned)result;
    copy(message, status(result), sizeof(message));
}
static int read_value(Dkr2Handle handle, unsigned property, Dkr2Value* result) {
    Dkr2Read value = {handle, property, {.bits = 0}, 0};
    int r = dkr2_read(&value, 1);
    if (r == 0)
        r = value.status;
    if (r == 0)
        *result = value.value;
    return r;
}
static int edit(Dkr2Handle handle, unsigned property, Dkr2Value value, int remember) {
    Dkr2Value old;
    int result = read_value(handle, property, &old);
    if (result)
        return result;
    Dkr2Edit patch = {handle, property, value};
    result = dkr2_edit(&patch, 1);
    if (!result && remember) {
        if (history_cursor == HISTORY) {
            for (unsigned i = 1; i < HISTORY; ++i)
                history[i - 1] = history[i];
            --history_cursor;
        }
        history[history_cursor].before = (Dkr2Edit){handle, property, old};
        history[history_cursor].after = patch;
        Dkr2Entity entity = {.size = sizeof(Dkr2Entity)};
        Identity* id = &history[history_cursor].identity;
        id->racer = -1;
        if (dkr2_entities(handle.slot - 1, &entity, 1) == 1 && same(handle, entity.handle)) {
            id->object_id = entity.object_id;
            id->behaviour = entity.behaviour;
            id->position[0] = entity.x;
            id->position[1] = entity.y;
            id->position[2] = entity.z;
            Dkr2Value index;
            if (entity.racer && read_value(handle, DKR2_RACER_INDEX, &index) == 0)
                id->racer = index.integer;
        }
        ++history_cursor;
        history_count = history_cursor;
    }
    return result;
}
static int stop(Dkr2Handle handle) {
    Dkr2Edit values[5] = {{handle, DKR2_VELOCITY_X, {.bits = 0}},
                          {handle, DKR2_VELOCITY_Y, {.bits = 0}},
                          {handle, DKR2_VELOCITY_Z, {.bits = 0}},
                          {handle, DKR2_RACER_SPEED, {.bits = 0}},
                          {handle, DKR2_RACER_LATERAL_SPEED, {.bits = 0}}};
    Dkr2Value racer;
    const unsigned n = read_value(handle, DKR2_RACER_SPEED, &racer) == 0 ? 5 : 3;
    return dkr2_edit(values, n);
}
static int operate(unsigned op, const unsigned* source, unsigned source_count) {
    unsigned args[16] = {0}, out[16] = {0};
    for (unsigned i = 0; i < source_count && i < 16; ++i)
        args[i] = source[i];
    const int result = dkr2_operation(op, args, out);
    done(result);
    return result;
}
static void action(const Dkr2Action* command) {
    const unsigned* a = command->arguments;
    if (a[0] && a[1])
        target = (Dkr2Handle){a[0], a[1]};
    last_action = command->id;
    switch (command->action) {
    case 1:
        done(0);
        break;
    case 2: {
        freeze_cpu = unlimited_boost = shield = keep_bananas = 0;
        history_count = history_cursor = 0;
        persistent_count = 0;
        dkr2_override(persistent, 0);
        Dkr2Tune reset = {.size = sizeof(reset)};
        dkr2_tune(&reset);
        done(dkr2_freeze_cpus(0));
        break;
    }
    case 10:
        first = a[2] <= 488 ? a[2] : 488;
        done(0);
        break;
    case 11:
        for (unsigned i = 0; i < 40; ++i) {
            const unsigned word = a[2 + i / 4], shift = 24 - (i % 4) * 8;
            search[i] = (char)(word >> shift);
        }
        search[40] = 0;
        done(0);
        break;
    case 12:
        catalogue_first = a[2];
        done(0);
        break;
    case 13:
        packages_first = a[2];
        done(0);
        break;
    case 14:
        route_page = a[2];
        route.size = sizeof(route);
        done(dkr2_route(route_page, &route));
        break;
    case 15:
        symbol_page = a[2];
        done(0);
        break;
    case 16:
        if (a[2] >= sizeof(resource_names) / sizeof(resource_names[0]))
            done(DKR2_INVALID);
        else {
            resource_kind = a[2];
            resource_page = a[3];
            done(0);
        }
        break;
    case 17:
        if (a[2] > 5 || a[4] > 1023)
            done(DKR2_INVALID);
        else {
            track_kind = a[2];
            track_page = a[3];
            track_segment = a[4];
            done(0);
        }
        break;
    case 18:
        done(inspect_geometry(a[2], a[3], a[4]));
        break;
    case 20:
        done(dkr2_control(1, 0));
        break;
    case 21:
        done(dkr2_control(0, 0));
        break;
    case 22:
        done(dkr2_control(2, 0));
        break;
    case 23:
        done(dkr2_control(3, a[2] + 1));
        break;
    case 100:
        watch_property = a[2];
        done(0);
        break;
    case 101: {
        Dkr2Value value = {.bits = a[3]};
        done(edit(target, a[2], value, 1));
        break;
    }
    case 102:
        if (history_cursor) {
            const int r = dkr2_edit(&history[history_cursor - 1].before, 1);
            if (!r)
                --history_cursor;
            done(r);
        } else
            done(1);
        break;
    case 103:
        if (history_cursor < history_count) {
            const int r = dkr2_edit(&history[history_cursor].after, 1);
            if (!r)
                ++history_cursor;
            done(r);
        } else
            done(1);
        break;
    case 104:
        done(stop(target));
        break;
    case 105: {
        Dkr2Value value = {.bits = a[2]};
        done(edit(target, DKR2_RACER_BANANAS, value, 1));
        break;
    }
    case 106: {
        Dkr2Value value = {.bits = a[2]};
        done(edit(target, DKR2_RACER_AI_SKILL, value, 1));
        break;
    }
    case 107: {
        Dkr2Value value = {.bits = a[2]};
        done(edit(target, DKR2_RACER_AMMO, value, 1));
        break;
    }
    case 110:
    case 111:
    case 112:
        operate(command->action == 110   ? DKR2_TELEPORT
                : command->action == 111 ? DKR2_CLONE_SCENERY
                                         : DKR2_DELETE_SPAWN,
                a, 5);
        break;
    case 120: {
        Dkr2Value x, y, z;
        int r = read_value(target, DKR2_X, &x);
        if (!r)
            r = read_value(target, DKR2_Y, &y);
        if (!r)
            r = read_value(target, DKR2_Z, &z);
        if (!r) {
            bookmark[0] = x.real;
            bookmark[1] = y.real;
            bookmark[2] = z.real;
            bookmark_scene = session.scene_generation;
        }
        done(r);
        break;
    }
    case 121: {
        if (bookmark_scene != session.scene_generation) {
            done(DKR2_STALE);
            break;
        }
        unsigned args[5] = {target.slot, target.generation, 0, 0, 0};
        for (unsigned i = 0; i < 3; ++i) {
            Word w = {.real = bookmark[i]};
            args[i + 2] = w.bits;
        }
        operate(DKR2_TELEPORT, args, 5);
        break;
    }
    case 130:
        operate(DKR2_SET_FOG, a + 2, 6);
        break;
    case 131:
        operate(DKR2_SET_WEATHER, a + 2, 6);
        break;
    case 140:
    case 141:
    case 142:
    case 143:
        operate(DKR2_SOUND + command->action - 140, a + 2, 1);
        break;
    case 150: {
        unsigned args[3] = {0, 0, 0};
        operate(DKR2_RESTART_SCENE, args, 3);
        break;
    }
    case 153: {
        unsigned args[3] = {0, a[2], 0};
        operate(DKR2_RESTART_SCENE, args, 3);
        break;
    }
    case 154:
        operate(DKR2_SET_RACE_LAPS, a + 2, 1);
        break;
    case 151: {
        unsigned args[3] = {a[2], a[4], a[3]};
        operate(DKR2_WARP_COURSE, args, 3);
        break;
    }
    case 152:
        operate(DKR2_RETURN_MENU, a + 2, 1);
        break;
    case 160: {
        const int next = !freeze_cpu;
        const int result = dkr2_freeze_cpus((unsigned)next);
        if (!result)
            freeze_cpu = next;
        done(result);
        break;
    }
    case 161:
        unlimited_boost = !unlimited_boost;
        done(0);
        break;
    case 162:
        shield = !shield;
        done(0);
        break;
    case 163:
        keep_bananas = !keep_bananas;
        done(0);
        break;
    case 170: {
        Dkr2Cast cast = {.size = sizeof(Dkr2Cast), .segment = 0xffffffffU, .flags = a[9], .radius = 0};
        Word radius = {.bits = a[8]};
        cast.radius = radius.real;
        for (unsigned i = 0; i < 3; ++i) {
            Word from = {.bits = a[2 + i]}, to = {.bits = a[5 + i]};
            cast.from[i] = from.real;
            cast.to[i] = to.real;
        }
        probe.size = sizeof(probe);
        done(dkr2_cast(&cast, &probe));
        break;
    }
    case 171: {
        Dkr2Route node = {.size = sizeof(Dkr2Route)};
        int r = dkr2_route(a[2], &node);
        if (r) {
            done(r);
            break;
        }
        unsigned args[9] = {node.object.slot, node.object.generation, a[3], a[4], a[5], a[6], a[7], a[8], a[9]};
        operate(DKR2_EDIT_CHECKPOINT, args, 9);
        break;
    }
    case 179:
        publish_document();
        done(0);
        break;
    case 180:
        done(apply_document());
        break;
    case 183:
        publish_route_document();
        break;
    case 184:
        done(apply_route_document());
        break;
    case 181: {
        Dkr2Tune reset = {.size = sizeof(reset)};
        done(dkr2_tune(&reset));
        break;
    }
    case 182: {
        Dkr2Value vehicle;
        const int r = read_value(target, DKR2_RACER_VEHICLE, &vehicle);
        if (r) {
            done(r);
            break;
        }
        Dkr2Tune tune = {.size = sizeof(tune), .handle = target, .mask = vehicle.integer > 2 ? 11U : 15U};
        Word weight = {.bits = a[2]}, handling = {.bits = a[3]}, acceleration = {.bits = a[4]}, cap = {.bits = a[5]};
        tune.weight = weight.real;
        tune.handling = handling.real;
        tune.acceleration = acceleration.real;
        tune.speed_cap = cap.real;
        done(dkr2_tune(&tune));
        break;
    }
    case 190:
    case 191: {
        const unsigned property = command->action == 190 ? DKR2_AMBIENT : DKR2_DIFFUSE;
        Dkr2Edit edits[32];
        for (unsigned i = 0; i < persistent_count; ++i)
            edits[i] = persistent[i];
        unsigned at = 0;
        while (at < persistent_count && !(same(edits[at].handle, target) && edits[at].property == property))
            ++at;
        if (at == 32) {
            done(DKR2_BUDGET);
            break;
        }
        edits[at] = (Dkr2Edit){target, property, {.bits = a[2]}};
        const unsigned n = at == persistent_count ? persistent_count + 1 : persistent_count;
        const int result = dkr2_override(edits, n);
        if (!result) {
            persistent_count = n;
            for (unsigned i = 0; i < n; ++i)
                persistent[i] = edits[i];
        }
        done(result);
        break;
    }
    case 200:
    case 201: {
        Dkr2Value value = {.bits = a[4]};
        done(dkr2_setting_apply(a[2], a[3], &value));
        break;
    }
    case 202:
        done(dkr2_setting_apply(a[2], a[3], 0));
        break;
    default:
        done(DKR2_UNAVAILABLE);
        break;
    }
}
static void key_text(DkrJson* j, const char* key, const char* value, int comma) {
    if (comma)
        dkr_json_char(j, ',');
    dkr_json_key(j, key);
    dkr_json_text(j, value);
}
static void key_number(DkrJson* j, const char* key, unsigned value, int comma) {
    if (comma)
        dkr_json_char(j, ',');
    dkr_json_key(j, key);
    dkr_json_unsigned(j, value);
}
#include "documents.inl"
#include "track.inl"
static void publish_debug(void) {
    static Dkr2Debug primitives[64];
    unsigned count = 0;
    for (unsigned view = 0; view < 4; ++view) {
        for (unsigned i = 0; i < geometry_count; ++i) {
            primitives[count] = geometry[i];
            primitives[count++].view = view;
        }
        for (unsigned i = route_page; !geometry_count && i < route_page + 6 && i < 127; ++i) {
            Dkr2Route node = {.size = sizeof(node)}, next = {.size = sizeof(next)};
            if (dkr2_route(i, &node))
                break;
            unsigned following = i < node.main_count ? i + 1 : (unsigned)(node.alternate + 1);
            if (following >= node.main_count)
                following = 0;
            if (dkr2_route(following, &next))
                continue;
            Dkr2Debug* line = &primitives[count++];
            *line = (Dkr2Debug){.size = sizeof(*line), .kind = DKR2_DEBUG_LINE, .rgba = 0x44ff88ffU, .view = view};
            for (unsigned axis = 0; axis < 3; ++axis) {
                line->a[axis] = node.position[axis];
                line->b[axis] = next.position[axis];
            }
            Dkr2Debug* label = &primitives[count++];
            *label = *line;
            label->kind = DKR2_DEBUG_LABEL;
            DkrJson caption = {label->label, sizeof(label->label), 0, 0};
            dkr_json_raw(&caption, "Checkpoint ");
            dkr_json_unsigned(&caption, i);
        }
        Dkr2Value x, y, z;
        if (read_value(target, DKR2_X, &x) == 0 && read_value(target, DKR2_Y, &y) == 0 &&
            read_value(target, DKR2_Z, &z) == 0) {
            const float point[] = {x.real, y.real, z.real};
            Dkr2Debug* box = &primitives[count++];
            *box = (Dkr2Debug){.size = sizeof(*box), .kind = DKR2_DEBUG_BOX, .rgba = 0xffcc44ffU, .view = view};
            for (unsigned axis = 0; axis < 3; ++axis) {
                box->a[axis] = point[axis] - 20;
                box->b[axis] = point[axis] + 20;
            }
            Dkr2Debug* label = &primitives[count++];
            *label = *box;
            label->kind = DKR2_DEBUG_LABEL;
            copy(label->label, "Selected entity marker", sizeof(label->label));
        }
        if (probe.hit) {
            Dkr2Debug* normal = &primitives[count++];
            *normal = (Dkr2Debug){.size = sizeof(*normal), .kind = DKR2_DEBUG_LINE, .rgba = 0xff6644ffU, .view = view};
            for (unsigned axis = 0; axis < 3; ++axis) {
                normal->a[axis] = probe.point[axis];
                normal->b[axis] = probe.point[axis] + probe.normal[axis] * 50;
            }
            Dkr2Debug* label = &primitives[count++];
            *label = *normal;
            label->kind = DKR2_DEBUG_LABEL;
            copy(label->label, "Probe hit / normal", sizeof(label->label));
        }
    }
    dkr2_debug(primitives, count);
}
static void publish(void) {
    DkrJson j = {report, sizeof(report), 0, 0};
    dkr_json_char(&j, '{');
    key_text(&j, "message", message, 0);
    key_number(&j, "last_action", last_action, 1);
    key_number(&j, "tick", session.tick, 1);
    key_number(&j, "scene", session.scene_generation, 1);
    key_number(&j, "entity_count", session.entities, 1);
    key_number(&j, "racer_count", session.racers, 1);
    key_text(&j, "save_status", "Development progress is isolated. Ordinary offline saves are not edited.", 1);
    key_text(&j, "authority",
             session.online ? "Online: commands come from the host's final input stream."
                            : "Offline development session.",
             1);
    key_text(
        &j, "settings-policy",
        "Only API 2 options explicitly listed as live by their author may change now. API 1 and resource-changing settings require relaunch.",
        1);
    key_number(&j, "instructions", session.instruction_budget, 1);
    key_number(&j, "native_calls", session.native_budget, 1);
    key_text(&j, "overrides",
             freeze_cpu        ? "CPUs frozen (resume before continuing a race)."
             : unlimited_boost ? "Continuous boost active."
             : shield          ? "Temporary shield active."
             : keep_bananas    ? "Banana override active."
                               : "No persistent racer overrides.",
             1);
    for (unsigned op = 1; op <= 40; ++op)
        if (op <= 4 || (op >= 10 && op <= 11) || (op >= 20 && op <= 23) || (op >= 30 && op <= 32) || op == 40) {
            char key[8] = {'o', 'p', '_', 0};
            DkrJson name = {key, sizeof(key), 3, 0};
            dkr_json_unsigned(&name, op);
            dkr_json_char(&j, ',');
            dkr_json_key(&j, key);
            dkr_json_raw(&j, dkr2_operation_available(op) ? "true" : "false");
        }
    key_text(&j, "target_values", "Select an entity and choose Inspect to watch a property.", 1);
    if (target.slot) {
        Dkr2Value value;
        const int r = read_value(target, watch_property, &value);
        dkr_json_raw(&j, ",\"watch\":");
        if (r)
            dkr_json_text(&j, status(r));
        else {
            Dkr2PropertyInfo property = {.size = sizeof(Dkr2PropertyInfo)};
            unsigned kind = 3;
            for (unsigned i = 0; dkr2_property(i, &property) == 0; ++i)
                if (property.id == watch_property) {
                    kind = property.type;
                    break;
                }
            if (kind == 1)
                dkr_json_float(&j, value.real);
            else if (kind == 2)
                dkr_json_integer(&j, value.integer);
            else
                dkr_json_unsigned(&j, value.bits);
        }
    }
    dkr_json_raw(&j, ",\"packages\":[");
    char record[2048];
    unsigned count = 0;
    for (unsigned i = packages_first; i < packages_first + 8 && i < 128; ++i) {
        const int r = dkr2_catalogue("packages", i, record, sizeof(record));
        if (r == 1)
            break;
        if (r < 0)
            continue;
        if (j.used + (unsigned)r * 6U + 128U > 9500U)
            break;
        if (count++)
            dkr_json_char(&j, ',');
        dkr_json_raw(&j, "{\"name\":\"Package ");
        dkr_json_unsigned(&j, i + 1);
        dkr_json_raw(&j, "\",\"detail\":");
        dkr_json_text(&j, record);
        dkr_json_char(&j, '}');
    }
    dkr_json_raw(&j, "],\"resources\":[");
    count = 0;
    for (unsigned i = resource_page; i < resource_page + 8 && i < 16384; ++i) {
        const int r = dkr2_catalogue(resource_names[resource_kind], i, record, sizeof(record));
        if (r == 1)
            break;
        if (r < 0)
            continue;
        if (j.used + (unsigned)r * 6U + 128U > 9500U)
            break;
        if (count++)
            dkr_json_char(&j, ',');
        dkr_json_raw(&j, "{\"name\":\"Resource ");
        dkr_json_unsigned(&j, i);
        dkr_json_raw(&j, "\",\"detail\":");
        dkr_json_text(&j, record);
        dkr_json_char(&j, '}');
    }
    dkr_json_raw(&j, "],\"courses\":[");
    count = 0;
    for (unsigned i = catalogue_first; i < catalogue_first + 8 && i < 1024; ++i) {
        const int r = dkr2_catalogue("courses", i, record, sizeof(record));
        if (r == 1)
            break;
        if (r < 0)
            continue;
        if (j.used + (unsigned)r * 6U + 128U > 9500U)
            break;
        if (count++)
            dkr_json_char(&j, ',');
        dkr_json_raw(&j, "{\"name\":\"Catalogue index ");
        dkr_json_unsigned(&j, i);
        dkr_json_raw(&j, "\",\"detail\":");
        dkr_json_text(&j, record);
        dkr_json_char(&j, '}');
    }
    dkr_json_raw(&j, "],\"geometry\":[");
    count = 0;
    char triangle_name[24];
    DkrJson tn = {triangle_name, sizeof(triangle_name), 0, 0};
    dkr_json_raw(&tn, "triangles/");
    dkr_json_unsigned(&tn, track_segment);
    for (unsigned i = track_page; i < track_page + 4 && i < 8192; ++i) {
        const int r =
            dkr2_catalogue(track_kind == 5 ? triangle_name : track_names[track_kind], i, record, sizeof(record));
        if (r == 1)
            break;
        if (r < 0)
            continue;
        if (j.used + (unsigned)r * 6U + 128U > 11000U)
            break;
        if (count++)
            dkr_json_char(&j, ',');
        dkr_json_raw(&j, "{\"name\":\"Scene entry ");
        dkr_json_unsigned(&j, i);
        dkr_json_raw(&j, "\",\"detail\":");
        dkr_json_text(&j, record);
        dkr_json_char(&j, '}');
    }
    dkr_json_raw(&j, "],\"routes\":[");
    count = 0;
    for (unsigned i = route_page; i < route_page + 8 && i < 127; ++i) {
        Dkr2Route node = {.size = sizeof(node)};
        if (dkr2_route(i, &node))
            break;
        if (count++)
            dkr_json_char(&j, ',');
        dkr_json_raw(&j, "{\"name\":\"Checkpoint ");
        dkr_json_unsigned(&j, i);
        dkr_json_raw(&j, "\",\"slot\":");
        dkr_json_unsigned(&j, node.object.slot);
        dkr_json_raw(&j, ",\"generation\":");
        dkr_json_unsigned(&j, node.object.generation);
        dkr_json_raw(&j, ",\"detail\":\"XYZ ");
        for (unsigned a = 0; a < 3; ++a) {
            if (a)
                dkr_json_char(&j, ' ');
            dkr_json_float(&j, node.position[a]);
        }
        dkr_json_raw(&j, " | Width ");
        dkr_json_float(&j, node.width);
        dkr_json_raw(&j, " | Alternate ");
        dkr_json_integer(&j, node.alternate);
        dkr_json_raw(&j, "\"}");
    }
    dkr_json_raw(&j, "],\"symbols\":[");
    count = 0;
    for (unsigned i = symbol_page; i < symbol_page + 12; ++i) {
        Dkr2SymbolInfo info = {.size = sizeof(info)};
        if (dkr2_symbols(i, &info))
            break;
        if (count++)
            dkr_json_char(&j, ',');
        dkr_json_raw(&j, "{\"name\":");
        dkr_json_text(&j, info.name);
        dkr_json_raw(&j, ",\"detail\":\"Address ");
        dkr_json_unsigned(&j, info.address);
        dkr_json_raw(&j, " | bytes ");
        dkr_json_unsigned(&j, info.bytes);
        dkr_json_raw(&j, " | call ");
        dkr_json_unsigned(&j, (info.flags & 2) != 0);
        dkr_json_raw(&j, " | hook ");
        dkr_json_unsigned(&j, (info.flags & 4) != 0);
        dkr_json_raw(&j, " | protected ");
        dkr_json_unsigned(&j, (info.flags & 8) != 0);
        dkr_json_raw(&j, "\"}");
    }
    dkr_json_raw(&j, "],\"trace\":[");
    for (unsigned i = 0; i < trace_count; ++i) {
        if (i)
            dkr_json_char(&j, ',');
        dkr_json_raw(&j, "{\"name\":\"Event ");
        dkr_json_unsigned(&j, trace[i].type);
        dkr_json_raw(&j, "\",\"detail\":\"Tick ");
        dkr_json_unsigned(&j, trace[i].tick);
        dkr_json_raw(&j, " | entity ");
        dkr_json_unsigned(&j, trace[i].handle.slot);
        dkr_json_raw(&j, " | value ");
        dkr_json_unsigned(&j, trace[i].value);
        dkr_json_raw(&j, "\"}");
    }
    dkr_json_raw(&j, "],\"probe\":\"");
    if (probe.hit) {
        dkr_json_raw(&j, "Hit fraction ");
        dkr_json_float(&j, probe.fraction);
        dkr_json_raw(&j, " | segment ");
        dkr_json_unsigned(&j, probe.segment);
        dkr_json_raw(&j, " | triangle ");
        dkr_json_unsigned(&j, probe.triangle);
        dkr_json_raw(&j, " | texture/object ID ");
        dkr_json_unsigned(&j, probe.texture);
    } else
        dkr_json_raw(&j, "No probe hit.");
    dkr_json_raw(&j, "\"}");
    if (!j.failed)
        dkr2_ui_publish(report, j.used);
    else {
        const char fallback[] = "{\"message\":\"Tool report reached its byte limit. Choose a smaller resource page.\"}";
        dkr2_ui_publish(fallback, sizeof(fallback) - 1);
    }
}
DKR_CALLBACK(on_boot) void boot(void) {
    copy(message, "Dev Menu ready. Open Tools from the overlay or press F2.", sizeof(message));
    dkr2_events(0x800000feU);
}
/* Reload callbacks may migrate only private VM state. No game writes or SDK
 * world services are legal here. Compatible layouts retain all other state. */
DKR_CALLBACK(on_cleanup) void cleanup(void) {}
DKR_CALLBACK(on_reload) void reloaded(void) {
    last_action = 0;
    tool_pulses = 0;
}
DKR_CALLBACK(on_tool) void tools(void) {
    session.size = sizeof(session);
    if (dkr2_session(&session))
        return;
    Dkr2Action command;
    const int result = dkr2_action_next(&command);
    if (result == 0)
        action(&command);
    else if (result < 0)
        done(result);
    if ((++tool_pulses & 7U) == 0 || result == 0) {
        publish();
        publish_debug();
    }
}
DKR_CALLBACK(on_frame) void tick(void) {
    session.size = sizeof(session);
    if (dkr2_session(&session))
        return;
    if (scene != session.scene_generation) {
        scene = session.scene_generation;
        target = (Dkr2Handle){0, 0};
        history_count = history_cursor = 0;
        first = 0;
        persistent_count = 0;
        geometry_count = 0;
        freeze_cpu = unlimited_boost = shield = keep_bananas = 0;
    }
    for (unsigned i = 0; i < 32; ++i) {
        Dkr2Event event;
        const int r = dkr2_event_next(&event);
        if (r)
            break;
        if (trace_count == 8) {
            for (unsigned j = 1; j < 8; ++j)
                trace[j - 1] = trace[j];
            --trace_count;
        }
        trace[trace_count++] = event;
    }
    if (!session.development)
        return;
    if (!unlimited_boost && !shield && !keep_bananas)
        return;
    for (unsigned i = 0; i < 8; ++i)
        racers[i].size = sizeof(Dkr2Entity);
    const int total = dkr2_entities(DKR2_ENTITIES_RACERS, racers, 8);
    for (unsigned i = 0; total > 0 && i < (unsigned)total; ++i) {
        Dkr2Handle h = racers[i].handle;
        if (same(h, target)) {
            if (unlimited_boost) {
                Dkr2Value value = {.integer = 10};
                edit(h, DKR2_RACER_BOOST_TICKS, value, 0);
            }
            if (shield) {
                Dkr2Value value = {.integer = 300}, type = {.integer = 3};
                edit(h, DKR2_RACER_SHIELD_TYPE, type, 0);
                edit(h, DKR2_RACER_SHIELD_TICKS, value, 0);
            }
            if (keep_bananas) {
                Dkr2Value value = {.integer = 10};
                edit(h, DKR2_RACER_BANANAS, value, 0);
            }
        }
    }
}
