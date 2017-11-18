#pragma once
/*
    z80.h -- Z80 CPU emulator

             +-----------+
    M1      -|           |- A0
    MREQ    -|           |- A1
    IORQ    -|           |- A2
    RD      -|           |- ...
    WR      -|           |- A15
    RFSH    -|           |
    HALT    -|           |
    WAIT    -|    Z80    |- D0
    INT     -|           |- D1
    NMI     -|           |- D2
    RESET   -|           |- ...
    BUSREQ  -|           |- D7
    BUSACK  -|           |
    CLK     -|           |
    +5V     -|           |
    GND     -|           |
             +-----------+

    Decoding Z80 instructions: http://z80.info/decoding.htm
*/
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* status indicator flags */
typedef enum {
    Z80_CF = (1<<0),        /* carry */
    Z80_NF = (1<<1),        /* add/subtract */
    Z80_VF = (1<<2),        /* parity/overflow */
    Z80_PF = Z80_VF,
    Z80_XF = (1<<3),        /* undocumented bit 3 */
    Z80_HF = (1<<4),        /* half carry */
    Z80_YF = (1<<5),        /* undocumented bit 5 */
    Z80_ZF = (1<<6),        /* zero */
    Z80_SF = (1<<7),        /* sign */
} z80_flags;

/* pin functions */
typedef enum {
    /* system control pins */
    Z80_M1    = (1<<0),          /* machine cycle 1 */
    Z80_MREQ  = (1<<1),          /* memory request */
    Z80_IORQ  = (1<<2),          /* input/output request */
    Z80_RD    = (1<<3),          /* read */
    Z80_WR    = (1<<4),          /* write */
    Z80_RFSH  = (1<<5),          /* refresh */

    /* CPU control pins */
    Z80_HALT  = (1<<6),          /* halt state */
    Z80_WAIT  = (1<<7),          /* wait state */
    Z80_INT   = (1<<8),          /* interrupt request */
    Z80_NMI   = (1<<9),          /* non-maskable interrupt */
    Z80_RESET = (1<<10),         /* reset */

    /* CPU bus control pins */
    Z80_BUSREQ = (1<<11),        /* bus request */
    Z80_BUSACK = (1<<12),        /* bus acknowledge */
} z80_pins;

/* Z80 CPU state */
typedef struct _z80 z80;
typedef struct _z80 {
    union {
        uint8_t r8[8];   /* to access with register index, flip bit 0 */
        uint16_t r16[4];
        struct {
            union { uint16_t BC; struct { uint8_t C, B; }; };
            union { uint16_t DE; struct { uint8_t E, D; }; };
            union { uint16_t HL; struct { uint8_t L, H; }; };
            union { uint16_t FA; struct { uint8_t A, F; }; };
        };
    };
    uint16_t BC_, DE_, HL_, AF_;
    uint16_t WZ, WZ_;
    union { uint16_t IX; struct { uint8_t IXL, IXH; }; };
    union { uint16_t IY; struct { uint8_t IYL, IYH; }; };
    union { uint16_t IR; struct { uint8_t R, I; }; };
    uint16_t SP;
    uint16_t PC;
    uint16_t CTRL;      /* control pins */
    uint16_t ADDR;      /* address pins */
    uint8_t DATA;       /* data pins */

    uint8_t im;
    bool imm1, imm2;
    uint32_t ticks;

    /* tick function and context data */
    void (*tick)(z80* cpu);
    void* context;
} z80;

typedef struct {
    void* tick_context;
    void (*tick_func)(z80* cpu);
} z80_desc;

/* initialize a new z80 instance */
extern void z80_init(z80* cpu, z80_desc* desc);
/* execute the next instruction, return number of time cycles */
extern uint32_t z80_step(z80* cpu);
/* execute instructions for up to 'ticks' time cycles, return executed time cycles */
extern uint32_t z80_run(z80* cpu, uint32_t ticks);
/* set one or more pins to active state */
extern void z80_on(z80* cpu, uint16_t pins);
/* set one or more pins to cleared state */
extern void z80_off(z80* cpu, uint16_t pins);
/* test if any control pin is active */
extern bool z80_any(z80* cpu, uint16_t pins);
/* test if all control pins are active */
extern bool z80_all(z80* cpu, uint16_t pins);

/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef CHIPS_IMPL
#include <string.h>
#ifndef CHIPS_DEBUG
    #ifdef _DEBUG
        #define CHIPS_DEBUG
    #endif
#endif
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

