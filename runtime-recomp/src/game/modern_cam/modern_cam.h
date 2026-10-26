/*
 * modern_cam.h - camera de perseguição "moderna" para jogos de corrida/kart.
 *
 * Código original, sem dependência de engine. Pensado para ser plugado em
 * recomp/port de N64: você alimenta com o estado do veículo a cada tick e
 * recebe eye/target/up/FOV prontos para o guLookAt/guPerspective do jogo.
 *
 * O que ela faz (técnicas usadas em câmeras atuais, estilo GTA V / Forza):
 *   - Yaw segue o carro por uma MOLA angular, em vez de ficar travada atrás
 *     do carro. Numa curva constante ela acompanha o giro sem atraso fixo
 *     (yawKeepUp); o atraso aparece só quando o giro muda, na entrada e na
 *     saída da curva, e dura uma fração de segundo. Com yawKeepUp = 0 a
 *     mola atrasa o tempo todo, como numa câmera de cinema.
 *   - Mistura direção do nariz com direção da VELOCIDADE: no drift você vê o
 *     carro de lado; numa rodada a câmera continua olhando para onde você vai.
 *     A parcela de alinhamento passa por um filtro (alignSharpness), então
 *     começar ou terminar um drift gira a câmera em meio segundo, não de uma
 *     vez; a rodada não passa pelo filtro.
 *   - Distância, altura e FOV crescem com a velocidade (sensação de velocidade).
 *   - Look-ahead: mira um pouco à frente, na direção do movimento.
 *   - Vertical amortecida separada (pulos e lombadas não chacoalham a tela).
 *   - Acompanha a inclinação da pista suavemente.
 *   - Órbita manual no analógico direito com recentralização automática.
 *   - Colisão por callback (entra rápido, sai devagar).
 *   - Tremor por "trauma" (boost, aterrissagem), roll leve nas curvas.
 *   - Tudo depende de dt de forma exponencial: o mesmo comportamento a
 *     20, 30, 60 ou 144 fps.
 *
 * Melhorias opcionais (desligadas por padrão, ligue pelos campos enable*):
 *   - Antecipação pelo volante: começa a virar quando você esterça.
 *   - Mola na posição: inércia nas freadas e acelerações. Em velocidade
 *     constante a âncora fica em cima do carro (posKeepUp); só uma mudança
 *     de velocidade a desloca. Sem isso a âncora ficava metros atrás do
 *     carro em alta velocidade e, numa batida, avançava tudo de uma vez.
 *   - Afundada na aterrissagem: a câmera desce e volta ao aterrissar.
 *   - Puxão no turbo: o braço estica por um instante quando o turbo começa.
 *   - Tremor só rotacional: ruído em camadas girando a câmera.
 * São as da Bancada ModernCam; o "Exportar para C" de lá gera o bloco de
 * parâmetros pronto para colar. Desde 2026-10 este módulo vai além dela em
 * três pontos (a Bancada não os tem; o export continua válido, os campos
 * novos ficam no padrão): yawKeepUp, posKeepUp e alignSharpness.
 *
 * Convenções:
 *   - Y para cima. Ângulos em radianos.
 *   - Yaw: frente = (sin(yaw), 0, cos(yaw)), igual ao atan2s(x, z) de vários
 *     jogos de N64. Se o seu jogo usa outra convenção, converta antes.
 *   - Os parâmetros padrão estão em metros e m/s. Ajuste unitScale para a
 *     escala do jogo (ex.: se 1 metro = 10 unidades do jogo, unitScale = 10).
 *
 * Integração típica:
 *   1. Faça hook na função de câmera do jogo (a que preenche pos/lookAt).
 *   2. Monte um MCInput a partir da struct do jogador.
 *   3. Chame ModernCam_Update com o dt real daquele tick.
 *   4. Escreva out.eye / out.target / out.up (ou yaw/pitch convertidos com
 *      ModernCam_ToBinAngle) de volta na struct de câmera do jogo.
 *   5. Chame ModernCam_Reset ao trocar de pista, respawnar ou teleportar
 *      (teleportes grandes também são detectados sozinhos).
 *
 * Matemática: por padrão usa <math.h>. Se o ambiente não tiver libm
 * (comum em mods de recomp), compile com -DMC_NO_LIBM para usar as
 * aproximações internas.
 */
