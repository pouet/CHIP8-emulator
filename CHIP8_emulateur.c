#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

enum {
  FLAG     = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER,
  FLAG_PAL = SDL_LOGPAL | SDL_PHYSPAL,
  RES_W    = 64,
  RES_H    = 32,
  SZ_PIX   = 10,
  WIDTH    = RES_W * SZ_PIX,
  HEIGHT   = RES_H * SZ_PIX,
  BPP      = 8
};

enum {
  MEM_TOT     = 0xFFF,
  MEM_START   = 0x200,
  N_REG       = 0x10,
  N_STACK     = 0x10,
  FREQ        = 250,
  FREQ_DELAY  = 1000 / FREQ,
  FPS         = 60,
  FPS_DELAY   = 1000 / FPS,
  N_KEYS      = 0x10
};

enum {
  DLEN_WAVE = 13416
};

Uint8 gData[DLEN_WAVE] = { 0 };
/*
 dlen = 13416
 48 -> 5A/59
 48 -> A5/A6
 */
struct sample {
  Uint8 * data;
  Uint32 dpos;
  Uint32 dlen;
} sounds;

typedef struct sCPU {
  Uint8   mem[MEM_TOT];   /* Mémoire */
  Uint16  pc;             /* Variable qui pointe sur l'adresse dans mem */
  Uint8   regV[N_REG];    /* Registres */
  Uint16  regI;           /* Registre d'instruction */
  Uint16  stack[N_STACK]; /* Pile */
  Uint8   nStack;         /* Variable de référence de la pile */
  Uint8   nCountSys;      /* Compteur système */
  Uint8   nCountSound;    /* Compteur sonore */
  Uint8 * keystate;       /* Clavier entier */
  Uint8 * pKey[N_KEYS];   /* Ptrs sur les touches pour la correspondance */
  Uint8   key[N_KEYS];    /* Clavier chip */
} sCPU;

/* Raffraichissement de l'écran */
SDL_Rect gRectUpdate[RES_H * RES_W];
Uint32 gNRectUpdate;

Uint8 SDLkeysToCHIPkeys[N_KEYS] = {
  SDLK_b, /* 0 */
  SDLK_q, /* 1 */
  SDLK_w, /* 2 */
  SDLK_e, /* 3 */
  SDLK_a, /* 4 */
  SDLK_s, /* 5 */
  SDLK_d, /* 6 */
  SDLK_z, /* 7 */
  SDLK_x, /* 8 */
  SDLK_c, /* 9 */
  SDLK_r, /* A */
  SDLK_t, /* B */
  SDLK_y, /* C */
  SDLK_f, /* D */
  SDLK_g, /* E */
  SDLK_h  /* F */
};

Uint8 car[] = {
  0xF0, 0x90, 0x90, 0x90, 0xF0, /* 0 */
  0x20, 0x60, 0x20, 0x20, 0x70, /* 1 */
  0xF0, 0x10, 0xF0, 0x80, 0xF0, /* 2 */
  0xF0, 0x10, 0xF0, 0x10, 0xF0, /* 3 */
  0x90, 0x90, 0xF0, 0x10, 0x10, /* 4 */
  0xF0, 0x80, 0xF0, 0x10, 0xF0, /* 5 */
  0xF0, 0x80, 0xF0, 0x90, 0xF0, /* 6 */
  0xF0, 0x10, 0x20, 0x40, 0x40, /* 7 */
  0xF0, 0x90, 0xF0, 0x90, 0xF0, /* 8 */
  0xF0, 0x90, 0xF0, 0x10, 0xF0, /* 9 */
  0xF0, 0x90, 0xF0, 0x90, 0x90, /* A */
  0xE0, 0x90, 0xE0, 0x90, 0xE0, /* B */
  0xF0, 0x80, 0x80, 0x80, 0xF0, /* C */
  0xE0, 0x90, 0x90, 0x90, 0xE0, /* D */
  0xF0, 0x80, 0xF0, 0x80, 0xF0, /* E */
  0xF0, 0x80, 0xF0, 0x80, 0x80  /* F */
};


