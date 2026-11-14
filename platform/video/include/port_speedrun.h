#ifndef PORT_SPEEDRUN_H
#define PORT_SPEEDRUN_H

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_SPEEDRUN_MAX_SPLITS 40

typedef struct PortSpeedrunSplit {
  char name[40];
  char time[20];
  char delta[20];
  int ahead;
} PortSpeedrunSplit;

typedef struct PortSpeedrunCheck {
  char text[64];
  int state;
} PortSpeedrunCheck;

#define PORT_SPEEDRUN_MAX_CHECKS 14

typedef struct PortSpeedrunView {
  int visible;
  char time[24];
  char label[40];
  int current;
  int warn;
  char info[96];
  char info2[96];
  int splitCount;
  int checkCount;
  PortSpeedrunCheck checks[PORT_SPEEDRUN_MAX_CHECKS];
  PortSpeedrunSplit splits[PORT_SPEEDRUN_MAX_SPLITS];
} PortSpeedrunView;

void PortSpeedrun_SetView(const PortSpeedrunView* view);

void PortSpeedrun_Draw(void);

#ifdef __cplusplus
}
#endif

#endif
