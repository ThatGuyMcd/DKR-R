/*
 * modern_cam.c - implementação. Veja modern_cam.h para a explicação.
 */
#include "modern_cam.h"

#define MC_PI 3.14159265358979f
#define MC_HALF_PI 1.57079632679490f
#define MC_TWO_PI 6.28318530717959f

/* ------------------------------------------------------------------------- */
/* Backend matemático                                                         */
/* ------------------------------------------------------------------------- */

static float mc_floor(float x) {
    int i = (int)x;
    return (float)(i - (x < (float)i));
}

#ifndef MC_NO_LIBM
#include <math.h>
#define MC_SIN(x) sinf(x)
#define MC_COS(x) cosf(x)
#define MC_ATAN2(y, x) atan2f((y), (x))
#define MC_SQRT(x) sqrtf(x)
#define MC_EXP(x) expf(x)
#else
/* Aproximações sem libm. Erro < 1e-5, sobra para câmera. */
typedef union {
    float f;
    unsigned int i;
} mc_bits;

static float mc_sqrt_impl(float x) {
    mc_bits u;
    float y;
    if (x <= 0.0f) {
        return 0.0f;
    }
    u.f = x;
    u.i = 0x1fbd1df5u + (u.i >> 1);
    y = u.f;
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    return y;
}

static float mc_exp_impl(float x) {
    float k, r, p;
    int n;
    mc_bits u;
    if (x < -80.0f) {
        return 0.0f;
    }
    if (x > 80.0f) {
        x = 80.0f;
    }
    k = x * 1.44269504f;
    n = (int)(k + (k >= 0.0f ? 0.5f : -0.5f));
    r = x - (float)n * 0.69314718f;
    p = 1.0f + r * (1.0f + r * (0.5f + r * (0.16666667f + r * (0.041666667f + r * 0.0083333333f))));
    u.i = (unsigned int)(n + 127) << 23;
    return p * u.f;
}

static float mc_sin_impl(float x) {
    float x2;
    x = x - MC_TWO_PI * mc_floor((x + MC_PI) / MC_TWO_PI);
    if (x > MC_HALF_PI) {
        x = MC_PI - x;
    } else if (x < -MC_HALF_PI) {
        x = -MC_PI - x;
    }
    x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.0083333333f + x2 * (-0.00019841270f + x2 * 0.0000027557319f))));
}

static float mc_cos_impl(float x) {
    return mc_sin_impl(x + MC_HALF_PI);
}

static float mc_atan2_impl(float y, float x) {
    float ax = x < 0.0f ? -x : x;
    float ay = y < 0.0f ? -y : y;
    float a, s, r;
    if (ax == 0.0f && ay == 0.0f) {
        return 0.0f;
    }
    a = (ax > ay) ? ay / ax : ax / ay;
    s = a * a;
    r = a * (0.99997726f +
             s * (-0.33262347f + s * (0.19354346f + s * (-0.11643287f + s * (0.05265332f + s * -0.01172120f)))));
    if (ay > ax) {
        r = MC_HALF_PI - r;
    }
    if (x < 0.0f) {
        r = MC_PI - r;
    }
    if (y < 0.0f) {
        r = -r;
    }
    return r;
}

#define MC_SIN(x) mc_sin_impl(x)
#define MC_COS(x) mc_cos_impl(x)
#define MC_ATAN2(y, x) mc_atan2_impl((y), (x))
#define MC_SQRT(x) mc_sqrt_impl(x)
#define MC_EXP(x) mc_exp_impl(x)
#endif

/* ------------------------------------------------------------------------- */
/* Utilitários                                                                */
/* ------------------------------------------------------------------------- */

static float mc_abs(float x) {
    return x < 0.0f ? -x : x;
}

static float mc_min(float a, float b) {
    return a < b ? a : b;
}

static float mc_max(float a, float b) {
    return a > b ? a : b;
}

