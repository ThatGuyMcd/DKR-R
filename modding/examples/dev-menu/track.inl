static int json_vector(const char* bytes, DkrJsonToken* tokens, unsigned array, float* values) {
    if (tokens[array].type != DKR_JSON_ARRAY)
        return 0;
    unsigned at = tokens[array].child;
    for (unsigned i = 0; i < 3; ++i) {
        if (at >= tokens[array].next || !dkr_json_real(bytes, &tokens[at], &values[i]))
            return 0;
        at = tokens[at].next;
    }
    return at == tokens[array].next;
}
static int inspect_geometry(unsigned segment, unsigned triangle, unsigned mode) {
    if (mode > 2 || segment > 1023)
        return DKR2_INVALID;
    if (!mode) {
        geometry_count = 0;
        return 0;
    }
    char name[24], bytes[2048];
    DkrJsonToken tokens[160];
    DkrJson key = {name, sizeof(name), 0, 0};
    dkr_json_raw(&key, "triangles/");
    dkr_json_unsigned(&key, segment);
    const int read =
        dkr2_catalogue(mode == 1 ? "segments" : name, mode == 1 ? segment : triangle, bytes, sizeof(bytes));
    if (read < 0 || read == DKR2_END)
        return read;
    if (dkr_json_parse(bytes, (unsigned)read - 1, tokens, 160) <= 0)
        return DKR2_INVALID;
    Dkr2Debug next[4];
    unsigned count = 0;
    if (mode == 1) {
        const int low = dkr_json_member(bytes, tokens, 0, "minimum"),
                  high = dkr_json_member(bytes, tokens, 0, "maximum");
        if (low < 0 || high < 0)
            return DKR2_INVALID;
        next[0] = (Dkr2Debug){.size = sizeof(Dkr2Debug), .kind = DKR2_DEBUG_BOX, .rgba = 0x55bbffffU};
        if (!json_vector(bytes, tokens, (unsigned)low, next[0].a) ||
            !json_vector(bytes, tokens, (unsigned)high, next[0].b))
            return DKR2_INVALID;
        count = 1;
    } else {
        const int vertices = dkr_json_member(bytes, tokens, 0, "vertices"),
                  normal = dkr_json_member(bytes, tokens, 0, "normal");
        if (vertices < 0 || normal < 0 || tokens[vertices].type != DKR_JSON_ARRAY)
            return DKR2_INVALID;
        float v[3][3], n[3];
        unsigned at = tokens[vertices].child;
        for (unsigned i = 0; i < 3; ++i) {
            if (at >= tokens[vertices].next || !json_vector(bytes, tokens, at, v[i]))
                return DKR2_INVALID;
            at = tokens[at].next;
        }
        if (at != tokens[vertices].next || !json_vector(bytes, tokens, (unsigned)normal, n))
            return DKR2_INVALID;
        for (unsigned i = 0; i < 4; ++i) {
            next[i] = (Dkr2Debug){
                .size = sizeof(Dkr2Debug), .kind = DKR2_DEBUG_LINE, .rgba = i == 3 ? 0xff8855ffU : 0x55bbffffU};
            for (unsigned axis = 0; axis < 3; ++axis) {
                next[i].a[axis] = i == 3 ? (v[0][axis] + v[1][axis] + v[2][axis]) / 3 : v[i][axis];
                next[i].b[axis] = i == 3 ? next[i].a[axis] + n[axis] * 50 : v[(i + 1) % 3][axis];
            }
        }
        count = 4;
    }
    geometry_count = count;
    for (unsigned i = 0; i < count; ++i)
        geometry[i] = next[i];
    return 0;
}
/* A route document is deliberately one native checkpoint transaction, separate
 * from the all-or-nothing property document. No half-import of two edit kinds. */
static void publish_route_document(void) {
    Dkr2Route node = {.size = sizeof(node)};
    const int r = dkr2_route(route_page, &node);
    if (r) {
        done(r);
        return;
    }
    const unsigned properties[] = {DKR2_YAW, DKR2_PITCH, DKR2_ROLL};
    Dkr2Value angles[3], scale;
    for (unsigned i = 0; i < 3; ++i)
        if (read_value(node.object, properties[i], &angles[i])) {
            done(DKR2_STALE);
            return;
        }
    if (read_value(node.object, DKR2_SCALE, &scale)) {
        done(DKR2_STALE);
        return;
    }
    DkrJson j = {document_bytes, 12000, 0, 0};
    dkr_json_raw(&j, "{\"version\":1,\"checkpoint\":");
    dkr_json_unsigned(&j, route_page);
    key_number(&j, "count", node.count, 1);
    key_number(&j, "main_count", node.main_count, 1);
    dkr_json_raw(&j, ",\"position\":[");
    for (unsigned i = 0; i < 3; ++i) {
        if (i)
            dkr_json_char(&j, ',');
        dkr_json_float(&j, node.position[i]);
    }
    dkr_json_raw(&j, "],\"angles\":[");
    for (unsigned i = 0; i < 3; ++i) {
        if (i)
            dkr_json_char(&j, ',');
        dkr_json_integer(&j, angles[i].integer);
    }
    dkr_json_raw(&j, "],\"scale\":");
    dkr_json_float(&j, scale.real);
    dkr_json_char(&j, '}');
    done(j.failed ? DKR2_BUDGET : dkr2_document_publish("checkpoint-patch", document_bytes, j.used));
}
static int apply_route_document(void) {
    const int bytes = dkr2_document_size("checkpoint-patch");
    if (bytes <= 0 || bytes > 4096)
        return DKR2_INVALID;
    const int got = dkr2_document_read("checkpoint-patch", 0, document_bytes, (unsigned)bytes);
    if (got != bytes)
        return got < 0 ? got : DKR2_INVALID;
    if (dkr_json_parse(document_bytes, (unsigned)bytes, document_tokens, 768) <= 0)
        return DKR2_INVALID;
    unsigned version, index, count, main;
    if (!document_unsigned(0, "version", &version) || version != 1 || !document_unsigned(0, "checkpoint", &index) ||
        !document_unsigned(0, "count", &count) || !document_unsigned(0, "main_count", &main))
        return DKR2_INVALID;
    Dkr2Route node = {.size = sizeof(node)};
    const int r = dkr2_route(index, &node);
    if (r)
        return r;
    if (node.count != count || node.main_count != main)
        return DKR2_CONFLICT;
    const int position = dkr_json_member(document_bytes, document_tokens, 0, "position"),
              angles = dkr_json_member(document_bytes, document_tokens, 0, "angles"),
              scale = dkr_json_member(document_bytes, document_tokens, 0, "scale");
    float p[3], a[3], s;
    if (position < 0 || angles < 0 || scale < 0 ||
        !json_vector(document_bytes, document_tokens, (unsigned)position, p) ||
        !json_vector(document_bytes, document_tokens, (unsigned)angles, a) ||
        !dkr_json_real(document_bytes, &document_tokens[scale], &s))
        return DKR2_INVALID;
    unsigned args[9] = {node.object.slot, node.object.generation, 0, 0, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < 3; ++i) {
        if (a[i] < -32768 || a[i] > 32767 || (float)(int)a[i] != a[i])
            return DKR2_INVALID;
        Word value = {.real = p[i]};
        args[i + 2] = value.bits;
        args[i + 5] = (unsigned)(int)a[i];
    }
    Word size = {.real = s};
    args[8] = size.bits;
    const int result = operate(DKR2_EDIT_CHECKPOINT, args, 9);
    if (!result)
        dkr2_document_accept("checkpoint-patch");
    return result;
}
