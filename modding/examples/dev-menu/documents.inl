/* The sample's portable scene document contains reviewed property edits and
 * a test-position bookmark, not a dump of guest RAM or copyrighted assets.
 * Static objects resolve by type/ID/original position; racers by native index.
 * Ambiguous/missing targets reject the WHOLE property batch before any write. */
static char document_bytes[16384];
static DkrJsonToken document_tokens[768];
static unsigned document_unsigned(unsigned object, const char* key, unsigned* output) {
    const int at = dkr_json_member(document_bytes, document_tokens, object, key);
    return at >= 0 && dkr_json_u32(document_bytes, &document_tokens[at], output);
}
static void publish_document(void) {
    DkrJson j = {document_bytes, 12000, 0, 0};
    dkr_json_raw(&j, "{\"version\":1,\"edits\":[");
    unsigned count = 0;
    for (unsigned i = 0; i < history_cursor; ++i) {
        int replaced = 0;
        for (unsigned n = i + 1; n < history_cursor; ++n)
            if (same(history[i].after.handle, history[n].after.handle) &&
                history[i].after.property == history[n].after.property)
                replaced = 1;
        if (replaced)
            continue;
        const Change* entry = &history[i];
        if (count++)
            dkr_json_char(&j, ',');
        dkr_json_raw(&j, "{\"racer\":");
        dkr_json_integer(&j, entry->identity.racer);
        key_number(&j, "object_id", entry->identity.object_id, 1);
        key_number(&j, "behaviour", entry->identity.behaviour, 1);
        dkr_json_raw(&j, ",\"position\":[");
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (axis)
                dkr_json_char(&j, ',');
            dkr_json_float(&j, entry->identity.position[axis]);
        }
        dkr_json_char(&j, ']');
        key_number(&j, "property", entry->after.property, 1);
        key_number(&j, "bits", entry->after.value.bits, 1);
        dkr_json_char(&j, '}');
    }
    dkr_json_raw(&j, "],\"bookmark\":[");
    for (unsigned i = 0; i < 3; ++i) {
        if (i)
            dkr_json_char(&j, ',');
        dkr_json_float(&j, bookmark[i]);
    }
    dkr_json_raw(&j, "]}");
    if (!j.failed) {
        const int result = dkr2_document_publish("scene-patch", document_bytes, j.used);
        if (result < 0)
            done(result);
    }
}
static int apply_document(void) {
    const int size = dkr2_document_size("scene-patch");
    if (size <= 0 || size > 16384)
        return DKR2_INVALID;
    for (unsigned at = 0; at < (unsigned)size; at += 4096) {
        unsigned bytes = (unsigned)size - at;
        if (bytes > 4096)
            bytes = 4096;
        const int read = dkr2_document_read("scene-patch", at, document_bytes + at, bytes);
        if (read != (int)bytes)
            return read < 0 ? read : DKR2_INVALID;
    }
    const int parsed = dkr_json_parse(document_bytes, (unsigned)size, document_tokens, 768);
    if (parsed <= 0)
        return DKR2_INVALID;
    unsigned version = 0;
    if (!document_unsigned(0, "version", &version) || version != 1)
        return DKR2_INVALID;
    const int edits = dkr_json_member(document_bytes, document_tokens, 0, "edits");
    if (edits < 0 || document_tokens[edits].type != DKR_JSON_ARRAY)
        return DKR2_INVALID;
    Dkr2Edit pending[32];
    unsigned count = 0;
    for (unsigned row = document_tokens[edits].child; row < document_tokens[edits].next;
         row = document_tokens[row].next) {
        if (count == 32 || document_tokens[row].type != DKR_JSON_OBJECT)
            return DKR2_BUDGET;
        unsigned object_id = 0, behaviour = 0, property = 0, bits = 0;
        if (!document_unsigned(row, "object_id", &object_id) || !document_unsigned(row, "behaviour", &behaviour) ||
            !document_unsigned(row, "property", &property) || !document_unsigned(row, "bits", &bits))
            return DKR2_INVALID;
        const int racer_key = dkr_json_member(document_bytes, document_tokens, row, "racer"),
                  position_key = dkr_json_member(document_bytes, document_tokens, row, "position");
        float racer = -1, position[3];
        if (racer_key < 0 || position_key < 0 || !dkr_json_real(document_bytes, &document_tokens[racer_key], &racer) ||
            document_tokens[position_key].type != DKR_JSON_ARRAY)
            return DKR2_INVALID;
        unsigned token = document_tokens[position_key].child;
        for (unsigned i = 0; i < 3; ++i) {
            if (token >= document_tokens[position_key].next ||
                !dkr_json_real(document_bytes, &document_tokens[token], &position[i]))
                return DKR2_INVALID;
            token = document_tokens[token].next;
        }
        if (token != document_tokens[position_key].next)
            return DKR2_INVALID;
        Dkr2Handle resolved = {0, 0};
        unsigned first_slot = 0;
        Dkr2Entity page[32];
        while (first_slot < 512) {
            for (unsigned i = 0; i < 32; ++i)
                page[i].size = sizeof(Dkr2Entity);
            const int n = dkr2_entities(first_slot, page, 32);
            if (n < 0)
                return n;
            if (!n)
                break;
            for (unsigned i = 0; i < (unsigned)n; ++i) {
                const Dkr2Entity* candidate = &page[i];
                int match = 0;
                if (racer >= 0) {
                    Dkr2Value index;
                    match = candidate->racer && read_value(candidate->handle, DKR2_RACER_INDEX, &index) == 0 &&
                            index.integer == (int)racer;
                } else if (candidate->object_id == object_id && candidate->behaviour == behaviour) {
                    const float x = candidate->x - position[0], y = candidate->y - position[1],
                                z = candidate->z - position[2];
                    match = x * x + y * y + z * z < 1.0f;
                }
                if (match) {
                    if (resolved.slot)
                        return DKR2_CONFLICT;
                    resolved = candidate->handle;
                }
            }
            first_slot = page[n - 1].handle.slot;
        }
        if (!resolved.slot)
            return DKR2_STALE;
        pending[count++] = (Dkr2Edit){resolved, property, {.bits = bits}};
    }
    float next_bookmark[3] = {bookmark[0], bookmark[1], bookmark[2]};
    const int mark = dkr_json_member(document_bytes, document_tokens, 0, "bookmark");
    if (mark >= 0) {
        if (document_tokens[mark].type != DKR_JSON_ARRAY)
            return DKR2_INVALID;
        unsigned at = document_tokens[mark].child;
        for (unsigned i = 0; i < 3; ++i) {
            if (at >= document_tokens[mark].next ||
                !dkr_json_real(document_bytes, &document_tokens[at], &next_bookmark[i]) || next_bookmark[i] < -1e6f ||
                next_bookmark[i] > 1e6f)
                return DKR2_INVALID;
            at = document_tokens[at].next;
        }
        if (at != document_tokens[mark].next)
            return DKR2_INVALID;
    }
    const int result = dkr2_edit(pending, count);
    if (result)
        return result;
    for (unsigned i = 0; i < 3; ++i)
        bookmark[i] = next_bookmark[i];
    bookmark_scene = session.scene_generation;
    history_count = history_cursor = 0;
    dkr2_document_accept("scene-patch");
    return DKR2_OK;
}
