#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>

/* Plants vs. Zombies - versao de estudo em C + Allegro 5.
   Arte e mecanicas simplificadas e desenhadas com formas geometricas. */
#define SW 1100
#define SH 720
#define FPS 60.0
#define ROWS 6
#define COLS 9
#define CELL 78
#define GX 244
#define GY 137
#define MAX_ZOMBIES 110
#define MAX_SHOTS 180
#define MAX_SUNS 45
#define PI 3.14159265358979323846

typedef enum { MENU, HELP, PLAYING, PAUSED, WON, LOST } GameState;
typedef enum { EMPTY, SUNFLOWER, PEASHOOTER, WALLNUT, ICEPEA, REPEATER, CHERRY, LILYPAD, POTATO } PlantType;
typedef enum { NORMAL, RUNNER, CONE, BUCKET, NEWSPAPER, POLE, SCREEN, FOOTBALL, BASKETBALL, SNORKEL, DOLPHIN, GARGANTUAR } ZombieType;

typedef struct { PlantType type; float hp, timer, anim; bool pad; } Plant;
typedef struct { bool active; ZombieType type; int row; float x, hp, maxhp, speed, bite, ability; bool jumped; } Zombie;
typedef struct { bool active; int row; float x, y, speed, damage; bool ice; } Shot;
typedef struct { bool active; float x, y, age; int amount; } Sun;
typedef struct { bool active; int row; float x, y, vx; } BasketBall;
#define MAX_BALLS 48
typedef struct {
    Plant plants[ROWS][COLS];
    Zombie zombies[MAX_ZOMBIES];
    Shot shots[MAX_SHOTS];
    Sun suns[MAX_SUNS];
    BasketBall balls[MAX_BALLS];
    GameState state;
    PlantType selected;
    bool shovel;
    float hit_flash, wave_banner;
    int money, level, score, kills, spawned, wave_total;
    float elapsed, spawn_timer, sky_sun_timer, message_timer;
    char message[120];
} Game;

static ALLEGRO_DISPLAY *display;
static ALLEGRO_EVENT_QUEUE *queue;
static ALLEGRO_TIMER *timer;
static ALLEGRO_FONT *font;
static ALLEGRO_SAMPLE *snd_plant=NULL,*snd_shot=NULL,*snd_sun=NULL,*snd_hit=NULL,*snd_groan=NULL,*snd_lose=NULL,*snd_splash=NULL,*snd_shovel=NULL;
static bool sound_on=false;
static float lose_anim=0.0f, groan_cd=0.0f;
static void play_sound(ALLEGRO_SAMPLE *sample, float gain){
    if(sound_on && sample) al_play_sample(sample,gain,0.0f,1.0f,ALLEGRO_PLAYMODE_ONCE,NULL);
}
static void load_sounds(void){
    if(!al_install_audio()) return;
    al_init_acodec_addon();
    if(!al_reserve_samples(24)) return;
    sound_on=true;
    snd_plant=al_load_sample("audio/plantar.wav");
    snd_shot=al_load_sample("audio/tiro.wav");
    snd_sun=al_load_sample("audio/sol.wav");
    snd_hit=al_load_sample("audio/impacto.wav");
    snd_groan=al_load_sample("audio/zumbi.wav");
    snd_lose=al_load_sample("audio/nao.wav");
    snd_splash=al_load_sample("audio/impacto.wav");
    snd_shovel=al_load_sample("audio/plantar.wav");
}
static void free_sounds(void){
    if(snd_plant)al_destroy_sample(snd_plant);
    if(snd_shot)al_destroy_sample(snd_shot);
    if(snd_sun)al_destroy_sample(snd_sun);
    if(snd_hit)al_destroy_sample(snd_hit);
    if(snd_groan)al_destroy_sample(snd_groan);
    if(snd_lose)al_destroy_sample(snd_lose);
    if(snd_splash)al_destroy_sample(snd_splash);
    if(snd_shovel)al_destroy_sample(snd_shovel);
    if(sound_on)al_uninstall_audio();
}
static Game g;
static bool running = true;
static float mouse_x = 0, mouse_y = 0;