#ifdef _SZ
#undef _SZ
#endif
#ifdef _SZYXCH
#undef _SZYXCH
#endif
#ifdef _ADD_FLAGS
#undef _ADD_FLAGS
#endif
#ifdef _SUB_FLAGS
#undef _SUB_FLAGS
#endif
#ifdef _CP_FLAGS
#undef _CP_FLAGS
#endif
#ifdef _ON
#undef _ON
#endif
#ifdef _OFF
#undef _OFF
#endif
#ifdef _TICK
#undef _TICK
#endif
#ifdef _READ
#undef _READ
#endif
#define _SZ(val) ((val&0xFF)?(val&Z80_SF):Z80_ZF)
#define _SZYXCH(acc,val,res) (_SZ(res)|(res&(Z80_YF|Z80_XF))|((res>>8)&Z80_CF)|((acc^val^res)&Z80_HF))
#define _ADD_FLAGS(acc,val,res) (_SZYXCH(acc,val,res)|((((val^acc^0x80)&(val^res))>>5)&Z80_VF))
#define _SUB_FLAGS(acc,val,res) (Z80_NF|_SZYXCH(acc,val,res)|((((val^acc)&(res^acc))>>5)&Z80_VF))
#define _CP_FLAGS(acc,val,res) (Z80_NF|(_SZ(res)|(val&(Z80_YF|Z80_XF))|((res>>8)&Z80_CF)|((acc^val^res)&Z80_HF))|((((val^acc)&(res^acc))>>5)&Z80_VF))
#define _ON(m) { cpu->CTRL |= (m); }
#define _OFF(m) { cpu->CTRL &= ~(m); }
#define _TICK() { cpu->tick(cpu); cpu->ticks++; }
#define _WRITE(a,r) { _z80_write(cpu, a, r); }
#define _READ(a) { _z80_read(cpu, a); }

/*
    instruction fetch machine cycle (M1)
              T1   T2   T3   T4
    --------+----+----+----+----+
    CLK     |--**|--**|--**|--**|
    A15-A0  |   PC    | REFRESH |
    MREQ    |   *|****|  **|**  |
    RD      |   *|****|    |    |
    WAIT    |    | -- |    |    |
    M1      |****|****|    |    |
    D7-D0   |    |   X|    |    |
    RFSH    |    |    |****|****|

    Result is the fetched opcode in DATA member.
*/
static void _z80_fetch(z80* cpu) {
    /*--- T1 ---*/
    _ON(Z80_M1);
    cpu->ADDR = cpu->PC++;
    _TICK();
    /*--- T2 ---*/
    _ON(Z80_MREQ|Z80_RD);
    _TICK();
    cpu->R = (cpu->R&0x80)|((cpu->R+1)&0x7F);   /* update R */
    /*--- T3 ---*/
    _OFF(Z80_M1|Z80_MREQ|Z80_RD);
    _ON(Z80_RFSH);
    cpu->ADDR = cpu->IR;
    _TICK();
    /*--- T4 ---*/
    _ON(Z80_MREQ);
    _TICK();
    _OFF(Z80_RFSH|Z80_MREQ);
    /* opcode is in DATA member */
}

/*
    a memory read cycle, place address in ADDR, read byte into DATA

              T1   T2   T3
    --------+----+----+----+
    CLK     |--**|--**|--**|
    A15-A0  |   MEM ADDR   |
    MREQ    |   *|****|*** |
    RD      |   *|****|*** |
    WR      |    |    |    |
    D7-D0   |    |    | X  |
    WAIT    |    | -- |    |
*/
static void _z80_read(z80* cpu, uint16_t addr) {
    /*--- T1 ---*/
    cpu->ADDR = addr;
    _TICK();
    /*--- T2 ---*/
    _ON(Z80_MREQ|Z80_RD);
    _TICK();
    /*--- T3 ---*/
    _OFF(Z80_MREQ|Z80_RD);
    _TICK();
}

/*
    a memory write cycle, place 16-bit address into ADDR, place 8-bit
    value into DATA, and then memory[ADDR] = DATA

              T1   T2   T3
    --------+----+----+----+
    CLK     |--**|--**|--**|
    A15-A0  |   MEM ADDR   |
    MREQ    |   *|****|*** |
    RD      |    |    |    |
    WR      |    |  **|*** |
    D7-D0   |   X|XXXX|XXXX|
    WAIT    |    | -- |    |
*/
static void _z80_write(z80* cpu, uint16_t addr, uint8_t data) {
    /*--- T1 ---*/
    cpu->ADDR = addr;
    _TICK();
    /*--- T2 ---*/
    _ON(Z80_MREQ|Z80_WR);
    cpu->DATA = data;
    _TICK();
    /*--- T3 ---*/
    _OFF(Z80_MREQ|Z80_WR);
    _TICK();
}

/*-- MISC functions ----------------------------------------------------------*/
static void _z80_halt(z80* cpu) {
    // FIXME!
}

/*-- ALU functions -----------------------------------------------------------*/
static void _z80_add8(z80* cpu, uint8_t val) {
    int res = cpu->A + val;
    cpu->F = _ADD_FLAGS(cpu->A, val, res);
    cpu->A = (uint8_t) res;
}

static void _z80_adc8(z80* cpu, uint8_t val) {
    int res = cpu->A + val + (cpu->F & Z80_CF);
    cpu->F = _ADD_FLAGS(cpu->A, val, res);
    cpu->A = (uint8_t) res;
}