void CPU_init(sCPU * cpu) {
  Uint8 i;
  
  if (cpu == NULL)
    return;
  
  memset(cpu, 0, sizeof *cpu);
  memcpy(cpu->mem, car, sizeof car);
  cpu->pc = MEM_START;
  
  cpu->keystate = SDL_GetKeyState(NULL);
  for (i = 0; i < N_KEYS; i++)
    cpu->pKey[i] = &cpu->keystate[SDLkeysToCHIPkeys[i]];
}

void CPU_downcount(sCPU * cpu) {
  if (cpu->nCountSys > 0)
    cpu->nCountSys--;
  if (cpu->nCountSound > 0)
    cpu->nCountSound--;
}

void aprintf(char * s) {
  if (s)
    fprintf(stderr, "%s\n", s);
  else
    fprintf(stderr, "Une erreur est survenue.\n");
}

/*
 * Return the pixel value at (x, y)
 * NOTE: The surface must be locked before calling this!
 */
Uint32 getpixel(SDL_Surface *surface, int x, int y)
{
  int bpp = surface->format->BytesPerPixel;
  /* Here p is the address to the pixel we want to retrieve */
  Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
  
  if ((unsigned) x > WIDTH || (unsigned) y > HEIGHT)
    return 0;
  
  switch(bpp) {
    case 1:
      return *p;
      
    case 2:
      return *(Uint16 *)p;
      
    case 3:
      if(SDL_BYTEORDER == SDL_BIG_ENDIAN)
        return p[0] << 16 | p[1] << 8 | p[2];
      else
        return p[0] | p[1] << 8 | p[2] << 16;
      
    case 4:
      return *(Uint32 *)p;
      
    default:
      return 0;       /* shouldn't happen, but avoids warnings */
  }
}

void nextInstruction(sCPU * cpu) {
  cpu->pc += 2;
}

void affectEvent(sCPU * cpu) {
  Uint8 i;
  
  for (i = 0; i < N_KEYS; i++)
    cpu->key[i] = *cpu->pKey[i];
}

/* >>>>>>>>>>>>>>>>>>> OP CODES <<<<<<<<<<<<<<<<<<<<<<< */

/* Efface l'écran */
void opCode00E0(sCPU * cpu, Uint16 op) {
  (void)cpu;
  (void)op;

  SDL_FillRect(SDL_GetVideoSurface(), NULL, 0);
  gNRectUpdate = 1;
  gRectUpdate[0].h = HEIGHT;
  gRectUpdate[0].w = WIDTH;
  gRectUpdate[0].x = 0;
  gRectUpdate[0].y = 0;
}

/* Retourne à partir d'un sous programme (saut) */
void opCode00EE(sCPU * cpu, Uint16 op) {
  (void)op;

  if (cpu->nStack == 0) {
    aprintf("Stack empty");
    return;
  }
  cpu->nStack--;
  cpu->pc = cpu->stack[cpu->nStack];
}

/* Effectue un saut à l'adresse NNN */
void opCode1NNN(sCPU * cpu, Uint16 op) {
  cpu->pc = op & 0x0FFF;
  
  cpu->pc -= 2;
}

/* Exécute le sous programme à l'adresse NNN */
void opCode2NNN(sCPU * cpu, Uint16 op) {
  if (cpu->nStack == N_STACK - 1) {
    aprintf("Stack full");
    return;
  }
  cpu->stack[cpu->nStack] = cpu->pc;
  cpu->nStack++;
  cpu->pc = op & 0x0FFF;
  
  cpu->pc -= 2;
}

/* Saute l'instruction suivante si VX est égal à NN */
void opCode3XNN(sCPU * cpu, Uint16 op) {
  if (cpu->regV[(op >> 8) & 0xF] == (op & 0xFF))
    nextInstruction(cpu);
}

