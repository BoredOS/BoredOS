#ifndef BOREDOS_OPENTTD_FILES_H
#define BOREDOS_OPENTTD_FILES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int fd;
    uint32_t size;
} BoredOSFile;

bool openttd_boredos_file_exists(const char *path);
bool openttd_boredos_file_open_readonly(const char *path, BoredOSFile *out_file);
int openttd_boredos_file_read(BoredOSFile *file, void *buffer, uint32_t bytes);
bool openttd_boredos_file_seek(BoredOSFile *file, int offset, int whence);
void openttd_boredos_file_close(BoredOSFile *file);
const char *openttd_boredos_data_dir(void);
const char *openttd_boredos_baseset_dir(void);
const char *openttd_boredos_required_gfx(void);
const char *openttd_boredos_required_sfx(void);
const char *openttd_boredos_required_msx(void);

#endif
