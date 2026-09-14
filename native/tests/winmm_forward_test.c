/*
 * Runtime check for the winmm proxy.
 *
 * Imports a spread of winmm functions by name - including every function
 * steamclient64.dll imports - and calls the side-effect-free ones, printing
 * deterministic results. Run it against the real winmm and through the proxy:
 * the output must be identical. Deliberately not named NMS.exe, so the proxy
 * acts as a pure forwarder with no flight hooking.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

int main(void)
{
    /* Referenced (not all called) so the loader has to bind every one of them. */
    void *bound[] = {
        mixerClose, mixerGetControlDetailsA, mixerGetDevCapsA, mixerGetID,
        mixerGetLineControlsA, mixerGetLineInfoA, mixerOpen, mixerSetControlDetails,
        waveInClose, waveInMessage, waveInOpen, waveOutClose, waveOutGetDevCapsW,
        waveOutMessage, waveOutOpen, waveOutReset, waveOutWrite, waveOutPrepareHeader,
        midiOutShortMsg, mciSendStringW, PlaySoundW, joyGetPosEx, timeSetEvent,
    };
    int nonnull = 0;
    for (size_t i = 0; i < sizeof bound / sizeof bound[0]; i++)
        nonnull += bound[i] != NULL;
    printf("bound=%u nonnull=%d\n", (unsigned)(sizeof bound / sizeof bound[0]), nonnull);

    printf("mmioStringToFOURCCA(RIFF)=%08lx\n", (unsigned long)mmioStringToFOURCCA("RIFF", 0));
    printf("mmioStringToFOURCCA(wave,upper)=%08lx\n", (unsigned long)mmioStringToFOURCCA("wave", MMIO_TOUPPER));
    printf("timeBeginPeriod(1)=%u\n", timeBeginPeriod(1));
    printf("timeEndPeriod(1)=%u\n", timeEndPeriod(1));
    TIMECAPS tc;
    MMRESULT r = timeGetDevCaps(&tc, sizeof tc);
    printf("timeGetDevCaps=%u min=%u max=%u\n", r, tc.wPeriodMin, tc.wPeriodMax);
    DWORD t1 = timeGetTime(); Sleep(20); DWORD t2 = timeGetTime();
    printf("timeGetTime advances=%d\n", (t2 - t1) >= 10 && (t2 - t1) < 2000);
    printf("waveOutGetNumDevs=%u waveInGetNumDevs=%u midiOutGetNumDevs=%u auxGetNumDevs=%u mixerGetNumDevs=%u joyGetNumDevs=%u\n",
           waveOutGetNumDevs(), waveInGetNumDevs(), midiOutGetNumDevs(),
           auxGetNumDevs(), mixerGetNumDevs(), joyGetNumDevs());
    WAVEOUTCAPSW wc;
    r = waveOutGetDevCapsW(WAVE_MAPPER, &wc, sizeof wc);
    printf("waveOutGetDevCapsW(mapper)=%u channels=%u\n", r, r == 0 ? wc.wChannels : 0);
    printf("waveOutReset(NULL)=%u\n", waveOutReset(NULL));   /* invalid handle: error code only */
    printf("PlaySoundW(NULL)=%d\n", PlaySoundW(NULL, NULL, 0));
    return 0;
}
