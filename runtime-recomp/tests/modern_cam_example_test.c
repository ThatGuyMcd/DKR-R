/*
 * example_test.c - exemplo de uso + testes de comportamento da modern_cam.
 *
 * gcc -std=c99 -O2 -Wall -Wextra example_test.c modern_cam.c -lm -o cam_test && ./cam_test
 */
#include <math.h>
#include <stdio.h>

#include "modern_cam.h"

static int g_fail = 0;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("  FALHOU: " __VA_ARGS__);                                                                          \
            printf("\n");                                                                                              \
            g_fail = 1;                                                                                                \
        }                                                                                                              \
    } while (0)

static float wrapf(float a) {
    while (a > 3.14159265f)
        a -= 6.28318531f;
    while (a <= -3.14159265f)
        a += 6.28318531f;
    return a;
}

static int finite_out(const MCOutput* o) {
    return isfinite(o->eye.x) && isfinite(o->eye.y) && isfinite(o->eye.z) && isfinite(o->target.x) &&
           isfinite(o->target.y) && isfinite(o->target.z) && isfinite(o->up.x) && isfinite(o->up.y) &&
           isfinite(o->up.z) && isfinite(o->fovDeg) && isfinite(o->yaw) && isfinite(o->pitch);
}

static float hdist(MCVec3 a, MCVec3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

static MCInput base_input(void) {
    MCInput in;
    in.pos.x = in.pos.y = in.pos.z = 0.0f;
    in.vel.x = in.vel.y = in.vel.z = 0.0f;
    in.yaw = 0.0f;
    in.slopePitch = 0.0f;
    in.grounded = 1;
    in.drifting = 0;
    in.boost = 0.0f;
    in.stickX = in.stickY = 0.0f;
    in.lookBack = 0;
    in.steer = 0.0f;
    return in;
}

/* Os valores exportados da Bancada ModernCam. */
static void bancada_params(MCParams* p) {
    ModernCam_DefaultParams(p);
    p->distMin = 5.9f;
    p->heightMin = 1.35f;
    p->yawFreq = 2.3f;
    p->boostTrauma = 0.35f;
    p->landingTrauma = 0.81f;
    p->enableSteerLead = 1;
    p->steerLead = 0.2007f;
    p->enablePosSpring = 1;
    p->posFreq = 2.3f;
    p->posDamping = 0.98f;
    p->enableLandingDip = 1;
    p->landingDip = 1.65f;
    p->dipFreq = 5.6f;
    p->dipDamping = 0.67f;
    p->enableBoostKick = 1;
    p->boostKick = 1.8f;
    p->kickFreq = 2.3f;
    p->kickDamping = 0.5f;
    p->rotationalShake = 1;
}

/* Curva de raio constante com escorregamento opcional; posição exata (sem erro de integração). */
static MCInput circle_input(float t, float speed, float yawRate, float slip, int drifting) {
    MCInput in = base_input();
    float R = speed / yawRate;
    float velYaw = yawRate * t;           /* direção do movimento */
    in.pos.x = R * (1.0f - cosf(velYaw)); /* começa em 0, indo para +z, virando para +x */
    in.pos.z = R * sinf(velYaw);
    in.vel.x = speed * sinf(velYaw);
    in.vel.z = speed * cosf(velYaw);
    in.yaw = velYaw + slip; /* nariz aponta para dentro durante o drift */
    in.drifting = drifting;
    return in;
}

static MCOutput run_circle_p(const MCParams* custom, float dt, float duration, float speed, float yawRate, float slip,
                             int drifting, MCInput* lastIn) {
    MCParams p;
    MCState s;
    MCOutput o;
    int n = (int)(duration / dt + 0.5f), i;
    if (custom) {
        p = *custom;
    } else {
        ModernCam_DefaultParams(&p);
    }
    s.initialized = 0;
    for (i = 1; i <= n; ++i) {
        MCInput in = circle_input(i * dt, speed, yawRate, slip, drifting);
        ModernCam_Update(&s, &p, &in, dt, NULL, NULL, &o);
        CHECK(finite_out(&o), "saida nao finita no tick %d", i);
        *lastIn = in;
    }
    return o;
}

static MCOutput run_circle(float dt, float duration, float speed, float yawRate, float slip, int drifting,
                           MCInput* lastIn) {
    return run_circle_p(NULL, dt, duration, speed, yawRate, slip, drifting, lastIn);
}

/* Pulo: sobe a 6 m/s e cai a -6 m/s. Grava a altura do olho acima do carro
   nos 36 ticks (0.6 s) depois de aterrissar e a altura normal antes do pulo. */
static void run_jump(const MCParams* p, float* trace, float* before) {
    MCState s;
    MCOutput o;
    float dt = 1.0f / 60.0f;
    int i;
    int n = 0;
    s.initialized = 0;
    *before = 0.0f;
    for (i = 1; i <= 240; ++i) {
        float t = i * dt, ta = t - 1.0f;
        MCInput in = base_input();
        in.pos.z = 20.0f * t;
        in.vel.z = 20.0f;
        if (ta > 0.0f && ta < 0.8f) {
            in.pos.y = 6.0f * ta - 7.5f * ta * ta;
            in.vel.y = 6.0f - 15.0f * ta;
            in.grounded = 0;
        }
        ModernCam_Update(&s, p, &in, dt, NULL, NULL, &o);
        CHECK(finite_out(&o), "nao finito no pulo");
        if (i == 59)
            *before = o.eye.y - in.pos.y;
        if (ta >= 0.8f && n < 36)
            trace[n++] = o.eye.y - in.pos.y;
    }
}

/* Reta a 20 m/s com turbo a partir de 2 s. Devolve o braço horizontal antes e o máximo depois. */
static void run_boost(const MCParams* p, float* armBefore, float* armMax) {
    MCState s;
    MCOutput o;
    float dt = 1.0f / 60.0f;
    int i;
    s.initialized = 0;
    *armMax = 0.0f;
    for (i = 1; i <= 240; ++i) {
        float t = i * dt;
        MCInput in = base_input();
        in.pos.z = 20.0f * t;
        in.vel.z = 20.0f;
        in.boost = (t >= 2.0f && t < 3.5f) ? 1.0f : 0.0f;
        ModernCam_Update(&s, p, &in, dt, NULL, NULL, &o);
        if (i == 119)
            *armBefore = hdist(o.eye, in.pos);
        if (t >= 2.0f && hdist(o.eye, in.pos) > *armMax)
            *armMax = hdist(o.eye, in.pos);
    }
}

/* Reta a 15 m/s; aos 2 s o volante vai todo para yaw positivo por 0.25 s,
   mas o carro ainda não girou. Devolve o quanto a câmera já girou (rad). */
static float run_steer(const MCParams* p) {
    MCState s;
    MCOutput o;
    float dt = 1.0f / 60.0f, y0 = 0.0f;
    int i;
    s.initialized = 0;
    for (i = 1; i <= 135; ++i) {
        float t = i * dt;
        MCInput in = base_input();
        in.pos.z = 15.0f * t;
        in.vel.z = 15.0f;
        in.steer = t >= 2.0f ? 1.0f : 0.0f;
        ModernCam_Update(&s, p, &in, dt, NULL, NULL, &o);
        if (i == 119)
            y0 = o.yaw;
    }
    return o.yaw - y0;
}

/* Parede de mentira: bate na metade do braço enquanto ativa. */
static int g_wall = 0;
static int wall_raycast(void* user, MCVec3 from, MCVec3 to, float radius, float* hitFraction) {
    (void)user;
    (void)from;
    (void)to;
    (void)radius;
    if (g_wall) {
        *hitFraction = 0.5f;
        return 1;
    }
    return 0;
}

int main(void) {
    MCParams p;
    MCState s;
    MCOutput o;
    MCInput in;
    int i;

    ModernCam_DefaultParams(&p);

    /* 1. Reta acelerando: braço e FOV crescem com a velocidade */
    printf("[1] reta acelerando 0 -> 25 m/s\n");
    {
        float dt = 1.0f / 60.0f, d0 = 0.0f, fov0 = 0.0f;
        s.initialized = 0;
        for (i = 1; i <= 360; ++i) {
            float t = i * dt;
            float v = t < 4.0f ? 6.25f * t : 25.0f;
            in = base_input();
            in.pos.z = t < 4.0f ? 3.125f * t * t : 50.0f + 25.0f * (t - 4.0f);
            in.vel.z = v;
            ModernCam_Update(&s, &p, &in, dt, NULL, NULL, &o);
            CHECK(finite_out(&o), "nao finito");
            if (i == 30) {
                d0 = hdist(o.eye, in.pos);
                fov0 = o.fovDeg;
            }
        }
        printf("  distancia horizontal: %.2f m (lento) -> %.2f m (rapido)\n", d0, hdist(o.eye, in.pos));
        printf("  FOV: %.1f -> %.1f graus\n", fov0, o.fovDeg);
        CHECK(o.eye.z < in.pos.z, "camera deveria estar atras do carro");
        CHECK(hdist(o.eye, in.pos) > d0 + 0.8f, "braco deveria crescer com a velocidade");
        CHECK(o.fovDeg > fov0 + 6.0f, "FOV deveria abrir com a velocidade");
    }

    /* 2. Curva constante: a mola acompanha o giro (yawKeepUp); sem a
       compensação ela fica para trás o tempo todo (o 'swing' antigo) */
    printf("[2] curva constante 15 m/s, 0.75 rad/s\n");
    {
        MCInput last;
        MCParams lagging;
        float lag, oldLag;
        o = run_circle(1.0f / 60.0f, 6.0f, 15.0f, 0.75f, 0.0f, 0, &last);
        lag = wrapf(last.yaw - o.yaw);
        ModernCam_DefaultParams(&lagging);
        lagging.yawKeepUp = 0.0f;
        o = run_circle_p(&lagging, 1.0f / 60.0f, 6.0f, 15.0f, 0.75f, 0.0f, 0, &last);
        oldLag = wrapf(last.yaw - o.yaw);
        printf("  camera atrasada %.1f graus em relacao ao carro (%.1f com yawKeepUp = 0)\n", lag * 57.2958f,
               oldLag * 57.2958f);
        CHECK(fabsf(lag) < 0.1f, "numa curva constante a camera deveria acompanhar o giro (%.3f)", lag);
        CHECK(oldLag > 0.1f && oldLag < 0.5f, "sem compensacao o atraso deveria voltar (%.3f)", oldLag);
    }

    /* 3. Independência de FPS */
    printf("[3] mesma curva a 20, 30, 60 e 144 fps\n");
    {
        const float rates[4] = {20.0f, 30.0f, 60.0f, 144.0f};
        MCOutput ref, cur;
        MCInput last;
        float worst = 0.0f;
        int k;
        ref = run_circle(1.0f / 144.0f, 6.0f, 15.0f, 0.75f, 0.0f, 0, &last);
        for (k = 0; k < 4; ++k) {
            float dx, dy, dz, d;
            cur = run_circle(1.0f / rates[k], 6.0f, 15.0f, 0.75f, 0.0f, 0, &last);
            dx = cur.eye.x - ref.eye.x;
            dy = cur.eye.y - ref.eye.y;
            dz = cur.eye.z - ref.eye.z;
            d = sqrtf(dx * dx + dy * dy + dz * dz);
            printf("  %3.0f fps: diferenca da posicao final = %.1f cm\n", rates[k], d * 100.0f);
            if (d > worst)
                worst = d;
        }
        CHECK(worst < 0.15f, "comportamento depende demais do FPS (%.3f m)", worst);
    }

    /* 4. Drift: câmera fica entre o nariz e a direção do movimento */
    printf("[4] drift com 25 graus de escorregamento\n");
    {
        MCInput last;
        float velYaw, toHeading, toVel;
        o = run_circle(1.0f / 60.0f, 6.0f, 15.0f, 0.9f, 0.436f, 1, &last);
        velYaw = 0.9f * 6.0f;
        toHeading = wrapf(last.yaw - o.yaw);
        toVel = wrapf(velYaw - o.yaw);
        printf("  nariz %.1f graus a frente da camera, movimento %.1f graus\n", toHeading * 57.2958f, toVel * 57.2958f);
        CHECK(toHeading > 0.15f, "no drift a camera deveria mostrar a lateral do carro");
    }

    /* 5. Rodada: carro gira 2 voltas, mas segue reto a 15 m/s */
    printf("[5] rodada (2 voltas em 1.5 s) andando reto\n");
    {
        float dt = 1.0f / 60.0f, worst = 0.0f;
        s.initialized = 0;
        for (i = 1; i <= 240; ++i) {
            float t = i * dt;
            in = base_input();
            in.pos.z = 15.0f * t;
            in.vel.z = 15.0f;
            in.yaw = (t > 1.0f && t < 2.5f) ? (t - 1.0f) * (12.566f / 1.5f) : 0.0f;
            ModernCam_Update(&s, &p, &in, dt, NULL, NULL, &o);
            CHECK(finite_out(&o), "nao finito");
            if (fabsf(wrapf(o.yaw)) > worst)
                worst = fabsf(wrapf(o.yaw));
        }
        printf("  desvio maximo da camera: %.1f graus (o carro girou 720)\n", worst * 57.2958f);
        CHECK(worst < 0.5f, "camera nao deveria rodar junto com o carro");
    }

    /* 6. Ré lenta: câmera continua atrás do nariz, sem virar */
    printf("[6] re lenta a 3 m/s\n");
    {
        float dt = 1.0f / 60.0f;
        s.initialized = 0;
        for (i = 1; i <= 180; ++i) {
            in = base_input();
            in.pos.z = -3.0f * i * dt;
            in.vel.z = -3.0f;
            ModernCam_Update(&s, &p, &in, dt, NULL, NULL, &o);
        }
        printf("  yaw da camera: %.1f graus\n", wrapf(o.yaw) * 57.2958f);
        CHECK(fabsf(wrapf(o.yaw)) < 0.1f, "em re a camera deveria continuar olhando para a frente do carro");
    }

    /* 7. Pulo e aterrissagem: vertical suave + tremor */
    printf("[7] pulo (sobe 6 m/s, cai a -6 m/s)\n");
    {
        float dt = 1.0f / 60.0f, maxRel = -1e9f, minRel = 1e9f, baseRel = 0.0f;
        int landed = 0;
        s.initialized = 0;
        for (i = 1; i <= 240; ++i) {
            float t = i * dt, ta = t - 1.0f;
            in = base_input();
            in.pos.z = 20.0f * t;
            in.vel.z = 20.0f;
            if (ta > 0.0f && ta < 0.8f) {
                in.pos.y = 6.0f * ta - 7.5f * ta * ta;
                in.vel.y = 6.0f - 15.0f * ta;
                in.grounded = 0;
            }
            ModernCam_Update(&s, &p, &in, dt, NULL, NULL, &o);
            if (i == 59)
                baseRel = o.eye.y - in.pos.y;
            if (in.grounded == 0 && o.eye.y - in.pos.y > maxRel)
                maxRel = o.eye.y - in.pos.y;
            if (in.grounded == 0 && o.eye.y - in.pos.y < minRel)
                minRel = o.eye.y - in.pos.y;
            if (ta >= 0.8f && !landed) {
                landed = 1;
                printf("  trauma ao aterrissar: %.2f\n", s.trauma);
                CHECK(s.trauma > 0.25f, "aterrissagem deveria gerar tremor");
            }
        }
        printf("  altura da camera acima do carro: %.2f m no chao; no ar varia de %.2f a %.2f m (amortecida)\n",
               baseRel, minRel, maxRel);
        CHECK(s.trauma < 0.05f, "tremor deveria sumir depois de ~1 s");
    }

    /* 8. Colisão: entra na hora, sai devagar */
    printf("[8] colisao\n");
    {
        float dt = 1.0f / 60.0f, full = 0.0f, hit = 0.0f, after = 0.0f;
        s.initialized = 0;
        g_wall = 0;
        for (i = 1; i <= 300; ++i) {
            float t = i * dt;
            MCVec3 piv;
            in = base_input();
            in.pos.z = 10.0f * t;
            in.vel.z = 10.0f;
            g_wall = (t > 2.0f && t <= 3.0f);
            ModernCam_Update(&s, &p, &in, dt, wall_raycast, NULL, &o);
            piv = s.anchor;
            piv.y += p.targetHeight;
            if (i == 119)
                full = sqrtf((o.eye.x - piv.x) * (o.eye.x - piv.x) + (o.eye.y - piv.y) * (o.eye.y - piv.y) +
                             (o.eye.z - piv.z) * (o.eye.z - piv.z));
            if (i == 121)
                hit = s.collisionDist;
            if (i == 186)
                after = s.collisionDist;
        }
        printf("  braco livre %.2f m; ao bater: %.0f%%; 0.1 s depois de liberar: %.0f%%; final: %.0f%%\n", full,
               hit * 100.0f, after * 100.0f, s.collisionDist * 100.0f);
        CHECK(hit < 0.51f, "deveria encolher na hora");
        CHECK(after > 0.5f && after < 0.8f, "deveria voltar devagar");
        CHECK(s.collisionDist > 0.95f, "deveria voltar ao normal");
    }

    /* 9. Órbita manual + recentralização */
    printf("[9] orbita no analogico direito\n");
    {
        float dt = 1.0f / 60.0f, peak = 0.0f;
        s.initialized = 0;
        for (i = 1; i <= 240; ++i) {
            float t = i * dt;
            in = base_input();
            in.pos.z = 10.0f * t;
            in.vel.z = 10.0f;
            in.stickX = t < 0.5f ? 1.0f : 0.0f;
            ModernCam_Update(&s, &p, &in, dt, NULL, NULL, &o);
            if (i == 30)
                peak = s.orbitYaw;
        }
        printf("  orbita: %.1f graus -> %.1f graus apos soltar\n", peak * 57.2958f, s.orbitYaw * 57.2958f);
        CHECK(peak > 1.2f, "orbita deveria girar com o analogico");
        CHECK(fabsf(s.orbitYaw) < 0.25f, "deveria recentralizar");
    }

    /* 9b. Em velocidade constante a âncora fica em cima do carro; numa
       parada seca ela não avança de uma vez (o antigo salto da mola) */
    printf("[9b] ancora em velocidade constante e numa parada seca\n");
    {
        MCParams spring, trailing;
        MCState s1, s2;
        MCOutput o1, o2;
        float dt = 1.0f / 60.0f, lag1 = 0.0f, lag2 = 0.0f, lunge1 = 0.0f, lunge2 = 0.0f, d1 = 0.0f, d2 = 0.0f;
        ModernCam_DefaultParams(&spring);
        spring.enablePosSpring = 1;
        spring.posFreq = 2.3f;
        spring.posDamping = 0.98f;
        trailing = spring;
        trailing.posKeepUp = 0.0f;
        s1.initialized = s2.initialized = 0;
        for (i = 1; i <= 300; ++i) {
            float t = i * dt;
            in = base_input();
            in.pos.z = t < 3.0f ? 20.0f * t : 60.0f;
            in.vel.z = t < 3.0f ? 20.0f : 0.0f;
            ModernCam_Update(&s1, &spring, &in, dt, NULL, NULL, &o1);
            ModernCam_Update(&s2, &trailing, &in, dt, NULL, NULL, &o2);
            if (i == 179) {
                lag1 = in.pos.z - s1.anchor.z;
                lag2 = in.pos.z - s2.anchor.z;
                d1 = s1.anchor.z;
                d2 = s2.anchor.z;
            }
            /* depois da parada: a ancora compensada passa um pouco do carro
               e volta; a antiga avanca o atraso inteiro de uma vez */
            if (t >= 3.0f) {
                if (s1.anchor.z - in.pos.z > lunge1)
                    lunge1 = s1.anchor.z - in.pos.z;
                if (s2.anchor.z - d2 > lunge2)
                    lunge2 = s2.anchor.z - d2;
            }
        }
        (void)d1;
        printf("  a 20 m/s a ancora fica %.2f m atras do carro (%.2f m com posKeepUp = 0)\n", lag1, lag2);
        printf("  parada seca: a ancora passa %.2f m do carro (com posKeepUp = 0 ela avanca %.2f m)\n", lunge1, lunge2);
        CHECK(fabsf(lag1) < 0.1f, "a ancora deveria ficar em cima do carro");
        CHECK(lag2 > 2.0f, "sem compensacao a ancora deveria ficar para tras");
        CHECK(lunge1 < 0.7f, "a parada nao deveria fazer a ancora passar tanto do carro (%.2f m)", lunge1);
        CHECK(lunge2 > 2.0f, "sem compensacao a ancora deveria avancar o atraso todo");
    }

    /* 10. Teleporte */
    printf("[10] teleporte de 100 m\n");
    {
        in = base_input();
        in.pos.x = 100.0f;
        ModernCam_Update(&s, &p, &in, 1.0f / 60.0f, NULL, NULL, &o);
        printf("  distancia da camera ao carro: %.2f m\n", hdist(o.eye, in.pos));
        CHECK(hdist(o.eye, in.pos) < 6.0f, "deveria resetar no teleporte");
    }

    /* ---------------- Melhorias, com os valores da bancada ---------------- */
    {
        MCParams on, off;
        bancada_params(&on);
        off = on;
        off.enableSteerLead = off.enablePosSpring = off.enableLandingDip = off.enableBoostKick = 0;
        off.rotationalShake = 0;

        /* 11. FPS com tudo ligado */
        printf("[11] valores da bancada: mesma curva a 20, 30, 60 e 144 fps\n");
        {
            const float rates[3] = {20.0f, 30.0f, 60.0f};
            MCInput last;
            MCOutput ref = run_circle_p(&on, 1.0f / 144.0f, 6.0f, 15.0f, 0.75f, 0.0f, 0, &last), cur;
            float worst = 0.0f;
            int k;
            for (k = 0; k < 3; ++k) {
                float dx, dy, dz;
                cur = run_circle_p(&on, 1.0f / rates[k], 6.0f, 15.0f, 0.75f, 0.0f, 0, &last);
                dx = cur.eye.x - ref.eye.x;
                dy = cur.eye.y - ref.eye.y;
                dz = cur.eye.z - ref.eye.z;
                if (sqrtf(dx * dx + dy * dy + dz * dz) > worst)
                    worst = sqrtf(dx * dx + dy * dy + dz * dz);
            }
            printf("  maior diferenca: %.1f cm\n", worst * 100.0f);
            CHECK(worst < 0.15f, "melhorias dependem do FPS (%.3f m)", worst);
        }

        /* 12. Antecipação pelo volante */
        printf("[12] antecipacao pelo volante (0.25 s de volante antes do carro girar)\n");
        {
            float a = run_steer(&on), b = run_steer(&off);
            printf("  camera ja girou %.1f graus (desligada: %.1f)\n", a * 57.2958f, b * 57.2958f);
            CHECK(a > 0.03f, "deveria girar antes do carro");
            CHECK(fabsf(b) < 0.005f, "desligada nao deveria girar");
        }

        /* 13. Afundada na aterrissagem */
        printf("[13] afundada na aterrissagem\n");
        {
            float tOn[36], tOff[36], baseOn, baseOff, dip = 0.0f;
            int k, at = 0;
            run_jump(&on, tOn, &baseOn);
            run_jump(&off, tOff, &baseOff);
            for (k = 0; k < 36; ++k)
                if (tOff[k] - tOn[k] > dip) {
                    dip = tOff[k] - tOn[k];
                    at = k;
                }
            printf("  afundou %.2f m a mais que sem a melhoria, %.0f ms depois de tocar o chao\n", dip,
                   (at + 1) * 1000.0f / 60.0f);
            CHECK(dip > 0.3f, "a camera deveria afundar ao aterrissar");
            CHECK(fabsf(tOff[35] - tOn[35]) < 0.1f, "deveria voltar ao normal em 0.6 s");
        }

        /* 14. Puxão no turbo */
        printf("[14] puxao no inicio do turbo\n");
        {
            float b0, bMax, o0, oMax;
            run_boost(&on, &b0, &bMax);
            run_boost(&off, &o0, &oMax);
            printf("  braco: %.2f m -> pico %.2f m (desligado: %.2f -> %.2f m)\n", b0, bMax, o0, oMax);
            CHECK(bMax - b0 > 0.8f, "o braco deveria esticar no turbo");
            CHECK(oMax - o0 < 0.1f, "desligado nao deveria esticar");
        }

        /* 15. Tremor rotacional não desloca o olho */
        printf("[15] tremor so rotacional\n");
        {
            MCState s1, s2;
            MCOutput o1, o2;
            float worstEye = 0.0f, worstAim = 0.0f, dt = 1.0f / 60.0f;
            s1.initialized = s2.initialized = 0;
            for (i = 1; i <= 120; ++i) {
                float dx, dy, dz;
                in = base_input();
                in.pos.z = 10.0f * i * dt;
                in.vel.z = 10.0f;
                if (i == 30)
                    ModernCam_AddTrauma(&s2, 1.0f);
                ModernCam_Update(&s1, &on, &in, dt, NULL, NULL, &o1);
                ModernCam_Update(&s2, &on, &in, dt, NULL, NULL, &o2);
                dx = o1.eye.x - o2.eye.x;
                dy = o1.eye.y - o2.eye.y;
                dz = o1.eye.z - o2.eye.z;
                if (sqrtf(dx * dx + dy * dy + dz * dz) > worstEye)
                    worstEye = sqrtf(dx * dx + dy * dy + dz * dz);
                if (fabsf(wrapf(o1.yaw - o2.yaw)) > worstAim)
                    worstAim = fabsf(wrapf(o1.yaw - o2.yaw));
            }
            printf("  olho deslocado %.4f m; mira tremeu ate %.2f graus\n", worstEye, worstAim * 57.2958f);
            CHECK(worstEye < 1e-4f, "tremor rotacional nao deveria mover o olho");
            CHECK(worstAim > 0.005f, "deveria tremer a mira");
        }
    }

    /* Exemplo de conversão para ângulos binários do N64 */
    printf("[N64] yaw %.3f rad = 0x%04X, pitch %.3f rad = 0x%04X\n", o.yaw, (unsigned short)ModernCam_ToBinAngle(o.yaw),
           o.pitch, (unsigned short)ModernCam_ToBinAngle(o.pitch));

    printf(g_fail ? "\nALGUM TESTE FALHOU\n" : "\nTodos os testes passaram\n");
    return g_fail;
}
