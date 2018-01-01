#include <stdtype.h>
class MidiFile;
class MidiPlayer;

void vis_init(void);
void vis_deinit(void);
int vis_getch(void);
int vis_getch_wait(void);
void vis_addstr(const char* text);
void vis_set_midi_file(const char* fileName, MidiFile* mFile);
void vis_set_midi_player(MidiPlayer* mPlay);
void vis_new_song(void);
void vis_do_channel_event(UINT16 chn, UINT8 action, UINT8 data);
void vis_do_ins_change(UINT16 chn);
void vis_do_ctrl_change(UINT16 chn, UINT8 ctrl, UINT8 value);
void vis_do_note(UINT16 chn, UINT8 note, UINT8 volume);
void vis_print_meta(UINT16 trk, UINT8 metaType, size_t dataLen, const char* data);
void vis_update(void);