static float mc_clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static float mc_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static float mc_smoothstep(float e0, float e1, float x) {
    float t = mc_clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* Ângulo em (-pi, pi]. */
static float mc_wrap(float a) {
    return a - MC_TWO_PI * mc_floor((a + MC_PI) / MC_TWO_PI);
}

/*
 * Fator de suavização exponencial independente de FPS:
 * value += (target - value) * mc_follow(sharpness, dt)
 * sharpness = quantas "constantes de tempo" por segundo.
 */
static float mc_follow(float sharpness, float dt) {
    return 1.0f - MC_EXP(-sharpness * dt);
}

/*
 * Mesma suavização, mas para um alvo que se moveu de prevTarget até target
 * durante o dt (solução exata da EDO para alvo em movimento linear).
 * Sem isso, a 20-30 fps o atraso fica diferente do de 60+ fps.
 */
static float mc_follow_moving(float x, float prevTarget, float target, float sharpness, float dt) {
    float b, lag;
    if (dt <= 0.0f) {
        return x;
    }
    if (sharpness * dt < 1e-4f) {
        return x + (target - x) * mc_follow(sharpness, dt);
    }
    b = (target - prevTarget) / dt;
    lag = b / sharpness;
    return target - lag + (x - prevTarget + lag) * MC_EXP(-sharpness * dt);
}

static MCVec3 mc_v3(float x, float y, float z) {
    MCVec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static MCVec3 mc_add(MCVec3 a, MCVec3 b) {
    return mc_v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static MCVec3 mc_sub(MCVec3 a, MCVec3 b) {
    return mc_v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static MCVec3 mc_scale(MCVec3 a, float s) {
    return mc_v3(a.x * s, a.y * s, a.z * s);
}

static float mc_dot(MCVec3 a, MCVec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static MCVec3 mc_cross(MCVec3 a, MCVec3 b) {
    return mc_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static float mc_len(MCVec3 a) {
    return MC_SQRT(mc_dot(a, a));
}

static MCVec3 mc_norm_or(MCVec3 a, MCVec3 fallback) {
    float l = mc_len(a);
    if (l < 1e-6f) {
        return fallback;
    }
    return mc_scale(a, 1.0f / l);
}

/* Ruído suave 1D em [-1, 1] para o tremor. */
static float mc_hash(unsigned int n) {
    n = (n << 13) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return 1.0f - (float)(n & 0x7fffffffu) / 1073741824.0f;
}

static float mc_noise(unsigned int seed, float t) {
    float f = mc_floor(t);
    float u = t - f;
    unsigned int i = (unsigned int)(int)f;
    u = u * u * (3.0f - 2.0f * u);
    return mc_lerp(mc_hash(i * 7919u + seed * 104729u), mc_hash((i + 1u) * 7919u + seed * 104729u), u);
}

/* Três camadas de ruído: tremor mais orgânico. */
static float mc_fbm(unsigned int seed, float t) {
    return mc_noise(seed, t) * 0.57f + mc_noise(seed + 17u, t * 2.13f) * 0.29f +
           mc_noise(seed + 31u, t * 4.37f) * 0.14f;
}

/* Mola amortecida (massa-mola) integrada em sub-passos de até 1/240 s. */
static void mc_spring(float* x, float* v, float target, float freq, float damp, float dt) {
    float om = MC_TWO_PI * freq;
    int steps = (int)(dt * 240.0f) + 1;
    float h = dt / (float)steps;
    int i;
    for (i = 0; i < steps; ++i) {
        float a = om * om * (target - *x) - 2.0f * damp * om * *v;
        *v += a * h;
        *x += *v * h;
    }
}

/* ------------------------------------------------------------------------- */
/* API                                                                        */
/* ------------------------------------------------------------------------- */

void ModernCam_DefaultParams(MCParams* p) {
    p->unitScale = 1.0f;

    p->distMin = 3.6f;
    p->distMax = 4.8f;
    p->heightMin = 1.25f;
    p->heightMax = 1.45f;
    p->targetHeight = 0.7f;
    p->speedForMax = 25.0f;
    p->zoomSharpness = 1.5f;

    p->minAlignSpeed = 1.0f;
    p->velocityAlign = 0.35f;
    p->driftAlign = 0.6f;
    p->driftLead = 0.0f;
    p->spinFollowSpeed = 8.0f;
    p->alignSharpness = 4.0f;

    p->yawFreq = 1.1f;
    p->yawFreqSpeedBoost = 0.4f;
    p->yawDamping = 0.85f;
    p->yawKeepUp = 1.0f;

    p->posSharpness = 18.0f;
    p->posKeepUp = 1.0f;
    p->heightSharpness = 7.0f;
    p->airHeightSharpness = 2.5f;
    p->maxVerticalLag = 2.0f;

    p->slopeFollow = 0.6f;
    p->slopeSharpness = 4.0f;

    p->lookAhead = 0.18f;
    p->lookAheadMax = 3.0f;
    p->lookAheadSharpness = 5.0f;

    p->fovBase = 60.0f;
    p->fovSpeed = 10.0f;
    p->fovSharpness = 3.0f;
    p->fovBoost = 8.0f;
    p->boostFovAttack = 10.0f;
    p->boostFovRelease = 2.0f;

    p->rollFromYawRate = 0.02f;
    p->rollMax = 0.05f;
    p->rollSharpness = 4.0f;

    p->orbitSpeedYaw = 3.0f;
    p->orbitSpeedPitch = 1.5f;
    p->recenterDelay = 1.2f;
    p->recenterSharpness = 3.0f;

    p->collisionRadius = 0.3f;
    p->collisionReleaseSharpness = 4.0f;

    p->traumaDecay = 1.4f;
    p->boostTrauma = 0.15f;
    p->landingTrauma = 0.6f;
    p->landingSpeedForMax = 10.0f;
    p->shakeFreq = 18.0f;
    p->shakeMaxAngle = 0.03f;
    p->shakeMaxOffset = 0.08f;

    p->teleportDistance = 30.0f;

    /* melhorias: desligadas, com valores de partida para quando ligar */
    p->enableSteerLead = 0;
    p->steerLead = 0.14f;
    p->enablePosSpring = 0;
    p->posFreq = 3.0f;
    p->posDamping = 0.7f;
    p->enableLandingDip = 0;
    p->landingDip = 0.55f;
    p->dipFreq = 3.2f;
    p->dipDamping = 0.42f;
    p->enableBoostKick = 0;
    p->boostKick = 1.1f;
    p->kickFreq = 1.8f;
    p->kickDamping = 0.5f;
    p->rotationalShake = 0;
}

void ModernCam_Reset(MCState* s, const MCParams* p, const MCInput* in) {
    float flatSpeed = MC_SQRT(in->vel.x * in->vel.x + in->vel.z * in->vel.z);
    float speed01 = mc_clamp(flatSpeed / (p->speedForMax * p->unitScale), 0.0f, 1.0f);

    s->initialized = 1;
    s->yaw = mc_wrap(in->yaw);
    s->yawVel = 0.0f;
    s->anchor = in->pos;
    s->lookAheadVec = mc_v3(0.0f, 0.0f, 0.0f);
    s->speed01 = speed01;
    s->slope = in->grounded ? in->slopePitch * p->slopeFollow : 0.0f;
    s->fov = p->fovBase + p->fovSpeed * speed01;
    s->boostFov = 0.0f;
    s->roll = 0.0f;
    s->orbitYaw = 0.0f;
    s->orbitPitch = 0.0f;
    s->idleTime = 0.0f;
    s->collisionDist = 1.0f;
    s->trauma = 0.0f;
    s->time = 0.0f;
    s->prevVehicleYaw = s->yaw;
    s->prevVelY = in->vel.y;
    s->prevGrounded = in->grounded;
    s->prevPos = in->pos;
    s->prevLookAheadTarget = mc_v3(0.0f, 0.0f, 0.0f);
    s->prevSpeed01Raw = speed01;
    s->prevTargetYaw = s->yaw;
    s->slipSm = 0.0f;
    s->turnRate = 0.0f;
    s->prevVelYaw = s->yaw;
    s->prevVel = mc_v3(in->vel.x, 0.0f, in->vel.z);
    s->anchorVel = mc_v3(in->vel.x, 0.0f, in->vel.z);
    s->steerSm = 0.0f;
    s->dipY = 0.0f;
    s->dipV = 0.0f;
    s->kick = 0.0f;
    s->kickV = 0.0f;
    s->prevBoost = in->boost;
}

void ModernCam_AddTrauma(MCState* s, float amount) {
    s->trauma = mc_clamp(s->trauma + amount, 0.0f, 1.0f);
}

void ModernCam_Update(MCState* s, const MCParams* p, const MCInput* in, float dt, MCRaycastFn raycast, void* user,
                      MCOutput* out) {
    const MCVec3 worldUp = mc_v3(0.0f, 1.0f, 0.0f);
    const float sc = p->unitScale;
    MCVec3 flatVel, pivot, eyeDesired, eye, target, boomDir;
    MCVec3 forward, right, camUp;
    float flatSpeed, speed01, heading, targetYaw, minSpeed;
    float headingRate, offset, spinW, velYaw, omega;
    float dist, height, boomLen, elev, camYaw, viewDist;
    float shake, roll, impact;
    int landed, boostEdge;

    if (!s->initialized) {
        ModernCam_Reset(s, p, in);
    }
    if (mc_len(mc_sub(in->pos, s->anchor)) > p->teleportDistance * sc) {
        ModernCam_Reset(s, p, in);
    }
    dt = mc_clamp(dt, 0.0f, 0.1f);

    s->time += dt;
    if (s->time > 1000.0f) {
        s->time -= 1000.0f;
    }

    /* 1. Velocidade e zoom ---------------------------------------------- */
    flatVel = mc_v3(in->vel.x, 0.0f, in->vel.z);
    flatSpeed = mc_len(flatVel);
    speed01 = mc_clamp(flatSpeed / (p->speedForMax * sc), 0.0f, 1.0f);
    s->speed01 = mc_follow_moving(s->speed01, s->prevSpeed01Raw, speed01, p->zoomSharpness, dt);

    /* 2. Yaw alvo: mistura nariz e direção da velocidade ----------------- */
    heading = mc_wrap(in->yaw);
    headingRate = dt > 0.0f ? mc_wrap(heading - s->prevVehicleYaw) / dt : 0.0f;
    minSpeed = p->minAlignSpeed * sc;
    s->steerSm += (mc_clamp(in->steer, -1.0f, 1.0f) - s->steerSm) * mc_follow(8.0f, dt);
    /* offset: a parcela suave do alvo em relação ao nariz (alinhamento com a
       velocidade e antecipações); spinW: quanto o alvo é a própria direção da
       velocidade, numa rodada */
    offset = 0.0f;
    spinW = 0.0f;
    velYaw = heading;
    if (flatSpeed > minSpeed) {
        float slip, absSlip, align, fast;
        velYaw = MC_ATAN2(flatVel.x, flatVel.z);
        slip = mc_wrap(velYaw - heading);
        absSlip = mc_abs(slip);
        align = in->drifting ? p->driftAlign : p->velocityAlign;
        /* em rodada rápida, segue a velocidade; em ré lenta, segue o nariz */
        fast = mc_smoothstep(p->spinFollowSpeed * sc * 0.5f, p->spinFollowSpeed * sc, flatSpeed);
        spinW = mc_smoothstep(0.785f, MC_HALF_PI, absSlip) * fast;

        align *= mc_smoothstep(minSpeed, minSpeed * 3.0f, flatSpeed);
        align *= 1.0f - mc_smoothstep(1.05f, 1.75f, absSlip);
        /* o escorregamento entra filtrado: começar ou terminar um drift gira a
           câmera ao longo de alignSharpness, não de uma vez */
        s->slipSm = mc_wrap(s->slipSm + mc_wrap(slip - s->slipSm) * mc_follow(p->alignSharpness, dt));
        offset = s->slipSm * align;

        if (in->drifting && p->driftLead != 0.0f && absSlip < MC_HALF_PI) {
            float lead = p->driftLead * mc_smoothstep(0.02f, 0.15f, absSlip);
            offset -= (slip > 0.0f ? lead : -lead);
        }
        /* antecipação: o volante empurra a câmera antes do carro girar */
        if (p->enableSteerLead && absSlip < MC_HALF_PI) {
            offset += s->steerSm * p->steerLead * mc_smoothstep(minSpeed, minSpeed * 3.0f, flatSpeed);
        }
    } else {
        s->slipSm -= s->slipSm * mc_follow(p->alignSharpness, dt);
    }

    /* 3. Mola angular do yaw (sub-passos -> estável e igual em qualquer FPS) */
    omega = MC_TWO_PI * (p->yawFreq + p->yawFreqSpeedBoost * s->speed01);
    {
        /* Uma mola amortecida atrasa 2*zeta*taxa/omega atrás de um alvo que
           gira a taxa constante. Esse atraso entra no alvo, filtrado, para a
           câmera acompanhar uma curva constante sem ficar para trás; o que
           sobra é a resposta da mola às mudanças de giro. Numa rodada o alvo
           é a direção da velocidade, que não gira, então a compensação some
           com spinW; e ela é limitada para um nariz batido não dar um tranco. */
        /* só o giro do nariz: o alinhamento com a velocidade chega pelo seu
           filtro e pela mola, no seu tempo */
        float rate = headingRate * (1.0f - spinW);
        float velYawRate = dt > 0.0f ? mc_wrap(velYaw - s->prevVelYaw) / dt : 0.0f;
        float comp;
        /* numa curva o nariz e a velocidade giram juntos; numa rodada (ou num
           nariz batido) só o nariz gira. Acima de ~2 rad/s de diferença não é
           curva, e a compensação some no mesmo tick, antes de spinW */
        rate *= 1.0f - mc_smoothstep(2.0f, 4.0f, mc_abs(headingRate - velYawRate));
        s->prevVelYaw = velYaw;
        s->turnRate += (rate - s->turnRate) * mc_follow(12.0f, dt);
        comp = 2.0f * p->yawDamping / omega * s->turnRate * mc_clamp(p->yawKeepUp, 0.0f, 1.0f);
        comp = mc_clamp(comp, -0.35f, 0.35f);
        targetYaw = mc_wrap(heading + mc_lerp(offset, mc_wrap(velYaw - heading), spinW) + comp);
    }
    {
        float zeta = p->yawDamping;
        int steps = (int)(dt * 240.0f) + 1;
        float h = dt / (float)steps;
        float delta = mc_wrap(targetYaw - s->prevTargetYaw);
        int i;
        for (i = 0; i < steps; ++i) {
            /* o alvo anda ao longo do dt, não salta no fim dele */
            float tgt = s->prevTargetYaw + delta * ((float)(i + 1) / (float)steps);
            float err = mc_wrap(tgt - s->yaw);
            float acc = omega * omega * err - 2.0f * zeta * omega * s->yawVel;
            s->yawVel += acc * h;
            s->yaw = mc_wrap(s->yaw + s->yawVel * h);
        }
    }

    /* 4. Órbita manual com recentralização ------------------------------- */
    {
        const float dz = 0.15f;
        float sx = mc_abs(in->stickX) > dz ? in->stickX : 0.0f;
        float sy = mc_abs(in->stickY) > dz ? in->stickY : 0.0f;
        if (sx != 0.0f || sy != 0.0f) {
            s->orbitYaw = mc_wrap(s->orbitYaw + sx * p->orbitSpeedYaw * dt);
            s->orbitPitch = mc_clamp(s->orbitPitch + sy * p->orbitSpeedPitch * dt, -0.5f, 0.8f);
            s->idleTime = 0.0f;
        } else {
            s->idleTime += dt;
            /* parado, a câmera fica onde você deixou; andando, volta para trás do carro */
            if (s->idleTime > p->recenterDelay && flatSpeed > minSpeed) {
                float k = mc_follow(p->recenterSharpness, dt);
                s->orbitYaw -= s->orbitYaw * k;
                s->orbitPitch -= s->orbitPitch * k;
            }
        }
    }

    /* 5. Âncora: horizontal firme, vertical amortecida ------------------- */
    {
        float kV = in->grounded ? p->heightSharpness : p->airHeightSharpness;
        float maxLag = p->maxVerticalLag * sc;
        /* velocidade medida pela posição: é ela que o atraso acompanha */
        float keep = mc_clamp(p->posKeepUp, 0.0f, 1.0f);
        MCVec3 vel = dt > 0.0f ? mc_scale(mc_sub(in->pos, s->prevPos), 1.0f / dt) : s->prevVel;
        vel.y = 0.0f;
        if (p->enablePosSpring) {
            /* mola de 2ª ordem; o alvo anda ao longo do dt (mesmo resultado em qualquer FPS).
               O amortecimento age sobre a velocidade RELATIVA ao carro (posKeepUp):
               em velocidade constante a âncora fica em cima dele, e só acelerar,
               frear ou bater a desloca. */
            float om = MC_TWO_PI * p->posFreq, z = p->posDamping;
            int steps = (int)(dt * 240.0f) + 1, i;
            float h = dt / (float)steps;
            for (i = 0; i < steps; ++i) {
                float t = (float)(i + 1) / (float)steps;
                float tx = s->prevPos.x + (in->pos.x - s->prevPos.x) * t;
                float tz = s->prevPos.z + (in->pos.z - s->prevPos.z) * t;
                s->anchorVel.x += (om * om * (tx - s->anchor.x) - 2.0f * z * om * (s->anchorVel.x - keep * vel.x)) * h;
                s->anchorVel.z += (om * om * (tz - s->anchor.z) - 2.0f * z * om * (s->anchorVel.z - keep * vel.z)) * h;
                s->anchor.x += s->anchorVel.x * h;
                s->anchor.z += s->anchorVel.z * h;
            }
        } else {
            /* o alvo vai um atraso à frente do carro, então a âncora fica nele */
            float lead = keep / p->posSharpness;
            s->anchor.x = mc_follow_moving(s->anchor.x, s->prevPos.x + s->prevVel.x * lead, in->pos.x + vel.x * lead,
                                           p->posSharpness, dt);
            s->anchor.z = mc_follow_moving(s->anchor.z, s->prevPos.z + s->prevVel.z * lead, in->pos.z + vel.z * lead,
                                           p->posSharpness, dt);
            /* mantém a mola pronta caso seja ligada no meio do jogo */
            s->anchorVel.x = in->vel.x;
            s->anchorVel.z = in->vel.z;
        }
        s->prevVel = vel;
        s->anchor.y = mc_follow_moving(s->anchor.y, s->prevPos.y, in->pos.y, kV, dt);
        s->anchor.y = mc_clamp(s->anchor.y, in->pos.y - maxLag, in->pos.y + maxLag);
    }

    /* 6. Inclinação do terreno (congela no ar) --------------------------- */
    if (in->grounded) {
        s->slope += (in->slopePitch * p->slopeFollow - s->slope) * mc_follow(p->slopeSharpness, dt);
    }

    /* 7. Look-ahead -------------------------------------------------------- */
    {
        MCVec3 la = mc_scale(flatVel, p->lookAhead);
        float l = mc_len(la);
        float lmax = p->lookAheadMax * sc;
        float fade = 1.0f - mc_smoothstep(0.2f, 0.8f, mc_abs(s->orbitYaw));
        float k = p->lookAheadSharpness;
        MCVec3 prev = s->prevLookAheadTarget;
        if (l > lmax) {
            la = mc_scale(la, lmax / l);
        }
        if (in->lookBack) {
            fade = 0.0f;
        }
        la = mc_scale(la, fade);
        s->lookAheadVec.x = mc_follow_moving(s->lookAheadVec.x, prev.x, la.x, k, dt);
        s->lookAheadVec.y = mc_follow_moving(s->lookAheadVec.y, prev.y, la.y, k, dt);
        s->lookAheadVec.z = mc_follow_moving(s->lookAheadVec.z, prev.z, la.z, k, dt);
        s->prevLookAheadTarget = la;
    }

    /* Impulsos: aterrissagem e início do turbo ------------------------------ */
    landed = in->grounded && !s->prevGrounded;
    impact = landed ? mc_clamp(-s->prevVelY / (p->landingSpeedForMax * sc), 0.0f, 1.0f) : 0.0f;
    boostEdge = in->boost > 0.5f && s->prevBoost <= 0.5f;
    if (p->enableBoostKick) {
        if (boostEdge) {
            s->kickV += p->boostKick * sc * MC_TWO_PI * p->kickFreq;
        }
        mc_spring(&s->kick, &s->kickV, 0.0f, p->kickFreq, p->kickDamping, dt);
    } else {
        s->kick = 0.0f;
        s->kickV = 0.0f;
    }
    if (p->enableLandingDip) {
        if (landed) {
            s->dipV -= impact * p->landingDip * sc * MC_TWO_PI * p->dipFreq;
        }
        mc_spring(&s->dipY, &s->dipV, 0.0f, p->dipFreq, p->dipDamping, dt);
    } else {
        s->dipY = 0.0f;
        s->dipV = 0.0f;
    }

    /* 8. Braço da câmera --------------------------------------------------- */
    dist = mc_lerp(p->distMin, p->distMax, s->speed01) * sc + s->kick;
    height = mc_lerp(p->heightMin, p->heightMax, s->speed01) * sc;
    boomLen = MC_SQRT(dist * dist + height * height);
    elev = mc_clamp(MC_ATAN2(height, dist) + s->orbitPitch - s->slope, -0.3f, 1.4f);
    camYaw = s->yaw + s->orbitYaw + (in->lookBack ? MC_PI : 0.0f);

    boomDir = mc_v3(-MC_SIN(camYaw) * MC_COS(elev), MC_SIN(elev), -MC_COS(camYaw) * MC_COS(elev));
    eyeDesired = mc_add(s->anchor, mc_scale(boomDir, boomLen));
    pivot = mc_add(s->anchor, mc_scale(worldUp, p->targetHeight * sc));

    /* 9. Colisão: entra na hora, sai devagar ----------------------------- */
    {
        float allowed = 1.0f;
        if (raycast) {
            float f = 1.0f;
            if (raycast(user, pivot, eyeDesired, p->collisionRadius * sc, &f)) {
                allowed = mc_clamp(f, 0.05f, 1.0f);
            }
        }
        if (allowed < s->collisionDist) {
            s->collisionDist = allowed;
        } else {
            s->collisionDist += (allowed - s->collisionDist) * mc_follow(p->collisionReleaseSharpness, dt);
        }
    }
    eye = mc_add(pivot, mc_scale(mc_sub(eyeDesired, pivot), s->collisionDist));
    target = mc_add(pivot, s->lookAheadVec);
    /* afundada: o olho desce mais que o alvo, a câmera "comprime" */
    eye.y += s->dipY;
    target.y += s->dipY * 0.5f;

    /* 10. FOV --------------------------------------------------------------- */
    {
        float fovTarget = p->fovBase + p->fovSpeed * s->speed01;
        float boostTarget = mc_clamp(in->boost, 0.0f, 1.0f) * p->fovBoost;
        float rate = boostTarget > s->boostFov ? p->boostFovAttack : p->boostFovRelease;
        s->fov += (fovTarget - s->fov) * mc_follow(p->fovSharpness, dt);
        s->boostFov += (boostTarget - s->boostFov) * mc_follow(rate, dt);
    }

    /* 11. Trauma (boost, aterrissagem) ------------------------------------ */
    s->trauma = mc_max(0.0f, s->trauma - p->traumaDecay * dt);
    if (in->boost > 0.0f) {
        s->trauma = mc_max(s->trauma, mc_clamp(in->boost, 0.0f, 1.0f) * p->boostTrauma);
    }
    if (landed) {
        s->trauma = mc_min(1.0f, s->trauma + impact * p->landingTrauma);
    }

    /* 12. Roll leve conforme a taxa de giro ------------------------------- */
    {
        float rollTarget = mc_clamp(-headingRate * p->rollFromYawRate, -p->rollMax, p->rollMax);
        s->roll += (rollTarget - s->roll) * mc_follow(p->rollSharpness, dt);
    }

    /* 13. Base da câmera + tremor ---------------------------------------- */
    forward = mc_norm_or(mc_sub(target, eye), mc_v3(MC_SIN(camYaw), 0.0f, MC_COS(camYaw)));
    right = mc_norm_or(mc_cross(forward, worldUp), mc_v3(-MC_COS(camYaw), 0.0f, MC_SIN(camYaw)));
    camUp = mc_cross(right, forward);

    roll = s->roll;
    shake = s->trauma * s->trauma;
    if (shake > 0.0f) {
        float t = s->time * p->shakeFreq;
        viewDist = mc_len(mc_sub(target, eye));
        if (p->rotationalShake) {
            /* só gira a visão: nada de deslocar o olho (não atravessa paredes) */
            float aY = mc_fbm(1u, t) * p->shakeMaxAngle * shake;
            float aP = mc_fbm(2u, t) * p->shakeMaxAngle * shake;
            target = mc_add(target, mc_add(mc_scale(right, aY * viewDist), mc_scale(camUp, aP * viewDist)));
            roll += mc_fbm(3u, t) * p->shakeMaxAngle * shake;
        } else {
            float aY = mc_noise(1u, t) * p->shakeMaxAngle * shake;
            float aP = mc_noise(2u, t) * p->shakeMaxAngle * shake;
            float off = p->shakeMaxOffset * sc * shake;
            target = mc_add(target, mc_add(mc_scale(right, aY * viewDist), mc_scale(camUp, aP * viewDist)));
            eye = mc_add(eye, mc_add(mc_scale(right, mc_noise(4u, t) * off), mc_scale(camUp, mc_noise(5u, t) * off)));
            roll += mc_noise(3u, t) * p->shakeMaxAngle * shake;
        }

        forward = mc_norm_or(mc_sub(target, eye), forward);
        right = mc_norm_or(mc_cross(forward, worldUp), right);
        camUp = mc_cross(right, forward);
    }

    /* 14. Saída ------------------------------------------------------------ */
    out->eye = eye;
    out->target = target;
    out->up = mc_add(mc_scale(camUp, MC_COS(roll)), mc_scale(right, MC_SIN(roll)));
    out->fovDeg = s->fov + s->boostFov;
    out->yaw = MC_ATAN2(forward.x, forward.z);
    out->pitch = MC_ATAN2(forward.y, MC_SQRT(forward.x * forward.x + forward.z * forward.z));
    out->roll = roll;

    s->prevVehicleYaw = heading;
    s->prevVelY = in->vel.y;
    s->prevGrounded = in->grounded;
    s->prevPos = in->pos;
    s->prevSpeed01Raw = speed01;
    s->prevTargetYaw = targetYaw;
    s->prevBoost = in->boost;
}