/* Saute l'instruction suivante si VX et NN ne sont pas égaux. */
void opCode4XNN(sCPU * cpu, Uint16 op) {
  if (cpu->regV[(op >> 8) & 0xF] != (op & 0xFF))
    nextInstruction(cpu);
}

/* Saute l'instruction suivante si VX et VY sont égaux */
void opCode5XY0(sCPU * cpu, Uint16 op) {
  if (cpu->regV[(op >> 8) & 0xF] == cpu->regV[(op >> 4) & 0xF])
    nextInstruction(cpu);
}

/* Définit VX à NN */
void opCode6XNN(sCPU * cpu, Uint16 op) {
  cpu->regV[(op >> 8) & 0xF] = op & 0xFF;
}

/* Ajoute NN à VX */
void opCode7XNN(sCPU * cpu, Uint16 op) {
  cpu->regV[(op >> 8) & 0xF] += (op & 0xFF);
}

/* Définit VX à la valeur de VY */
void opCode8XY0(sCPU * cpu, Uint16 op) {
  cpu->regV[(op >> 8) & 0xF] = cpu->regV[(op >> 4) & 0xF];
}

/* Définit VX à VX OR VY. */
void opCode8XY1(sCPU * cpu, Uint16 op) {
  cpu->regV[(op >> 8) & 0xF] |= cpu->regV[(op >> 4) & 0xF];
}

/* Définit VX à VX AND VY. */
void opCode8XY2(sCPU * cpu, Uint16 op) {
  cpu->regV[(op >> 8) & 0xF] &= cpu->regV[(op >> 4) & 0xF];
}

/* Définit VX à VX xOR VY. */
void opCode8XY3(sCPU * cpu, Uint16 op) {
  cpu->regV[(op >> 8) & 0xF] ^= cpu->regV[(op >> 4) & 0xF];
}

/* Ajoute VY à VX .
 VF est mis à 1 quand il y a un overflow, et à 0 quand il n'y en pas.
 */
void opCode8XY4(sCPU * cpu, Uint16 op) {
  Uint16 n;
  
  n = cpu->regV[(op >> 8) & 0xF] + cpu->regV[(op >> 4) & 0xF];
  if (n > 0xFF)
    cpu->regV[0xF] = 1;
  else
    cpu->regV[0xF] = 0;
  
  cpu->regV[(op >> 8) & 0xF] = n;
}

/* VY est soustraite de VX. VF est mis à 0 quand il y a un emprunt,
 et à 1 quand il n'y a en pas.
 */
void opCode8XY5(sCPU * cpu, Uint16 op) {
  Sint16 n;
  
  n = cpu->regV[(op >> 8) & 0xF] - cpu->regV[(op >> 4) & 0xF];
  if (n < 0)
    cpu->regV[0xF] = 0;
  else
    cpu->regV[0xF] = 1;
  
  cpu->regV[(op >> 8) & 0xF] = (Uint8) (n & 0xFF);
}

/* Décale (shift) VX à droite de 1 bit.VF est fixé à la valeur du bit de
 poids faible de VX avant le décalage
 */
void opCode8XY6(sCPU * cpu, Uint16 op) {
  cpu->regV[0xF] = cpu->regV[(op >> 8) & 0xF] & 1;
  cpu->regV[(op >> 8) & 0xF] >>= 1;
}

/* VX = VY - VX,VF est mis à 0 quand il y a un emprunt, et à 1 quand il
 n'y en a pas.
 */
void opCode8XY7(sCPU * cpu, Uint16 op) {
  Sint16 n;
  
  n = cpu->regV[(op >> 4) & 0xF] - cpu->regV[(op >> 8) & 0xF];
  if (n < 0)
    cpu->regV[0xF] = 0;
  else
    cpu->regV[0xF] = 1;
  
  cpu->regV[(op >> 8) & 0xF] = (Uint8) ((Uint16) n & 0xFF);
}

