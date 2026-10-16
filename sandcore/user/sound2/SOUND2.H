#ifndef SANDCORE_SOUND2_PRIVATE_H
#define SANDCORE_SOUND2_PRIVATE_H
#include "../systemtools/TOOLS.H"
#include "SCUICANVAS.H"
#include "SOUNDWIRE.H"

enum {S2_OPEN=0x6900,S2_PLAY,S2_STOP,S2_REPLAY,S2_BACK,S2_FORWARD,
    S2_SEEK,S2_VOLUME,S2_MINI,S2_FULL,S2_CANCEL,S2_COPY,
    S2_EMPTY=0,S2_CONNECTING=1,S2_HELLO=2,S2_OPENING=3,S2_READY=4,
    S2_PLAYING=5,S2_STOPPING=6,S2_CLOSING=7};
typedef struct {
    ScJob job;ScIpcEndpoint endpoint;ScIpcChannel channel;ScBrokerIdentity peer;
    ScU32 phase,sequence,inflight,expected_opcode,cancel,deadline,seconds,revision,skip_metadata;
    ScU32 want_pause,want_volume,quit_sent,open_sent,fallback;ScStatus failure;
    char endpoint_name[96],path[68];SndMessage state;
    unsigned char *cover;ScU32 cover_bytes,cover_at;
} SndSlot;
typedef struct {
    SndSlot *current,*candidate,*retiring;
    ScIdentityInfo identity;ScProcessRef self;ScU32 nonce,quit,closing;
    ScU32 volume,paused,mini,full,setting_value,dialog_paused,dialog_previous_pause;
    ScUiNode title,artist,details,progress,seek,volume_control,cover_panel,stats_panel,peaks;
    ScUiDialog *dialog;ScGfxCanvas *canvas;ScGfxSurface *cover;
    ScGfxOperation *image;unsigned char *image_bytes;ScU32 image_wait,image_more,image_cancel,image_done;
    ScUiClass peak_class;ScWindowInfo restore;
    char status[320],display_path[68],track_title[128],track_artist[128];ScU32 last_position,last_total,last_flags;
} SoundApp;
static SoundApp sound_app;
static ScStatus sound_begin(const char *,ScU32,ScU32);
static ScStatus sound_command(ScU32);
static ScStatus sound_event(ScUiContext *,const ScUiEvent *,void *);
static void sound_changed(void);
static void sound_image_cancel(void);
static ScStatus sound_cover_step(void);
static ScStatus sound_ui(void);
static ScStatus sound_mode(ScU32);
static void sound_slot_stop(SndSlot *);
static int sound_jobs_step(void);
#endif
