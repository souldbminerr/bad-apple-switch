#include <switch.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <malloc.h>
// use ffmpeg to convert to wav
// ex: ffmpeg -i BA.wav -ac 2 -ar 44100 -sample_fmt s16 BA_conv.wav
static inline double getTimeSeconds()
{
    return armGetSystemTick() / (double)armGetSystemTickFreq();
}
constexpr double FPS = 30.0;
constexpr size_t WAV_HEADER_SIZE = 44;

struct WavData {
    u8* data;
    size_t size;
};

bool loadWav(const char* path, WavData& out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    rewind(f);

    if (fileSize <= WAV_HEADER_SIZE) {
        fclose(f);
        return false;
    }

    fseek(f, WAV_HEADER_SIZE, SEEK_SET);

    out.size = fileSize - WAV_HEADER_SIZE;
    out.data = (u8*)memalign(0x1000, out.size);

    fread(out.data, 1, out.size, f);
    fclose(f);

    return true;
}

void playMusic()
{
    audoutInitialize();
    audoutStartAudioOut();

    WavData wav{};
    if (!loadWav("romfs:/res/BA.wav", wav))
        return;

    AudioOutBuffer buffer{};
    buffer.next = nullptr;
    buffer.buffer = wav.data;
    buffer.buffer_size = wav.size;
    buffer.data_size = wav.size;

    while (appletMainLoop())
    {
        audoutAppendAudioOutBuffer(&buffer);

        AudioOutBuffer* released = nullptr;
        u32 released_count = 0;
        audoutWaitPlayFinish(&released, &released_count, UINT64_MAX);
    }

    audoutStopAudioOut();
    audoutExit();

    free(wav.data);
}

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

int main(int argc, char* argv[])
{
    consoleInit(NULL);
    romfsInit();

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    std::thread musicThread(playMusic);

	svcSleepThread(500'000'000);

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

		svcSleepThread(1'000'000);
	}

    musicThread.join();

    romfsExit();
    consoleExit(NULL);
    return 0;
}
