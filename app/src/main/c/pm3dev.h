#include <stdint.h>
#include <termios.h>

#ifndef PROXMARK3_PM3DEV_H
#define PROXMARK3_PM3DEV_H
extern char *pm3dev_relayd_path;

int pm3dev_change(const char *);
int pm3dev_write(const uint8_t *, size_t);
#endif // PROXMARK3_PM3DEV_H