/* Décale (shift) VX à gauche de 1 bit. VF est fixé à la valeur du bit de
 poids fort de VX avant le décalage.
 */
void opCode8XYE(sCPU * cpu, Uint16 op) {
  cpu->regV[0xF] = (cpu->regV[(op >> 8) & 0xF] >> 7) & 1;
  cpu->regV[(op >> 8) & 0xF] <<= 1;
}

/* Saute l'instruction suivante si VX et VY ne sont pas égaux */
void opCode9XY0(sCPU * cpu, Uint16 op) {
  if (cpu->regV[(op >> 8) & 0xF] != cpu->regV[(op >> 4) & 0xF])
    nextInstruction(cpu);
}

/* Affecte NNN à I */
void opCodeANNN(sCPU * cpu, Uint16 op) {
  cpu->regI = op & 0xFFF;
}

/* Passe à l'adresse NNN + V0 */
void opCodeBNNN(sCPU * cpu, Uint16 op) {
  cpu->pc = cpu->regV[0] + (op & 0xFFF);
  
  cpu->pc -= 2;
}

/* Définit VX à un nombre aléatoire inférieur ou égal à NN */
void opCodeCXNN(sCPU * cpu, Uint16 op) {
  cpu->regV[(op >> 8) & 0xF] = rand() % ((op & 0x00FF) + 1);
}

/*
 Dessine un sprite aux coordonnées (VX, VY).
 Le sprite a une largeur de 8 pixels et une hauteur de pixels N. 
 Chaque rangée de 8 pixels est lue comme codée en binaire à partir de
 l'emplacement mémoire I.
 I ne change pas de valeur après l'exécution de cette instruction.
 */
void opCodeDXYN(sCPU * cpu, Uint16 op) {
  SDL_Surface * screen = SDL_GetVideoSurface();
  SDL_Rect r, s;
  Uint8 i, j, x, y;
  
  cpu->regV[0xF] = 0;
  x = cpu->regV[(op >> 8) & 0xF];
  y = cpu->regV[(op >> 4) & 0xF];
  
  r.w = SZ_PIX;
  r.h = SZ_PIX;
  
  if (SDL_MUSTLOCK(screen))
    SDL_LockSurface(screen);
  
  for (i = 0; i < (op & 0xF); i++, y++) {
    Uint8 cod = cpu->mem[cpu->regI + i];
    r.y = y * SZ_PIX;
    
    for (j = 8; j > 0; j--, cod >>= 1) {
      r.x = (x + j - 1) * SZ_PIX;
      
      if ((unsigned) r.x >= WIDTH || (unsigned) r.y >= HEIGHT)
        continue;
      /* Pixel allumé et pixel à 1 -> on éteind + regV[0xF] = 1 */
      if (getpixel(screen, r.x, r.y) == 1 && (cod & 1)) {
        cpu->regV[0xF] = 1;
        s = r;
        SDL_FillRect(screen, &s, 0);
        gRectUpdate[gNRectUpdate++] = r;
      }
      /* On allume le pixel */
      else if (cod & 1) {
        s = r;
        SDL_FillRect(screen, &s, 1);
        gRectUpdate[gNRectUpdate++] = r;
      }
    }
  }
  
  if (SDL_MUSTLOCK(screen))
    SDL_UnlockSurface(screen);
}

/* Saute l'instruction suivante si la clé stockée dans VX est pressée */
void opCodeEX9E(sCPU * cpu, Uint16 op) {
  if (cpu->key[cpu->regV[(op >> 8) & 0xF]] != 0)
    nextInstruction(cpu);
}

/* Saute l'instruction suivante si la clé stockée dans VX n'est pas pressée */
void opCodeEXA1(sCPU * cpu, Uint16 op) {
  if (cpu->key[cpu->regV[(op >> 8) & 0xF]] == 0)
    nextInstruction(cpu);
}

