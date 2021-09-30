#include "util.h"
#include "led.h"
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "minIni.h"
#include <switch.h>

static bool inputThreadRunning = true;
static bool paused = false;
static Mutex pausedMutex = 0;
static Thread pauseThread;
static HidNpadButton comboKeys[8] = {0};
static PadState pad = {0};

bool isHidHandheld()
{
    return padIsHandheld(&pad);
}

void initPads()
{
    padInitializeAny(&pad);
}

void inputPoller()
{
    do
    {
        padUpdate(&pad);
        u64 kHeld = padGetButtons(&pad);

        u64 keyCombo = 0;
        for (u8 i = 0; i != sizearray(comboKeys); ++i)
            keyCombo |= comboKeys[i];

        static bool keyComboPressed = false;

        if ((kHeld & keyCombo) == keyCombo)
        {
            if (!keyComboPressed)
            {
                keyComboPressed = true;
                setPaused(!isPaused());
            }
        }
        else
        {
            keyComboPressed = false;
        }
        svcSleepThread(1e+8);
    } while (inputThreadRunning);
}

const char* buttons[] = {
    "A",
    "B",
    "X",
    "Y",
    "LS",
    "RS",
    "L",
    "R",
    "ZL",
    "ZR",
    "PLUS",
    "MINUS",
    "DLEFT",
    "DUP",
    "DRIGHT",
    "DDOWN",
};

HidNpadButton GetKey(const char* text)
{
    for (u8 i = 0; i != sizearray(buttons); ++i)
    {
        if (strcmp(text, buttons[i]) == 0)
        {
            return BIT(i);
        }
    }
    return 0;
}

Result pauseInit()
{
    Result rc;
    mutexLock(&pausedMutex);

    FILE* should_pause_file = fopen("/config/sys-ftpd/ftpd_paused", "r");
    if (should_pause_file != NULL)
    {
        paused = true;
        fclose(should_pause_file);
    }

    {
        char buffer[128];
        ini_gets("Pause", "keycombo:", "PLUS+MINUS+X", buffer, 128, CONFIGPATH);
        char* token = strtok(buffer, "+ ");
        int i = 0;
        while (token != NULL && i != sizearray(comboKeys))
        {
            comboKeys[i++] = GetKey(token);
            token = strtok(NULL, "+ ");
        };
    }

    inputThreadRunning = true;

    rc = threadCreate(&pauseThread, inputPoller, NULL, NULL, 0x300, 0x3B, -2);
    if (R_FAILED(rc))
        goto exit;

    rc = threadStart(&pauseThread);
    if (R_FAILED(rc))
        goto exit;

exit:
    mutexUnlock(&pausedMutex);
    return rc;
}

void pauseExit()
{
    inputThreadRunning = false;
    threadWaitForExit(&pauseThread);
    threadClose(&pauseThread);
}

bool isPaused()
{
    mutexLock(&pausedMutex);
    bool ret = paused;
    mutexUnlock(&pausedMutex);
    return ret;
}

void setPaused(bool newPaused)
{
    mutexLock(&pausedMutex);
    paused = newPaused;
    if (paused)
    {
        FILE* should_pause_file = fopen("/config/sys-ftpd/ftpd_paused", "w");
        fclose(should_pause_file);
        flash_led_pause();
    }
    else
    {
        unlink("/config/sys-ftpd/ftpd_paused");
        flash_led_unpause();
    }
    mutexUnlock(&pausedMutex);
}
