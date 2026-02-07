#include <switch.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
// use ffmpeg to convert to wav
// ex: ffmpeg -i BA.wav -ac 2 -ar 48000 -sample_fmt s16 BA_out.wav

static inline double getTimeSeconds()
{
    return armGetSystemTick() / (double)armGetSystemTickFreq();
}

constexpr double FPS = 30.0;
constexpr size_t WAV_HEADER_SIZE = 44;
constexpr size_t AUDIO_CHUNK_SIZE = 0x10000; // 64 KB per buffer
constexpr int AUDIO_BUFFER_COUNT = 4;         // 4 buffers

struct AudioRingBuffer {
    u8* data[AUDIO_BUFFER_COUNT];
    AudioOutBuffer buffers[AUDIO_BUFFER_COUNT];
    int count;
    int next; // next buffer to fill
};

void printFrame(int frame)
{
    char path[64];
    snprintf(path, sizeof(path), "romfs:/res/BA%d.txt", frame);

    FILE* f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f))
        printf("%s", line);

    fclose(f);
}

bool initAudioRingBuffer(AudioRingBuffer& ring)
{
    ring.count = AUDIO_BUFFER_COUNT;
    ring.next = 0;

    for (int i = 0; i < ring.count; i++) {
        ring.data[i] = (u8*)memalign(0x1000, AUDIO_CHUNK_SIZE);
        if (!ring.data[i])
            return false;

        ring.buffers[i].next = nullptr;
        ring.buffers[i].buffer = ring.data[i];
        ring.buffers[i].buffer_size = 0;
        ring.buffers[i].data_size = 0;
    }
    return true;
}

void freeAudioRingBuffer(AudioRingBuffer& ring)
{
    for (int i = 0; i < ring.count; i++) {
        free(ring.data[i]);
    }
}

bool fillAudioBuffer(AudioRingBuffer& ring, FILE* wavFile)
{
    AudioOutBuffer* buf = &ring.buffers[ring.next];
    size_t read = fread(buf->buffer, 1, AUDIO_CHUNK_SIZE, wavFile);
    if (read == 0)
        return false; // EOF

    buf->buffer_size = read;
    buf->data_size = read;

    audoutAppendAudioOutBuffer(buf);

    ring.next = (ring.next + 1) % ring.count;
    return true;
}

int main(int argc, char* argv[])
{
    consoleInit(NULL);
    romfsInit();

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    FILE* wavFile = fopen("romfs:/res/BA.wav", "rb");
    if (!wavFile) {
        printf("Failed to open WAV\n");
        romfsExit();
        consoleExit(NULL);
        return 1;
    }

    fseek(wavFile, WAV_HEADER_SIZE, SEEK_SET);

    audoutInitialize();
    audoutStartAudioOut();

    AudioRingBuffer ring;
    if (!initAudioRingBuffer(ring)) {
        printf("Failed to allocate audio buffers\n");
        fclose(wavFile);
        romfsExit();
        consoleExit(NULL);
        return 1;
    }

    AudioOutBuffer* released = nullptr;
    u32 released_count = 0;

    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++)
        fillAudioBuffer(ring, wavFile);

    double startTime = getTimeSeconds();
    int lastFrame = -1;

    while (appletMainLoop())
    {
        double now = getTimeSeconds();
        int frame = (int)((now - startTime) * FPS);
        if (frame >= 6560)
            break;

        if (frame != lastFrame)
        {
            consoleClear();
            printFrame(frame);
            consoleUpdate(NULL);
            lastFrame = frame;
        }

        audoutWaitPlayFinish(&released, &released_count, 0);
        for (u32 i = 0; i < released_count; i++)
            fillAudioBuffer(ring, wavFile); // refill released buffers

        svcSleepThread(1'000'000); // 1 ms
    }

    // Wait for remaining audio to finish
    audoutWaitPlayFinish(&released, &released_count, UINT64_MAX);

    // Cleanup
    freeAudioRingBuffer(ring);
    audoutStopAudioOut();
    audoutExit();
    fclose(wavFile);

    romfsExit();
    consoleExit(NULL);
    return 0;
}