/* Définit VX à la valeur de la temporisation */
void opCodeFX07(sCPU * cpu, Uint16 op) {
  cpu->regV[(op >> 8) & 0xF] = cpu->nCountSys;
}

/* L'appui sur une touche est attendue, et ensuite stockée dans VX */
void opCodeFX0A(sCPU * cpu, Uint16 op) {
  SDL_Event ev;
  SDL_bool done;
  Uint8 i;
  
  done = SDL_FALSE;
  while (!done) {
    SDL_WaitEvent(&ev);
    affectEvent(cpu);
    
    if (ev.type == SDL_QUIT)
      exit(EXIT_SUCCESS);
    else if (ev.type == SDL_KEYDOWN) {
      for (i = 0; i < N_KEYS; i++) {
        if (ev.key.keysym.sym == SDLkeysToCHIPkeys[i]) {
          cpu->regV[(op >> 8) & 0xF] = i;
          return;
        }
      }
    }
  }
}

/* Définit la temporisation à VX */
void opCodeFX15(sCPU * cpu, Uint16 op) {
  cpu->nCountSys = cpu->regV[(op >> 8) & 0xF];
}

/* Définit la tempo sonore à VX */
void opCodeFX18(sCPU * cpu, Uint16 op) {
  cpu->nCountSound = cpu->regV[(op >> 8) & 0xF];
}

/* Ajoute VX à I. VF est mis à 1 quand il y a overflow (I+VX>0xFFF),
 et à 0 si tel n'est pas le cas */
void opCodeFX1E(sCPU * cpu, Uint16 op) {
  Uint32 n;
  
  n = cpu->regI + cpu->regV[(op >> 8) & 0xF];
  if (n > 0xFFF)
    cpu->regV[0xF] = 1;
  else
    cpu->regV[0xF] = 0;
  
  cpu->regI = n & 0xFFFF;
}

/* Définit I à l'emplacement du caractère stocké dans VX */
void opCodeFX29(sCPU * cpu, Uint16 op) {
  cpu->regI = 5 * cpu->regV[(op >> 8) & 0xF];
}

/* Stores the Binary-coded decimal representation of VX, with the most
 significant of three digits at the address in I, the middle digit at
 I plus 1, and the least significant digit at I plus 2.
 */
void opCodeFX33(sCPU * cpu, Uint16 op) {
  Uint8 n = cpu->regV[(op >> 8) & 0xF];
  
  cpu->mem[cpu->regI]     = n / 100;
  cpu->mem[cpu->regI + 1] = (n / 10) % 10;
  cpu->mem[cpu->regI + 2] = n % 10;
}

/* Stocke V0 à VX en mémoire à partir de l'adresse I */
void opCodeFX55(sCPU * cpu, Uint16 op) {
  memcpy(&cpu->mem[cpu->regI], cpu->regV, ((op >> 8) & 0xF) + 1);
}

/* Remplit V0 à VX avec les valeurs de la mémoire à partir de l'adresse I */
void opCodeFX65(sCPU * cpu, Uint16 op) {
  memcpy(cpu->regV, &cpu->mem[cpu->regI], ((op >> 8) & 0xF) + 1);
}

/* >>>>>>>>>>>>>>>>>> FIN OP CODE <<<<<<<<<<<<<<<<<<<<<< */

Uint16 getOpCode(sCPU * cpu) {
  return (Uint16) (cpu->mem[cpu->pc] << 8) + cpu->mem[cpu->pc + 1];
}

