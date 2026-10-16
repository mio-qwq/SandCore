#ifndef SANDCORE_PALETTE2_PRIVATE_H
#define SANDCORE_PALETTE2_PRIVATE_H
#include "TOOLS.H"
#include "SC32WALL.H"
enum {PAL_RAW=0x6400,PAL_THEME,PAL_COPY_RGB,PAL_DUNES,PAL_NIGHT,PAL_SAND};
typedef struct {
    ScU32 rgb,id,index,registered,legacy;
    char group[64],hex[16],channels[32],state[32];
} PaletteRow;
typedef struct {
    ScUiNode table,preview,details,hex,rgb,slot,filter,source,details_panel,splitter;
    ScUiProvider *provider;
    ScUiSelection *selection;
    ScUiClass preview_class;
    ScUiIcon *icons[280];
    ScU32 icon_colors[280];
    PaletteRow rows[280];
    ScU32 raw[256],raw_valid,theme_valid,mode,selected,count,revision,sync,descending,field,observed_theme_epoch,raw_stale,theme_stale;
    ScU32 order[280],rank[280];
    ScThemeInfo theme;
    char filter_text[256];
} PaletteApp;
static PaletteApp pal;
#endif