static void _z80_sub8(z80* cpu, uint8_t val) {
    int res = (int)cpu->A - (int)val;
    cpu->F = _SUB_FLAGS(cpu->A, val, res);
    cpu->A = (uint8_t) res;
}

static void _z80_sbc8(z80* cpu, uint8_t val) {
    int res = (int)cpu->A - (int)val - (cpu->F & Z80_CF);
    cpu->F = _SUB_FLAGS(cpu->A, val, res);
    cpu->A = (uint8_t) res;
}

static void _z80_cp8(z80* cpu, uint8_t val) {
    /* NOTE: XF|YF are set from val, not from result */
    int res = (int)cpu->A - (int)val;
    cpu->F = _CP_FLAGS(cpu->A, val, res);
}

static void _z80_neg8(z80* cpu) {
    uint8_t val = cpu->A;
    cpu->A = 0;
    _z80_sub8(cpu, val);
}

/*-- INSTRUCTION DECODERS ----------------------------------------------------*/
static void _z80_op(z80* cpu) {

    /*
        Expects current opcode in DATA.

        Split opcode into bit groups:
        |xx|yyy|zzz|
        |xx|ppq|zzz|
    */
    const uint8_t op = cpu->DATA;
    const uint8_t x = op>>6;
    const uint8_t y = (op>>3) & 7;
    const uint8_t z = op & 7;
    const uint8_t p = y>>1;
    const uint8_t q = y & 1;

    if (x == 1) {
        /* block 1: 8-bit loads and HALT */
        if (y == 6) {
            if (z == 6) {
                /* special case: LD HL,HL is HALT */
                _z80_halt(cpu);
            }
            else {
                /* LD (HL),r; LD (IX+d),r; LD (IY+d),r */
                _WRITE(cpu->HL, cpu->r8[z^1]);
            }
        }
        else if (z == 6) {
            /* LD r,(HL); LD r,(IX+d); LD r,(IY+d),r */
            _READ(cpu->HL); cpu->r8[y^1]=cpu->DATA;
        }
        else {
            /* LD r,r */
            cpu->r8[y^1] = cpu->r8[z^1];
        }
    }
    else if (x == 2) {
        /* block 2: 8-bit ALU instructions */

    }
    else if (x == 0) {
        /* block 0: misc instructions */
        switch (z) {
            case 0:
                switch (y) {
                    case 0: /* NOP */ break;
                    case 1: /* EX AF,AF' */ break;
                    case 2: /* DJNZ */ break;
                    case 3: /* JR d */ break;
                    default: /* JR cc,d */ break;
                }
                break;
            case 1:
                if (q == 0) {
                    /* 16-bit immediate loads */
                }
                else {
                    /* ADD HL,rr; ADD IX,rr; ADD IY,rr */
                }
                break;
            case 2:
                /* indirect loads */
                break;
            case 3:
                /* 16-bit INC,DEC */
                break;
            case 4:
                /* INC */
                break;
            case 5:
                /* DEC */
                break;
            case 6:
                if (y == 6) {
                    /* LD (HL),n; LD (IX+d),n; LD (IY+d),n */
                }
                else {
                    /* LD r,n */
                    _READ(cpu->PC++); cpu->r8[y^1]=cpu->DATA;
                }
                break;
            case 7:
                /* misc ops on A and F */
                break;
        }
    }
    else if (x == 3) {
        /* block 3: misc and extended instructions */
    }
}

void z80_init(z80* cpu, z80_desc* desc) {
    CHIPS_ASSERT(cpu);
    CHIPS_ASSERT(desc);
    CHIPS_ASSERT(desc->tick_func);
    memset(cpu, 0, sizeof(z80));
    cpu->tick = desc->tick_func;
    cpu->context = desc->tick_context;
}

uint32_t z80_step(z80* cpu) {
    cpu->ticks = 0;
    _z80_fetch(cpu);
    _z80_op(cpu);
    return cpu->ticks;
}

uint32_t z80_run(z80* cpu, uint32_t t) {
    // FIXME
    return 0;
}

/* control pin functions */
void z80_on(z80* cpu, uint16_t pins) {
    cpu->CTRL |= pins;
}
void z80_off(z80* cpu, uint16_t pins) {
    cpu->CTRL &= ~pins;
}
bool z80_any(z80* cpu, uint16_t pins) {
    return (cpu->CTRL & pins) != 0;
}
bool z80_all(z80* cpu, uint16_t pins) {
    return (cpu->CTRL & pins) == pins;
}

#undef _SZ
#undef _SZYXCH
#undef _ADD_FLAGS
#undef _SUB_FLAGS
#undef _CP_FLAGS
#undef _ON
#undef _OFF
#undef _TICK
#endif /* CHIPS_IMPL */

#ifdef __cplusplus
} /* extern "C" */
#endif