void applyOpCode(sCPU * cpu) {
  Uint16 op;
  int i;
  
  static struct {
    Uint16 mask;
    Uint16 compar;
    void (*p)(sCPU *, Uint16);
  } tab[35] = {
    /* 0NNN juste pour faire joli, jamais utilisé */
    { 0x0000, 0xFFFF, NULL       }, /* 0NNN */
    { 0xFFFF, 0x00E0, opCode00E0 }, /* 00E0 */
    { 0xFFFF, 0x00EE, opCode00EE }, /* 00EE */
    { 0xF000, 0x1000, opCode1NNN }, /* 1NNN */
    { 0xF000, 0x2000, opCode2NNN }, /* 2NNN */
    { 0xF000, 0x3000, opCode3XNN }, /* 3XNN */
    { 0xF000, 0x4000, opCode4XNN }, /* 4XNN */
    { 0xF00F, 0x5000, opCode5XY0 }, /* 5XY0 */
    { 0xF000, 0x6000, opCode6XNN }, /* 6XNN */
    { 0xF000, 0x7000, opCode7XNN }, /* 7XNN */
    { 0xF00F, 0x8000, opCode8XY0 }, /* 8XY0 */
    { 0xF00F, 0x8001, opCode8XY1 }, /* 8XY1 */
    { 0xF00F, 0x8002, opCode8XY2 }, /* 8XY2 */
    { 0xF00F, 0x8003, opCode8XY3 }, /* BXY3 */
    { 0xF00F, 0x8004, opCode8XY4 }, /* 8XY4 */
    { 0xF00F, 0x8005, opCode8XY5 }, /* 8XY5 */
    { 0xF00F, 0x8006, opCode8XY6 }, /* 8XY6 */
    { 0xF00F, 0x8007, opCode8XY7 }, /* 8XY7 */
    { 0xF00F, 0x800E, opCode8XYE }, /* 8XYE */
    { 0xF00F, 0x9000, opCode9XY0 }, /* 9XY0 */
    { 0xF000, 0xA000, opCodeANNN }, /* ANNN */
    { 0xF000, 0xB000, opCodeBNNN }, /* BNNN */
    { 0xF000, 0xC000, opCodeCXNN }, /* CXNN */
    { 0xF000, 0xD000, opCodeDXYN }, /* DXYN */
    { 0xF0FF, 0xE09E, opCodeEX9E }, /* EX9E */
    { 0xF0FF, 0xE0A1, opCodeEXA1 }, /* EXA1 */
    { 0xF0FF, 0xF007, opCodeFX07 }, /* FX07 */
    { 0xF0FF, 0xF00A, opCodeFX0A }, /* FX0A */
    { 0xF0FF, 0xF015, opCodeFX15 }, /* FX15 */
    { 0xF0FF, 0xF018, opCodeFX18 }, /* FX18 */
    { 0xF0FF, 0xF01E, opCodeFX1E }, /* FX1E */
    { 0xF0FF, 0xF029, opCodeFX29 }, /* FX29 */
    { 0xF0FF, 0xF033, opCodeFX33 }, /* FX33 */
    { 0xF0FF, 0xF055, opCodeFX55 }, /* FX55 */
    { 0xF0FF, 0xF065, opCodeFX65 }  /* FX65 */
  };
  
  op = getOpCode(cpu);
  
  for (i = 0; i < 35; i++) {
    if ((op & tab[i].mask) == tab[i].compar) {
      tab[i].p(cpu, op);
      break;
    }
  }
  
  nextInstruction(cpu);
}

/* Fonction de callback pour le son */
void mixaudio(void * unused, Uint8 * stream, int len) {
  Uint32 amount;

  (void)unused;

  if (len < 0)
      return;
  
  amount = sounds.dlen - sounds.dpos;
  if (amount > (Uint32)len)
    amount = len;
  SDL_MixAudio(stream, &sounds.data[sounds.dpos], amount, SDL_MIX_MAXVOLUME);
  sounds.dpos += amount;
}

int createSound(void) {
  int i, j, k;
  SDL_AudioSpec fmt;
  
  fmt.freq = 22050;
  fmt.format = AUDIO_S8;
  fmt.channels = 2;
  fmt.samples = 512;
  fmt.callback = mixaudio;
  fmt.userdata = NULL;
  
  
  k = 0;
  for (j = 0, i = 0; i < DLEN_WAVE; i++, j++) {
    if (j >= 48) {
      j = 0;
      k = 1 - k;
    }
    if (k == 0)
      gData[i] = 0x48;
    else
      gData[i] = 0xA5;
  }
  sounds.data = gData;
  sounds.dlen = DLEN_WAVE;
  sounds.dpos = 0;
  
  if (SDL_OpenAudio(&fmt, NULL) < 0)
    return -1;
  
  return 0;
}