static ALLEGRO_COLOR rgb(int r, int gr, int b) { return al_map_rgb(r, gr, b); }
static float rand01(void) { return (float)rand() / (float)RAND_MAX; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float dist2(float x1, float y1, float x2, float y2) {float dx=x1-x2,dy=y1-y2;return dx*dx+dy*dy;}
static float row_y(int r) {return GY + r*CELL + CELL*0.56f;}
static float col_x(int c) {return GX + c*CELL + CELL*0.5f;}
static bool is_pool(void){ return g.level>=4; }
static bool water_row(int r){return is_pool() && (r==2 || r==3);}
static int cost(PlantType p) {
    switch(p){case SUNFLOWER:return 50;case PEASHOOTER:return 100;case WALLNUT:return 50;
      case ICEPEA:return 175;case REPEATER:return 200;case CHERRY:return 150;
      case LILYPAD:return 25;case POTATO:return 25;default:return 0;}
}
static const char *plant_name(PlantType p){
    switch(p){case SUNFLOWER:return "GIRASSOL";case PEASHOOTER:return "ERVILHEIRA";
      case WALLNUT:return "NOZ";case ICEPEA:return "GELO";case REPEATER:return "REPETIDORA";
      case CHERRY:return "CEREJA";case LILYPAD:return "VITORIA-REGIA";case POTATO:return "BATATA";default:return "";}
}
static const char *zombie_name(ZombieType t){
    static const char *names[]={"NORMAL","CORREDOR","CONE","BALDE","JORNAL",
       "SALTADOR","PORTA","ATLETA","BASQUETE","MERGULHADOR","GOLFINHO","GIGANTE"};
    return names[(int)t];
}
static float zombie_hp(ZombieType t){
    static const float hp[]={125,90,250,520,175,190,460,620,370,210,210,1200};
    return hp[(int)t];
}
static float zombie_speed(ZombieType t){
    static const float speed[]={15,30,14,11,17,25,11,24,13,19,38,9};
    return speed[(int)t];
}
static void label(float x, float y, ALLEGRO_COLOR c, const char *s) {al_draw_text(font,c,x,y,0,s);}
static void center_label(float x, float y, ALLEGRO_COLOR c, const char *s) {al_draw_text(font,c,x,y,ALLEGRO_ALIGN_CENTER,s);}
static void rect(float x1,float y1,float x2,float y2,ALLEGRO_COLOR c){al_draw_filled_rectangle(x1,y1,x2,y2,c);}
static void circle(float x,float y,float rad,ALLEGRO_COLOR c){al_draw_filled_circle(x,y,rad,c);}
static void draw_button(float x,float y,float w,float h,const char *s,bool hover){
    rect(x,y,x+w,y+h,hover?rgb(81,154,81):rgb(45,111,68));
    al_draw_rectangle(x,y,x+w,y+h,rgb(175,229,135),2);
    center_label(x+w/2,y+(h-10)/2,rgb(255,255,235),s);
}
static bool in_rect(float x,float y,float a,float b,float w,float h){return x>=a&&x<=a+w&&y>=b&&y<=b+h;}
static void notify(const char *s) {snprintf(g.message,sizeof(g.message),"%s",s);g.message_timer=2.3f;}

static void reset_level(int lev) {
    g.level=lev;g.elapsed=0;g.spawn_timer=(lev==1?14.0f:(lev==2?12.0f:(lev==3?10.0f:17.0f)));
    g.sky_sun_timer=3.0f;g.spawned=0;g.kills=0;g.wave_total=lev==1?8:(lev==2?11:(lev==3?14:(14+(lev-4)*4)));
    g.selected=PEASHOOTER;g.shovel=false;g.hit_flash=0;g.wave_banner=4.0f;
    g.money=(lev==1?150:(lev==4?225:150));
    for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++)g.plants[r][c]=(Plant){0};
    for(int i=0;i<MAX_ZOMBIES;i++)g.zombies[i]=(Zombie){0};
    for(int i=0;i<MAX_SHOTS;i++)g.shots[i]=(Shot){0};
    for(int i=0;i<MAX_SUNS;i++)g.suns[i]=(Sun){0};
    for(int i=0;i<MAX_BALLS;i++)g.balls[i]=(BasketBall){0};
    g.state=PLAYING;lose_anim=0;
    notify(lev==4?"FASE 4: PISCINA! Use vitorias-regias na agua.":"Prepare sua defesa!");
}
static void start_game(void){g.score=0;reset_level(1);}
static void add_sun(float x,float y,int amt) {
    for(int i=0;i<MAX_SUNS;i++)if(!g.suns[i].active){g.suns[i]=(Sun){true,x,y,0,amt};return;}
}
static bool zombie_exists_ahead(int row,float x){
    for(int i=0;i<MAX_ZOMBIES;i++)if(g.zombies[i].active && g.zombies[i].row==row && g.zombies[i].x>x)return true;
    return false;
}
static void add_shot(int row,float x,bool ice){
    for(int i=0;i<MAX_SHOTS;i++)if(!g.shots[i].active){g.shots[i]=(Shot){true,row,x,row_y(row)-15,ice?230.0f:285.0f,ice?24.0f:30.0f,ice};return;}
}
static ZombieType choose_zombie(void){
    float n=rand01();
    if(g.level==1)return n<0.17f?RUNNER:NORMAL;
    if(g.level==2)return n<0.20f?CONE:(n<0.35f?NEWSPAPER:(n<0.52f?RUNNER:NORMAL));
    if(g.level==3)return n<0.16f?BUCKET:(n<0.32f?POLE:(n<0.46f?SCREEN:(n<0.64f?CONE:NORMAL)));
    if(g.level==4)return n<0.16f?BUCKET:(n<0.33f?SNORKEL:(n<0.50f?DOLPHIN:(n<0.68f?POLE:NORMAL)));
    if(g.level==5)return n<0.13f?BASKETBALL:(n<0.28f?FOOTBALL:(n<0.43f?BUCKET:(n<0.60f?SNORKEL:NORMAL)));
    return n<0.12f?GARGANTUAR:(n<0.28f?BASKETBALL:(n<0.43f?FOOTBALL:(n<0.59f?BUCKET:(n<0.76f?DOLPHIN:NORMAL))));
}
static void spawn_zombie(void){
    for(int i=0;i<MAX_ZOMBIES;i++)if(!g.zombies[i].active){
        ZombieType type=choose_zombie();int row=rand()%ROWS;
        if((type==SNORKEL||type==DOLPHIN) && is_pool())row=2+rand()%2;
        if(!is_pool()&&(type==SNORKEL||type==DOLPHIN))type=NORMAL;
        float hp=zombie_hp(type),speed=zombie_speed(type)*(1.0f+fminf((g.level-1)*0.045f,0.4f));
        g.zombies[i]=(Zombie){.active=true,.type=type,.row=row,.x=GX+COLS*CELL+20.0f,
           .hp=hp,.maxhp=hp,.speed=speed,.bite=0,.ability=0,.jumped=false};
        g.spawned++;if(groan_cd<=0){play_sound(snd_groan,0.25f);groan_cd=4.0f;}return;
    }
}
static int active_zombies(void){int n=0;for(int i=0;i<MAX_ZOMBIES;i++)if(g.zombies[i].active)n++;return n;}
static void explode_cherry(int row,int col){
    float cx=col_x(col),cy=row_y(row);
    for(int i=0;i<MAX_ZOMBIES;i++){
        Zombie *z=&g.zombies[i];if(!z->active)continue;
        if(fabsf(z->x-cx)<140 && abs(z->row-row)<=1){z->hp-=1100;
            if(z->hp<=0){z->active=false;g.kills++;g.score+=50;}}
    }
    play_sound(snd_hit,0.8f);notify("BOOM! Cereja explosiva!");
}
static void update_game(float dt){
    if(g.state!=PLAYING)return;
    g.elapsed+=dt;
    if(groan_cd>0)groan_cd-=dt;
    if(g.message_timer>0)g.message_timer-=dt;
    if(g.wave_banner>0)g.wave_banner-=dt;
    if(g.hit_flash>0)g.hit_flash-=dt;
    g.sky_sun_timer-=dt;
    if(g.sky_sun_timer<=0){add_sun(GX+30+rand01()*(COLS*CELL-60),GY+35+rand01()*(ROWS*CELL-75),25);
       g.sky_sun_timer=5.0f+rand01()*2.0f;}
    if(g.spawned<g.wave_total){
        g.spawn_timer-=dt;
        if(g.spawn_timer<=0){spawn_zombie();
            float base=(g.level==1?9.0f:(g.level==2?8.0f:(g.level==3?7.0f:6.5f)));
            g.spawn_timer=base+rand01()*2.0f;}
    }
    for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++){
        Plant *p=&g.plants[r][c];if(p->type==EMPTY)continue;
        p->timer+=dt;p->anim+=dt;
        if(p->type==SUNFLOWER&&p->timer>=6.5f){p->timer=0;add_sun(col_x(c)+rand01()*22-11,row_y(r)-15,25);}
        if((p->type==PEASHOOTER||p->type==ICEPEA||p->type==REPEATER)&&p->timer>=1.55f && zombie_exists_ahead(r,col_x(c))){
            p->timer=0;add_shot(r,col_x(c)+25,p->type==ICEPEA);
            if(p->type==REPEATER)add_shot(r,col_x(c)+2,false);
            play_sound(snd_shot,0.13f);
        }
        if(p->type==POTATO&&p->timer>4.0f){
            for(int j=0;j<MAX_ZOMBIES;j++){
                Zombie *z=&g.zombies[j];if(z->active&&z->row==r&&fabsf(z->x-col_x(c))<35){
                    z->hp-=600;g.plants[r][c]=(Plant){0};play_sound(snd_hit,0.75f);
                    if(z->hp<=0){z->active=false;g.kills++;g.score+=35;}break;
                }
            }
        }
    }
    for(int i=0;i<MAX_SHOTS;i++){
        Shot *s=&g.shots[i];if(!s->active)continue;s->x+=s->speed*dt;
        if(s->x>SW+10){s->active=false;continue;}
        for(int j=0;j<MAX_ZOMBIES;j++){
            Zombie *z=&g.zombies[j];if(!z->active||z->row!=s->row)continue;
            if(fabsf(s->x-z->x)<22){
                z->hp-=s->damage;if(s->ice)z->bite=-2.2f;play_sound(snd_hit,0.15f);
                s->active=false;
                if(z->hp<=0){z->active=false;g.kills++;g.score+=z->type==GARGANTUAR?150:(z->type==BUCKET?50:20);}
                break;
            }
        }
    }
    for(int i=0;i<MAX_ZOMBIES;i++){
        Zombie *z=&g.zombies[i];if(!z->active)continue;
        if(z->x<GX-28){g.state=LOST;lose_anim=0;play_sound(snd_lose,1.0f);
           notify("Os zumbis invadiram sua casa!");return;}
        int c=(int)((z->x-GX)/CELL);bool eating=false;
        if(c>=0&&c<COLS){
            Plant *p=&g.plants[z->row][c];
            if(p->type!=EMPTY && fabsf(z->x-col_x(c))<37){
                if((z->type==POLE||z->type==DOLPHIN) && !z->jumped && c>0){
                    z->x-=CELL*0.92f;z->jumped=true;play_sound(snd_splash,0.35f);
                }else{
                    eating=true;float damage=(z->type==FOOTBALL?56.0f:(z->type==GARGANTUAR?105.0f:31.0f))*dt;
                    p->hp-=damage;if(p->hp<=0){bool pad=p->pad;*p=(Plant){0};if(pad)p->type=LILYPAD,p->hp=110;}
                }
            }
        }
        if(z->type==BASKETBALL && c>=0 && c<COLS && z->ability<=0){
            for(int cc=c-1;cc>=0;cc--)if(g.plants[z->row][cc].type!=EMPTY){
                for(int b=0;b<MAX_BALLS;b++)if(!g.balls[b].active){
                    g.balls[b]=(BasketBall){true,z->row,z->x,row_y(z->row)-42,-175};break;
                }z->ability=4.0f;break;
            }
        }
        if(!eating){float multiplier=z->bite<0?0.45f:1.0f;z->x-=z->speed*multiplier*dt;}
        if(z->bite<0)z->bite+=dt;
        if(z->ability>0)z->ability-=dt;
    }
    for(int b=0;b<MAX_BALLS;b++)if(g.balls[b].active){
        BasketBall *a=&g.balls[b];a->x+=a->vx*dt;
        if(a->x<GX-10){a->active=false;continue;}
        int c=(int)((a->x-GX)/CELL);
        if(c>=0&&c<COLS){Plant *p=&g.plants[a->row][c];
            if(p->type!=EMPTY && fabsf(a->x-col_x(c))<21){p->hp-=85;a->active=false;
                if(p->hp<=0){bool pad=p->pad;*p=(Plant){0};if(pad)p->type=LILYPAD,p->hp=110;}}
        }
    }
    for(int i=0;i<MAX_SUNS;i++)if(g.suns[i].active){g.suns[i].age+=dt;if(g.suns[i].age>11.0f)g.suns[i].active=false;}
    if(g.spawned>=g.wave_total&&active_zombies()==0){
        if(g.level>=6){g.state=WON;notify("Voce salvou o jardim e a piscina!");}
        else{int next=g.level+1;g.score+=100*next;reset_level(next);
            notify(next==4?"Piscina liberada! Coloque vitorias-regias na agua.":"Nova fase! Zumbis mais fortes se aproximam.");}
    }
}
static void draw_leaf(float x,float y,float w,float h,ALLEGRO_COLOR c){
    al_draw_filled_ellipse(x,y,w,h,c);
    al_draw_line(x-w*0.75f,y+h*0.6f,x+w*0.75f,y-h*0.6f,rgb(33,108,41),1.5f);
}
static void plant_icon(PlantType type,float x,float y,float scale){
    ALLEGRO_COLOR green=rgb(37,142,52),dark=rgb(28,89,40),brown=rgb(121,68,36);
    if(type!=WALLNUT && type!=LILYPAD && type!=CHERRY && type!=POTATO){
        al_draw_line(x,y+5*scale,x,y+26*scale,dark,6*scale);
        draw_leaf(x-13*scale,y+18*scale,16*scale,7*scale,rgb(39,159,62));
        draw_leaf(x+14*scale,y+22*scale,15*scale,7*scale,rgb(65,184,65));
    }
    if(type==LILYPAD){
        al_draw_filled_ellipse(x,y+11*scale,32*scale,17*scale,rgb(38,116,71));
        al_draw_filled_ellipse(x,y+8*scale,29*scale,13*scale,rgb(76,190,83));
        al_draw_filled_triangle(x,y+10*scale,x+31*scale,y-5*scale,x+17*scale,y+17*scale,rgb(22,93,58));
        circle(x-9*scale,y+6*scale,3*scale,rgb(27,74,46));circle(x+8*scale,y+5*scale,3*scale,rgb(27,74,46));
    }else if(type==CHERRY){
        al_draw_line(x-9*scale,y-15*scale,x+6*scale,y-34*scale,rgb(66,120,35),4*scale);
        al_draw_line(x+10*scale,y-12*scale,x+6*scale,y-34*scale,rgb(66,120,35),4*scale);
        circle(x-12*scale,y+2*scale,17*scale,rgb(205,39,50));
        circle(x+13*scale,y+7*scale,17*scale,rgb(230,54,55));
        circle(x-17*scale,y-5*scale,4*scale,rgb(255,151,132));
        circle(x+7*scale,y,4*scale,rgb(255,151,132));
    }else if(type==POTATO){
        al_draw_filled_ellipse(x,y+10*scale,23*scale,14*scale,rgb(156,104,55));
        circle(x-9*scale,y+8*scale,3*scale,rgb(46,33,19));
        circle(x+6*scale,y+8*scale,3*scale,rgb(46,33,19));
    }else if(type==SUNFLOWER){
        for(int k=0;k<12;k++){
            float a=k*(float)(PI/6);
            al_draw_filled_ellipse(x+16*scale*cosf(a),y-8*scale+16*scale*sinf(a),8*scale,11*scale,rgb(255,184+(k%3)*12,29));
        }
        circle(x,y-8*scale,17*scale,rgb(105,60,33));
        circle(x-6*scale,y-13*scale,3*scale,rgb(255,243,191));
        circle(x+6*scale,y-13*scale,3*scale,rgb(255,243,191));
        al_draw_arc(x,y-6*scale,7*scale,0.15f,2.8f,rgb(246,210,150),2*scale);
        circle(x-13*scale,y-1*scale,4*scale,rgb(207,104,59));
        circle(x+13*scale,y-1*scale,4*scale,rgb(207,104,59));
    }else if(type==WALLNUT){
        al_draw_filled_ellipse(x+3*scale,y+10*scale,25*scale,29*scale,rgb(111,60,33));
        al_draw_filled_ellipse(x,y+6*scale,24*scale,29*scale,rgb(179,110,59));
        al_draw_filled_ellipse(x-8*scale,y-6*scale,9*scale,11*scale,rgb(201,136,72));
        al_draw_arc(x+2*scale,y+12*scale,12*scale,0.0f,2.6f,rgb(122,66,36),2*scale);
        circle(x-9*scale,y+4*scale,3*scale,rgb(38,32,26));
        circle(x+9*scale,y+4*scale,3*scale,rgb(38,32,26));
        al_draw_line(x-3*scale,y+17*scale,x+5*scale,y+17*scale,rgb(69,44,31),2*scale);
    }else if(type==PEASHOOTER||type==ICEPEA||type==REPEATER){
        ALLEGRO_COLOR head=type==ICEPEA?rgb(112,226,241):rgb(89,213,77);
        ALLEGRO_COLOR light=type==ICEPEA?rgb(180,250,250):rgb(149,242,94);
        circle(x-6*scale,y-10*scale,23*scale,dark);
        circle(x-8*scale,y-12*scale,21*scale,head);
        al_draw_filled_ellipse(x+16*scale,y-10*scale,18*scale,11*scale,head);
        al_draw_filled_ellipse(x+30*scale,y-10*scale,9*scale,10*scale,dark);
        al_draw_filled_ellipse(x+31*scale,y-10*scale,6*scale,7*scale,rgb(34,77,45));
        al_draw_filled_ellipse(x-13*scale,y-20*scale,9*scale,5*scale,light);
        circle(x-14*scale,y-16*scale,4*scale,rgb(25,58,34));
        circle(x-15*scale,y-17*scale,1.5f*scale,rgb(252,255,241));
        if(type==REPEATER){al_draw_filled_ellipse(x+12*scale,y+4*scale,13*scale,8*scale,head);}
        if(type==ICEPEA){
            for(int i=0;i<4;i++){
                float a=i*(float)(PI/2);float px=x-9*scale+21*scale*cosf(a),py=y-11*scale+21*scale*sinf(a);
                al_draw_line(px-3*scale,py,px+3*scale,py,rgb(210,249,255),2*scale);
            }
        }
    }
}
static void draw_zombie(Zombie *z){
    float x=z->x,y=row_y(z->row),sc=z->type==GARGANTUAR?1.23f:0.85f;
    ALLEGRO_COLOR skin=z->bite<0?rgb(100,185,199):rgb(157,181,131);
    if(water_row(z->row) && (z->type==SNORKEL || z->type==DOLPHIN)){
        al_draw_filled_ellipse(x,y+15,27,9,rgb(60,161,190));
    }
    al_draw_filled_ellipse(x,y+29*sc,25*sc,5*sc,al_map_rgba(30,44,26,110));
    al_draw_line(x-9*sc,y+6*sc,x-13*sc,y+25*sc,rgb(62,56,65),8*sc);
    al_draw_line(x+7*sc,y+6*sc,x+12*sc,y+25*sc,rgb(62,56,65),8*sc);
    ALLEGRO_COLOR coat=z->type==FOOTBALL?rgb(175,45,42):(z->type==BASKETBALL?rgb(97,70,176):rgb(104,78,90));
    al_draw_filled_rounded_rectangle(x-17*sc,y-25*sc,x+16*sc,y+11*sc,5,5,coat);
    al_draw_filled_triangle(x-5*sc,y-22*sc,x+7*sc,y-22*sc,x,y+5*sc,rgb(216,208,170));
    al_draw_line(x-14*sc,y-15*sc,x-28*sc,y-3*sc,skin,8*sc);
    al_draw_line(x+12*sc,y-16*sc,x+27*sc,y-9*sc,skin,8*sc);
    al_draw_filled_ellipse(x+3*sc,y-40*sc,20*sc,22*sc,skin);
    circle(x-15*sc,y-40*sc,5*sc,skin);
    al_draw_filled_ellipse(x-5*sc,y-44*sc,7*sc,8*sc,rgb(237,236,209));
    al_draw_filled_ellipse(x+11*sc,y-43*sc,7*sc,8*sc,rgb(237,236,209));
    circle(x-4*sc,y-43*sc,2.8f*sc,rgb(43,35,25));circle(x+12*sc,y-42*sc,2.8f*sc,rgb(43,35,25));
    al_draw_filled_ellipse(x+8*sc,y-29*sc,10*sc,5*sc,rgb(55,55,48));
    for(int t=0;t<3;t++)rect(x+(1+t*6)*sc,y-32*sc,x+(5+t*6)*sc,y-29*sc,rgb(239,227,175));
    if(z->type==CONE){
        al_draw_filled_triangle(x-21*sc,y-58*sc,x+22*sc,y-58*sc,x+2*sc,y-101*sc,rgb(241,132,39));
        rect(x-17*sc,y-67*sc,x+17*sc,y-61*sc,rgb(250,231,182));
    }else if(z->type==BUCKET){
        rect(x-22*sc,y-88*sc,x+25*sc,y-58*sc,rgb(135,151,163));
        rect(x-25*sc,y-91*sc,x+27*sc,y-83*sc,rgb(182,191,194));
        al_draw_line(x-20*sc,y-60*sc,x+21*sc,y-60*sc,rgb(88,109,119),2);
    }else if(z->type==NEWSPAPER){
        rect(x-34*sc,y-20*sc,x+19*sc,y+13*sc,rgb(224,225,206));
        for(int i=0;i<3;i++)al_draw_line(x-28*sc,y+(-10+i*7)*sc,x+15*sc,y+(-10+i*7)*sc,rgb(95,97,94),1);
    }else if(z->type==POLE){
        al_draw_line(x+25*sc,y-65*sc,x+25*sc,y+31*sc,rgb(185,176,123),4);
    }else if(z->type==SCREEN){
        rect(x-35*sc,y-27*sc,x+23*sc,y+16*sc,rgb(141,161,167));
        for(int i=0;i<6;i++)al_draw_line(x+(-33+i*10)*sc,y-25*sc,x+(-33+i*10)*sc,y+14*sc,rgb(66,82,80),1);
    }else if(z->type==FOOTBALL){
        al_draw_filled_ellipse(x+2*sc,y-62*sc,25*sc,17*sc,rgb(200,61,50));
        rect(x-22*sc,y-62*sc,x+24*sc,y-57*sc,rgb(247,245,222));
    }else if(z->type==BASKETBALL){
        circle(x+29*sc,y-4*sc,9*sc,rgb(221,121,42));
        al_draw_arc(x+29*sc,y-4*sc,9*sc,0,6.28f,rgb(75,57,39),2);
        rect(x-18*sc,y-66*sc,x+22*sc,y-60*sc,rgb(130,93,188));
    }else if(z->type==SNORKEL){
        al_draw_line(x+6*sc,y-59*sc,x+26*sc,y-59*sc,rgb(70,80,138),5);
        al_draw_line(x+25*sc,y-59*sc,x+25*sc,y-88*sc,rgb(70,80,138),5);
        rect(x-18*sc,y-47*sc,x+21*sc,y-39*sc,rgb(71,92,150));
    }else if(z->type==DOLPHIN){
        al_draw_filled_ellipse(x,y+18*sc,31*sc,9*sc,rgb(111,172,189));
        al_draw_filled_triangle(x+2*sc,y+12*sc,x+10*sc,y-9*sc,x+21*sc,y+15*sc,rgb(111,172,189));
    }else if(z->type==GARGANTUAR){
        al_draw_line(x+26*sc,y-48*sc,x+38*sc,y+25*sc,rgb(106,86,66),11);
        rect(x+23*sc,y-56*sc,x+44*sc,y-47*sc,rgb(145,128,103));
    }else if(z->type==RUNNER){
        al_draw_filled_triangle(x-21*sc,y-64*sc,x+24*sc,y-64*sc,x+25*sc,y-54*sc,rgb(238,87,65));
    }
    rect(x-23*sc,y-95*sc,x+23*sc,y-90*sc,rgb(48,48,48));
    rect(x-23*sc,y-95*sc,x+(-23+46*clampf(z->hp/z->maxhp,0,1))*sc,y-90*sc,rgb(239,100,68));
}
static void draw_game(void){
    bool pool=is_pool();
    rect(0,0,SW,SH,rgb(35,63,54));
    rect(0,0,SW,104,rgb(32,63,51));
    rect(0,0,SW,14,rgb(68,126,69));
    char buf[160];
    label(15,22,rgb(255,230,116),"JARDIM vs ZUMBIS - PROJETO C / ALLEGRO 5");
    snprintf(buf,sizeof(buf),"SOIS: %d     FASE: %d/6     PONTOS: %d",g.money,g.level,g.score);
    label(16,61,rgb(253,246,205),buf);
    snprintf(buf,sizeof(buf),"ZUMBIS: %d/%d   RESTANTES: %d",g.spawned,g.wave_total,active_zombies());
    label(735,61,rgb(250,240,202),buf);
    if(g.spawned==0 && g.spawn_timer>0){
        snprintf(buf,sizeof(buf),"PRIMEIRO ZUMBI EM %d s",(int)ceilf(g.spawn_timer));
        center_label(560,83,rgb(255,232,143),buf);
    }
    rect(0,104,GX-12,SH,rgb(184,166,112));
    rect(4,107,GX-15,SH-20,rgb(214,189,134));
    // Fachada e cerca no quintal
    rect(GX-37,GY-8,GX-8,GY+ROWS*CELL+3,rgb(211,194,156));
    for(int i=0;i<ROWS;i++){
       int y=(int)row_y(i);rect(GX-35,y-22,GX-16,y+20,rgb(162,107,75));
       rect(GX-32,y-17,GX-19,y+15,rgb(207,172,112));
    }
    // Gramado/piscina com textura
    for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++){
        int x=GX+c*CELL,y=GY+r*CELL;
        if(water_row(r)){
            ALLEGRO_COLOR water=(r+c)%2?rgb(54,156,197):rgb(64,174,215);
            rect(x,y,x+CELL,y+CELL,water);
            for(int k=0;k<3;k++)al_draw_arc(x+13+k*22,y+23+k*15,9,0.4f,2.1f,rgb(155,230,235),1);
        }else{
            rect(x,y,x+CELL,y+CELL,(r+c)%2?rgb(93,155,67):rgb(119,185,82));
            for(int j=0;j<3;j++){
                al_draw_line(x+15+j*20,y+CELL-17,x+18+j*20,y+CELL-26,rgb(71,140,62),1);
                al_draw_line(x+18+j*20,y+CELL-26,x+22+j*20,y+CELL-19,rgb(71,140,62),1);
            }
        }
        al_draw_rectangle(x,y,x+CELL,y+CELL,al_map_rgba(55,105,48,70),1);
    }
    if(pool){
        rect(GX,GY+2*CELL-5,GX+COLS*CELL,GY+2*CELL+1,rgb(222,225,201));
        rect(GX,GY+4*CELL-1,GX+COLS*CELL,GY+4*CELL+5,rgb(222,225,201));
        center_label(GX+COLS*CELL/2,GY+3*CELL-8,al_map_rgba(235,255,255,150),"PISCINA");
    }
    rect(GX-7,GY-7,GX+COLS*CELL+7,GY,rgb(105,80,55));
    rect(GX-7,GY+ROWS*CELL,GX+COLS*CELL+7,GY+ROWS*CELL+9,rgb(105,80,55));
    // Cortadores de grama (decoracao de defesa)
    for(int r=0;r<ROWS;r++){
        float yy=row_y(r)+18;
        rect(GX-24,yy-17,GX-12,yy+3,rgb(211,58,48));
        circle(GX-24,yy+5,5,rgb(42,43,43));circle(GX-11,yy+5,5,rgb(42,43,43));
        al_draw_line(GX-12,yy-11,GX-4,yy-22,rgb(70,65,62),2);
    }
    // Cartas compactas
    for(int i=0;i<8;i++){
        PlantType type=(PlantType)(i+1);float y=119+i*62;
        bool selected=g.selected==type&&!g.shovel;
        rect(13,y,210,y+56,selected?rgb(245,227,131):rgb(226,211,170));
        al_draw_rectangle(13,y,210,y+56,selected?rgb(46,113,49):rgb(123,100,66),selected?3:1);
        plant_icon(type,45,y+25,0.47f);
        label(78,y+8,rgb(49,55,39),plant_name(type));
        snprintf(buf,sizeof(buf),"%d SOIS",cost(type));label(78,y+31,g.money>=cost(type)?rgb(40,116,55):rgb(161,59,42),buf);
    }
    // Pa
    rect(17,626,209,670,g.shovel?rgb(246,207,119):rgb(98,115,86));
    al_draw_rectangle(17,626,209,670,rgb(248,228,159),2);
    al_draw_line(38,638,38,659,rgb(139,79,43),5);
    al_draw_filled_ellipse(38,634,10,6,rgb(171,189,189));
    label(60,645,rgb(255,251,218),g.shovel?"PA SELECIONADA":"PA: REMOVER");
    for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++){
        Plant *p=&g.plants[r][c];if(p->type==EMPTY)continue;
        if(p->pad){plant_icon(LILYPAD,col_x(c),row_y(r)+18,0.85f);}
        plant_icon(p->type,col_x(c),row_y(r)-4,p->type==LILYPAD?0.78f:0.82f);
        if(p->type==WALLNUT){
            rect(col_x(c)-24,row_y(r)+26,col_x(c)+24,row_y(r)+30,rgb(46,87,38));
            rect(col_x(c)-24,row_y(r)+26,col_x(c)-24+48*clampf(p->hp/600,0,1),row_y(r)+30,rgb(238,191,90));
        }
    }
    for(int i=0;i<MAX_SHOTS;i++)if(g.shots[i].active){
        Shot *a=&g.shots[i];circle(a->x,a->y,a->ice?8:6,a->ice?rgb(132,239,249):rgb(44,157,62));
        circle(a->x-2,a->y-2,2,rgb(237,255,220));
    }
    for(int i=0;i<MAX_BALLS;i++)if(g.balls[i].active){
        BasketBall *b=&g.balls[i];circle(b->x,b->y,10,rgb(218,124,43));
        al_draw_line(b->x-9,b->y,b->x+9,b->y,rgb(90,59,38),2);
        al_draw_line(b->x,b->y-9,b->x,b->y+9,rgb(90,59,38),2);
    }
    for(int i=0;i<MAX_ZOMBIES;i++)if(g.zombies[i].active)draw_zombie(&g.zombies[i]);
    for(int i=0;i<MAX_SUNS;i++)if(g.suns[i].active){
        Sun *a=&g.suns[i];circle(a->x,a->y,19,rgb(255,212,63));circle(a->x,a->y,12,rgb(255,245,155));
        for(int j=0;j<8;j++){float an=j*(float)(PI/4);
            al_draw_line(a->x+21*cosf(an),a->y+21*sinf(an),a->x+27*cosf(an),a->y+27*sinf(an),rgb(255,235,98),2);}
    }
    rect(235,620,SW,SH,rgb(35,70,47));
    label(247,641,rgb(255,242,205),"CLIQUE CARTA -> TERRENO | COLETE SOIS | PA PARA REMOVER");
    label(247,674,rgb(236,248,207),"ESPACO: PAUSA   R: REINICIAR   M: MENU   ESC: MENU");
    if(g.message_timer>0)center_label(694,111,rgb(255,251,215),g.message);
    if(g.wave_banner>0){
        center_label(690,118,rgb(255,241,133),pool?"DEFENDA A PISCINA!":"DEFENDA O JARDIM!");
    }
    if(g.state==PAUSED){
        rect(315,244,876,443,rgb(30,55,52));al_draw_rectangle(315,244,876,443,rgb(246,229,145),3);
        center_label(595,298,rgb(255,246,171),"JOGO PAUSADO");
        center_label(595,359,rgb(242,248,220),"Pressione ESPACO para continuar");
    }
}
static void draw_menu(void){
    rect(0,0,SW,SH,rgb(24,50,47));
    for(int r=0;r<ROWS;r++)for(int c=0;c<COLS;c++){int x=180+c*85,y=210+r*85;rect(x,y,x+85,y+85,(r+c)%2?rgb(61,106,64):rgb(76,124,68));}
    rect(0,0,SW,195,rgb(30,60,48));
    center_label(SW/2,62,rgb(255,220,85),"PLANTS vs ZOMBIES");
    center_label(SW/2,95,rgb(237,249,215),"Defenda seu jardim! - C + Allegro 5");
    draw_button(410,255,280,76,"JOGAR",in_rect(mouse_x,mouse_y,410,255,280,76));
    draw_button(410,355,280,76,"COMO JOGAR",in_rect(mouse_x,mouse_y,410,355,280,76));
    draw_button(410,455,280,76,"SAIR",in_rect(mouse_x,mouse_y,410,455,280,76));
    center_label(SW/2,603,rgb(230,239,205),"6 fases / 8 plantas / 12 zumbis / piscina na fase 4 / sons proprios");
}
static void draw_help(void){
    rect(0,0,SW,SH,rgb(28,57,48));
    center_label(SW/2,68,rgb(255,224,108),"COMO JOGAR");
    label(170,158,rgb(242,242,211),"1. Clique em uma carta de planta na lateral esquerda.");
    label(170,207,rgb(242,242,211),"2. Clique em um quadrado vazio para plantar.");
    label(170,256,rgb(242,242,211),"3. Clique nos sois amarelos para coletar recursos.");
    label(170,305,rgb(242,242,211),"4. Use a PA para remover plantas; nao devolve os sois.");
    label(170,368,rgb(245,209,111),"Girassol: gera sois | Ervilheira: atira | Noz: bloqueia");
    label(170,403,rgb(245,209,111),"Gelo: desacelera | Repetidora: 2 tiros | Cereja: explode");
    label(170,454,rgb(242,242,211),"Vitoria-regia: plantar na piscina | Batata: mina | 6 fases");
    draw_button(400,536,300,65,"VOLTAR AO MENU",in_rect(mouse_x,mouse_y,400,536,300,65));
}
static void draw_scared_person(float x,float y,float scale){
    // Personagem original, inspirado em desenhos animados de defesa de jardim.
    ALLEGRO_COLOR face=rgb(244,185,133),hair=rgb(94,62,39),shirt=rgb(92,145,213);
    al_draw_filled_ellipse(x,y+72*scale,54*scale,63*scale,shirt);
    al_draw_filled_ellipse(x,y,52*scale,62*scale,face);
    al_draw_filled_ellipse(x,y-56*scale,53*scale,22*scale,hair);
    for(int i=0;i<4;i++){
        float xx=x+(-31+i*21)*scale;
        al_draw_filled_triangle(xx,y-63*scale,xx+16*scale,y-79*scale,xx+27*scale,y-56*scale,hair);
    }
    al_draw_filled_ellipse(x-20*scale,y-10*scale,13*scale,18*scale,rgb(255,255,246));
    al_draw_filled_ellipse(x+20*scale,y-10*scale,13*scale,18*scale,rgb(255,255,246));
    circle(x-18*scale,y-9*scale,5*scale,rgb(39,44,55));
    circle(x+18*scale,y-9*scale,5*scale,rgb(39,44,55));
    al_draw_line(x-37*scale,y-34*scale,x-9*scale,y-39*scale,hair,5*scale);
    al_draw_line(x+10*scale,y-39*scale,x+37*scale,y-34*scale,hair,5*scale);
    // Boca aberta gritando
    al_draw_filled_ellipse(x,y+34*scale,20*scale,26*scale,rgb(72,25,39));
    al_draw_filled_ellipse(x,y+49*scale,12*scale,7*scale,rgb(217,89,100));
    // Bracos erguidos
    al_draw_line(x-49*scale,y+65*scale,x-86*scale,y+1*scale,face,17*scale);
    al_draw_line(x+49*scale,y+65*scale,x+86*scale,y+1*scale,face,17*scale);
    circle(x-87*scale,y-1*scale,13*scale,face);
    circle(x+87*scale,y-1*scale,13*scale,face);
}
static void draw_end(void){
    draw_game();
    rect(0,0,SW,SH,al_map_rgba(18,26,24,214));
    if(g.state==LOST){
        // Tela especial de derrota com grito e personagem
        rect(127,90,973,620,rgb(48,58,44));
        al_draw_rectangle(127,90,973,620,rgb(247,208,112),5);
        for(int i=0;i<10;i++){
            float yy=114+i*46;
            al_draw_line(133,yy,968,yy,rgb(56+i*2,72+i*2,49),1);
        }
        float bob=sinf(lose_anim*9.0f)*3.0f;
        draw_scared_person(356,330+bob,1.5f);
        center_label(722,169,rgb(255,218,89),"OS ZUMBIS ENTRARAM!");
        center_label(728,249,rgb(255,117,91),"NAAAAAAAAAO!");
        char buf[96];snprintf(buf,sizeof(buf),"PONTOS: %d     FASE: %d",g.score,g.level);
        center_label(728,324,rgb(255,247,220),buf);
        draw_button(554,423,172,64,"REINICIAR",in_rect(mouse_x,mouse_y,554,423,172,64));
        draw_button(740,423,172,64,"MENU",in_rect(mouse_x,mouse_y,740,423,172,64));
        center_label(731,523,rgb(255,227,169),"PROTEJA SUA CASA NA PROXIMA!");
    }else{
        rect(240,204,860,515,rgb(23,46,47));al_draw_rectangle(240,204,860,515,rgb(251,226,111),3);
        center_label(550,250,rgb(255,229,120),"VOCE VENCEU!");
        char buf[100];snprintf(buf,sizeof(buf),"Pontuacao final: %d",g.score);center_label(550,306,rgb(250,247,224),buf);
        draw_button(336,400,200,60,"REINICIAR",in_rect(mouse_x,mouse_y,336,400,200,60));
        draw_button(560,400,200,60,"MENU",in_rect(mouse_x,mouse_y,560,400,200,60));
    }
}
static void handle_click(float x,float y){
    if(g.state==MENU){
        if(in_rect(x,y,410,255,280,76))start_game();
        else if(in_rect(x,y,410,355,280,76))g.state=HELP;
        else if(in_rect(x,y,410,455,280,76))running=false;return;
    }
    if(g.state==HELP){if(in_rect(x,y,400,536,300,65))g.state=MENU;return;}
    if(g.state==WON||g.state==LOST){
        if(g.state==LOST){if(in_rect(x,y,554,423,172,64))start_game();else if(in_rect(x,y,740,423,172,64))g.state=MENU;}
        else {if(in_rect(x,y,336,400,200,60))start_game();else if(in_rect(x,y,560,400,200,60))g.state=MENU;}
        return;
    }
    if(g.state!=PLAYING)return;
    for(int i=0;i<MAX_SUNS;i++)if(g.suns[i].active&&dist2(x,y,g.suns[i].x,g.suns[i].y)<33*33){
        g.money+=g.suns[i].amount;g.suns[i].active=false;play_sound(snd_sun,0.45f);return;
    }
    if(in_rect(x,y,17,626,192,44)){g.shovel=!g.shovel;
        notify(g.shovel?"Pa selecionada: clique em uma planta para remover.":"Pa guardada.");return;}
    for(int i=0;i<8;i++)if(in_rect(x,y,13,119+i*62,197,56)){
        g.selected=(PlantType)(i+1);g.shovel=false;return;
    }
    if(x>=GX&&x<GX+COLS*CELL&&y>=GY&&y<GY+ROWS*CELL){
        int c=(int)((x-GX)/CELL),r=(int)((y-GY)/CELL);
        Plant *p=&g.plants[r][c];
        if(g.shovel){
            if(p->type!=EMPTY){*p=(Plant){0};play_sound(snd_shovel,0.5f);notify("Planta removida.");}
            g.shovel=false;return;
        }
        if(g.selected==LILYPAD && !water_row(r)){notify("Vitoria-regia so pode ser plantada na agua!");return;}
        if(water_row(r)&&g.selected!=LILYPAD&&p->type!=LILYPAD){
            notify("Plante primeiro uma VITORIA-REGIA nesta casa.");return;
        }
        if(p->type!=EMPTY && !(p->type==LILYPAD && g.selected!=LILYPAD)){
            notify("Este espaco ja esta ocupado.");return;
        }
        int price=cost(g.selected);if(g.money<price){notify("Sois insuficientes! Plante girassois.");return;}
        g.money-=price;
        if(g.selected==CHERRY){explode_cherry(r,c);return;}
        bool pad=water_row(r)&&p->type==LILYPAD&&g.selected!=LILYPAD;
        float hp=g.selected==WALLNUT?600:(g.selected==LILYPAD?145:120);
        *p=(Plant){.type=g.selected,.hp=hp,.timer=0,.anim=0,.pad=pad};
        play_sound(snd_plant,0.55f);
    }
}
int main(void){
    srand((unsigned)time(NULL));
    if(!al_init()){fprintf(stderr,"Erro: al_init falhou.\n");return 1;}
    if(!al_init_primitives_addon()){fprintf(stderr,"Erro: addon primitives.\n");return 1;}
    al_init_font_addon();
    if(!al_install_mouse()||!al_install_keyboard()){fprintf(stderr,"Erro: mouse ou teclado.\n");return 1;}
    display=al_create_display(SW,SH);if(!display){fprintf(stderr,"Erro ao criar janela.\n");return 1;}
    al_set_window_title(display,"Jardim vs Zumbis - Versao Expandida em C");
    font=al_create_builtin_font();timer=al_create_timer(1.0/FPS);queue=al_create_event_queue();
    load_sounds();
    if(!font||!timer||!queue){fprintf(stderr,"Erro ao criar recursos Allegro.\n");return 1;}
    al_register_event_source(queue,al_get_display_event_source(display));
    al_register_event_source(queue,al_get_timer_event_source(timer));
    al_register_event_source(queue,al_get_mouse_event_source());
    al_register_event_source(queue,al_get_keyboard_event_source());
    g.state=MENU;g.selected=PEASHOOTER;
    bool redraw=true;al_start_timer(timer);
    while(running){
        ALLEGRO_EVENT e;al_wait_for_event(queue,&e);
        if(e.type==ALLEGRO_EVENT_DISPLAY_CLOSE)running=false;
        else if(e.type==ALLEGRO_EVENT_TIMER){update_game(1.0f/(float)FPS);if(g.state==LOST)lose_anim+=1.0f/(float)FPS;redraw=true;}
        else if(e.type==ALLEGRO_EVENT_MOUSE_AXES){mouse_x=e.mouse.x;mouse_y=e.mouse.y;}
        else if(e.type==ALLEGRO_EVENT_MOUSE_BUTTON_DOWN && e.mouse.button==1){handle_click(e.mouse.x,e.mouse.y);redraw=true;}
        else if(e.type==ALLEGRO_EVENT_KEY_DOWN){
            if(e.keyboard.keycode==ALLEGRO_KEY_ESCAPE){if(g.state==MENU)running=false;else g.state=MENU;}
            if(e.keyboard.keycode==ALLEGRO_KEY_SPACE){if(g.state==PLAYING)g.state=PAUSED;else if(g.state==PAUSED)g.state=PLAYING;}
            if(e.keyboard.keycode==ALLEGRO_KEY_R&&(g.state==PLAYING||g.state==PAUSED||g.state==LOST||g.state==WON))start_game();
            if(e.keyboard.keycode==ALLEGRO_KEY_M)g.state=MENU;
            if(e.keyboard.keycode==ALLEGRO_KEY_ENTER&&g.state==MENU)start_game();
            redraw=true;
        }
        if(redraw && al_is_event_queue_empty(queue)){
            redraw=false;
            if(g.state==MENU)draw_menu();else if(g.state==HELP)draw_help();else if(g.state==WON||g.state==LOST)draw_end();else draw_game();
            al_flip_display();
        }
    }
    free_sounds();
    al_destroy_event_queue(queue);al_destroy_timer(timer);al_destroy_font(font);al_destroy_display(display);
    al_shutdown_font_addon();al_shutdown_primitives_addon();return 0;
}