#ifndef MODERN_CAM_H
#define MODERN_CAM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y, z;
} MCVec3;

/* Estado do veículo neste tick. */
typedef struct {
    MCVec3 pos;       /* posição do veículo (unidades do jogo) */
    MCVec3 vel;       /* velocidade (unidades do jogo por SEGUNDO) */
    float yaw;        /* direção do nariz, em radianos */
    float slopePitch; /* inclinação do chão no sentido do nariz (+ = subida); 0 se não souber */
    int grounded;     /* 1 = rodas no chão */
    int drifting;     /* 1 = derrapando / power slide */
    float boost;      /* 0..1, intensidade de turbo neste tick */
    float stickX;     /* analógico direito, -1..1 (0 se não houver) */
    float stickY;
    int lookBack; /* 1 = olhar para trás pressionado */
    float steer;  /* volante, -1..1, + = virando para yaw POSITIVO (0 se não usar a antecipação) */
} MCInput;

/* Ajuste fino. ModernCam_DefaultParams preenche valores bons para kart. */
typedef struct {
    float unitScale; /* multiplica todas as distâncias e velocidades */

    /* enquadramento */
    float distMin, distMax;     /* distância atrás do carro, parado / velocidade máxima (m) */
    float heightMin, heightMax; /* altura da câmera acima do carro (m) */
    float targetHeight;         /* altura do ponto para onde a câmera olha (m) */
    float speedForMax;          /* velocidade em que zoom/FOV chegam ao máximo (m/s) */
    float zoomSharpness;        /* quão rápido o zoom acompanha a velocidade (1/s) */

    /* direção */
    float minAlignSpeed;   /* abaixo disso ignora a direção da velocidade (m/s) */
    float velocityAlign;   /* 0 = segue o nariz, 1 = segue a velocidade (pilotagem normal) */
    float driftAlign;      /* mesmo, durante drift */
    float driftLead;       /* giro extra para dentro da curva no drift (rad), estilo Mario Kart; 0 desliga */
    float spinFollowSpeed; /* acima disso, numa rodada a câmera segue a velocidade (m/s) */
    float alignSharpness;  /* quão rápido o alinhamento com a velocidade muda (1/s) */

    /* mola do yaw */
    float yawFreq;           /* frequência natural (Hz): maior = câmera mais presa */
    float yawFreqSpeedBoost; /* Hz extras na velocidade máxima */
    float yawDamping;        /* 1 = sem balanço; < 1 = balança um pouco ao voltar */
    float yawKeepUp;         /* 0..1: quanto do atraso da mola numa curva constante é compensado */

    /* seguimento de posição */
    float posSharpness;       /* horizontal (1/s) */
    float posKeepUp;          /* 0..1: quanto do atraso em velocidade constante é compensado (mola ou não) */
    float heightSharpness;    /* vertical no chão (1/s) */
    float airHeightSharpness; /* vertical no ar (1/s) */
    float maxVerticalLag;     /* atraso vertical máximo permitido (m) */

    /* terreno */
    float slopeFollow; /* 0..1, quanto a câmera acompanha a inclinação */
    float slopeSharpness;

    /* look-ahead */
    float lookAhead;    /* segundos de velocidade somados ao alvo */
    float lookAheadMax; /* limite (m) */
    float lookAheadSharpness;

    /* FOV (graus, vertical) */
    float fovBase;
    float fovSpeed; /* graus extras na velocidade máxima */
    float fovSharpness;
    float fovBoost; /* graus extras com turbo cheio */
    float boostFovAttack, boostFovRelease;

    /* roll */
    float rollFromYawRate; /* rad de roll por rad/s de giro; inverta o sinal se inclinar para o lado errado */
    float rollMax;
    float rollSharpness;

    /* órbita manual */
    float orbitSpeedYaw, orbitSpeedPitch; /* rad/s com o analógico no máximo */
    float recenterDelay;                  /* segundos sem mexer até recentralizar */
    float recenterSharpness;

    /* colisão */
    float collisionRadius; /* (m) */
    float collisionReleaseSharpness;

    /* tremor */
    float traumaDecay;        /* por segundo */
    float boostTrauma;        /* trauma mínimo enquanto em turbo cheio */
    float landingTrauma;      /* trauma numa aterrissagem forte */
    float landingSpeedForMax; /* velocidade de queda que dá trauma máximo (m/s) */
    float shakeFreq;          /* Hz */
    float shakeMaxAngle;      /* rad */
    float shakeMaxOffset;     /* m */

    float teleportDistance; /* salto maior que isso reseta a câmera (m) */

    /* melhorias: enable* = 1 liga, 0 desliga */
    int enableSteerLead;  /* antecipação pelo volante (usa MCInput.steer) */
    float steerLead;      /* giro extra com o volante no máximo (rad) */
    int enablePosSpring;  /* mola de 2ª ordem na posição horizontal (substitui posSharpness) */
    float posFreq;        /* Hz */
    float posDamping;     /* 1 = sem balanço */
    int enableLandingDip; /* afundada na aterrissagem */
    float landingDip;     /* profundidade (m) numa queda de landingSpeedForMax */
    float dipFreq, dipDamping;
    int enableBoostKick; /* puxão no início do turbo */
    float boostKick;     /* quanto o braço estica (m) */
    float kickFreq, kickDamping;
    int rotationalShake; /* 1 = tremor só rotacional, com ruído em camadas */
} MCParams;