int init(void) {
  SDL_Color col[2];
  
  srand(time(NULL));
  
  if (SDL_Init(FLAG) < 0) {
    aprintf(SDL_GetError());
    return -1;
  }
  if (SDL_SetVideoMode(WIDTH, HEIGHT, BPP, SDL_SWSURFACE) == NULL) {
    aprintf(SDL_GetError());
    return -1;
  }
  
  col[0].r = 0x00;
  col[0].g = 0x00;
  col[0].b = 0x00;
  col[1].r = 0xFF;
  col[1].g = 0xFF;
  col[1].b = 0xFF;
  if (SDL_SetPalette(SDL_GetVideoSurface(), FLAG_PAL, col, 0, 2) == 0) {
    aprintf("Erreur d'initialisation de la palette.");
    return -1;
  }
  
  if (createSound() < 0) {
    aprintf(SDL_GetError());
    return -1;
  }
  
  return 0;
}

void release(void) {
  SDL_CloseAudio();
  SDL_Quit();
}

int loadROM(sCPU * cpu, char * s) {
  FILE * f;
  int ret = 0;
  unsigned len = sizeof(Uint8) * (MEM_TOT - MEM_START);
  
  if (!cpu || !s)
    return -1;
  
  f = fopen(s, "rb");
  if (!f) {
    perror("File not open");
    return -1;
  }

  fread(&cpu->mem[MEM_START], len, 1, f);
  if (feof(f) && ferror(f)) {
    aprintf("File corrupted.");
    ret = -1;
  }
  fclose(f);
  
  return ret;
}

void playSound(void) {
  SDL_LockAudio();
  sounds.dpos = 0;
  SDL_UnlockAudio();
  SDL_PauseAudio(0);
}

int loop(char *rom) {
  SDL_Surface * screen;
  SDL_Event event;
  SDL_bool done;
  Uint32 curTime;
  Uint32 nxtTimeCPU;
  Uint32 nxtTimeVID;
  sCPU cpu;
  
  CPU_init(&cpu);
  
  if (loadROM(&cpu, rom) < 0) {
    aprintf("ROM not loaded");
    return -1;
  }
  
  screen = SDL_GetVideoSurface();
  SDL_FillRect(screen, NULL, 0);
  SDL_Flip(screen);
  
  curTime = SDL_GetTicks();
  nxtTimeCPU = curTime + FREQ_DELAY;
  nxtTimeVID = curTime + FPS_DELAY;
  done = SDL_FALSE;
  while (!done) {
    curTime = SDL_GetTicks();
    
    if (curTime >= nxtTimeCPU) {
      nxtTimeCPU = curTime + FREQ_DELAY;
      
      if (cpu.nCountSound != 0)
        playSound();
      
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT)
          done = SDL_TRUE;
      }
      if (cpu.keystate[SDLK_ESCAPE])
        done = SDL_TRUE;
      affectEvent(&cpu);
      
      applyOpCode(&cpu);
      CPU_downcount(&cpu);
    }
    
    if (curTime >= nxtTimeVID) {
      nxtTimeVID = curTime + FPS_DELAY;
      
      SDL_UpdateRects(screen, gNRectUpdate, gRectUpdate);
      gNRectUpdate = 0;
    }
  }
  
  return 0;
}

int main(int argc, char ** argv) {
	if (argc != 2)
    return EXIT_FAILURE;
  if (init() < 0)
    return EXIT_FAILURE;
  
  loop(argv[1]);
  
  release();
  return EXIT_SUCCESS;
}
