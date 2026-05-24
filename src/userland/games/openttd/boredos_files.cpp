extern "C" {
#include "libc/syscall.h"
}

#include "boredos_files.h"
#include "port_config.h"

bool openttd_boredos_file_exists(const char *path)
{
    return path != 0 && sys_exists(path) != 0;
}

bool openttd_boredos_file_open_readonly(const char *path, BoredOSFile *out_file)
{
    if (path == 0 || out_file == 0) return false;

    int fd = sys_open(path, "rb");
    if (fd < 0) return false;

    out_file->fd = fd;
    out_file->size = sys_size(fd);
    return true;
}

int openttd_boredos_file_read(BoredOSFile *file, void *buffer, uint32_t bytes)
{
    if (file == 0 || file->fd < 0 || buffer == 0) return -1;
    return sys_read(file->fd, buffer, bytes);
}

bool openttd_boredos_file_seek(BoredOSFile *file, int offset, int whence)
{
    if (file == 0 || file->fd < 0) return false;
    return sys_seek(file->fd, offset, whence) >= 0;
}

void openttd_boredos_file_close(BoredOSFile *file)
{
    if (file == 0 || file->fd < 0) return;
    sys_close(file->fd);
    file->fd = -1;
    file->size = 0;
}

const char *openttd_boredos_data_dir(void)
{
    return OPENTTD_BOREDOS_DATA_DIR;
}

const char *openttd_boredos_baseset_dir(void)
{
    return OPENTTD_BOREDOS_BASESET_DIR;
}

const char *openttd_boredos_required_gfx(void)
{
    return OPENTTD_BOREDOS_REQUIRED_GFX;
}

const char *openttd_boredos_required_sfx(void)
{
    return OPENTTD_BOREDOS_REQUIRED_SFX;
}

const char *openttd_boredos_required_msx(void)
{
    return OPENTTD_BOREDOS_REQUIRED_MSX;
}