/* Estado interno; zere com ModernCam_Reset (ou deixe initialized = 0). */
typedef struct {
    int initialized;
    float yaw, yawVel;
    MCVec3 anchor;
    MCVec3 lookAheadVec;
    float speed01;
    float slope;
    float fov, boostFov;
    float roll;
    float orbitYaw, orbitPitch;
    float idleTime;
    float collisionDist;
    float trauma;
    float time;
    float prevVehicleYaw;
    float prevVelY;
    int prevGrounded;
    /* alvos do tick anterior (mantêm o comportamento igual em qualquer FPS) */
    MCVec3 prevPos;
    MCVec3 prevLookAheadTarget;
    float prevSpeed01Raw;
    float prevTargetYaw;
    /* alinhamento filtrado e compensação do atraso de giro */
    float slipSm;     /* escorregamento filtrado (rad) */
    float turnRate;   /* taxa de giro filtrada que a mola do yaw acompanha (rad/s) */
    float prevVelYaw; /* direção da velocidade no tick anterior (rad) */
    MCVec3 prevVel;   /* velocidade medida no tick anterior (xz) */
    /* melhorias */
    MCVec3 anchorVel;
    float steerSm;
    float dipY, dipV;
    float kick, kickV;
    float prevBoost;
} MCState;

typedef struct {
    MCVec3 eye;    /* posição da câmera */
    MCVec3 target; /* ponto para onde olha */
    MCVec3 up;     /* vetor "para cima" já com roll */
    float fovDeg;  /* FOV vertical em graus */
    float yaw;     /* yaw da direção de visão (mesma convenção do input) */
    float pitch;   /* pitch da direção de visão (+ = olhando para cima) */
    float roll;
} MCOutput;

/*
 * Callback opcional de colisão: lança uma esfera de raio `radius` de `from`
 * até `to`. Se bater, escreva em *hitFraction a fração do caminho livre
 * (0..1) e retorne 1. Senão retorne 0. Passe NULL para desligar colisão.
 */
typedef int (*MCRaycastFn)(void* user, MCVec3 from, MCVec3 to, float radius, float* hitFraction);

void ModernCam_DefaultParams(MCParams* p);
void ModernCam_Reset(MCState* s, const MCParams* p, const MCInput* in);
void ModernCam_AddTrauma(MCState* s, float amount);
void ModernCam_Update(MCState* s, const MCParams* p, const MCInput* in, float dt, MCRaycastFn raycast, void* user,
                      MCOutput* out);

/* Radianos -> ângulo binário de 16 bits do N64 (0x10000 = volta completa). */
static inline short ModernCam_ToBinAngle(float rad) {
    return (short)(int)(rad * (32768.0f / 3.14159265f));
}

/* Ângulo binário de 16 bits do N64 -> radianos. */
static inline float ModernCam_FromBinAngle(short ang) {
    return (float)ang * (3.14159265f / 32768.0f);
}

#ifdef __cplusplus
}
#endif

#endif /* MODERN_CAM_H */
