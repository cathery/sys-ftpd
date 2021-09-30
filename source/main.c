#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "console.h"
#include "ftp.h"
#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

// only for mkdir, used when creating the "logs" directory
#include <sys/stat.h>

#include <switch.h>

#include "util.h"

#include "minIni.h"

#define HEAP_SIZE 0xA7000

// We aren't an applet.
u32 __nx_applet_type = AppletType_None;
// We're a sysmodule and don't need multithreaded FS. Use 1 session instead of default 3.
u32 __nx_fs_num_sessions = 1;

// setup a fake heap
char fake_heap[HEAP_SIZE];

// we override libnx internals to do a minimal init
void __libnx_initheap(void)
{
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    // setup newlib fake heap
    fake_heap_start = fake_heap;
    fake_heap_end = fake_heap + HEAP_SIZE;
}

void __appInit(void)
{
    R_ASSERT(smInitialize());
    R_ASSERT(fsInitialize());
    R_ASSERT(fsdevMountSdmc());
    R_ASSERT(timeInitialize());
    R_ASSERT(hidInitialize());
    R_ASSERT(hidsysInitialize());
    R_ASSERT(setsysInitialize());
    SetSysFirmwareVersion fw;
    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
        hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
    setsysExit();

    static const SocketInitConfig socketInitConfig = {
        .bsdsockets_version = 1,

        .tcp_tx_buf_size = 0x800,
        .tcp_rx_buf_size = 0x800,
        .tcp_tx_buf_max_size = 0x25000,
        .tcp_rx_buf_max_size = 0x25000,

        //We don't use UDP, set all UDP buffers to 0
        .udp_tx_buf_size = 0,
        .udp_rx_buf_size = 0,

        .sb_efficiency = 1,
    };
    R_ASSERT(socketInitialize(&socketInitConfig));
    smExit();
}

void __appExit(void)
{
    socketExit();
    hidsysExit();
    hidExit();
    timeExit();
    fsdevUnmountAll();
    fsExit();
}

static loop_status_t loop(loop_status_t (*callback)(void))
{
    loop_status_t status = LOOP_CONTINUE;

    while (true)
    {
        svcSleepThread(1e+7);
        status = callback();
        console_render();
        if (status != LOOP_CONTINUE)
            return status;
        if (isPaused())
            return LOOP_RESTART;
    }
    return LOOP_EXIT;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    FILE* should_log_file = fopen("/config/sys-ftpd/logs/ftpd_log_enabled", "r");
    if (should_log_file != NULL)
    {
        should_log = true;
        fclose(should_log_file);

        mkdir("/config/sys-ftpd/logs", 0700);
        unlink("/config/sys-ftpd/logs/ftpd.log");
    }

    char buffer[100];
    ini_gets("Pause", "disabled:", "0", buffer, 100, CONFIGPATH);

    initPads();

    //Checks if pausing is disabled in the config file, in which case it skips the entire pause initialization
    if (strncmp(buffer, "1", 4) != 0)
    {
        Result rc = pauseInit();
        if (R_FAILED(rc))
            fatalThrow(rc);
    }

    loop_status_t status = LOOP_RESTART;

    ftp_pre_init();
    while (status == LOOP_RESTART)
    {
        while (isPaused())
        {
            svcSleepThread(1e+9);
        }

        /* initialize ftp subsystem */
        if (ftp_init() == 0)
        {
            /* ftp loop */
            status = loop(ftp_loop);

            /* done with ftp */
            ftp_exit();
        }
        else
            status = LOOP_EXIT;
    }
    ftp_post_exit();

    pauseExit();

    return 0;
}
