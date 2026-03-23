

#pragma once
#include <iostream>
#include <string>

#include "utils.h"

// supress warning from miniaudio the external library i use for audio
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "miniaudio.h"
#pragma warning(pop)

class AudioManager {
private:
    ma_engine engine;
    ma_sound currentBGM;
    bool isBgmLoaded;

public:
    AudioManager() 
    {
        isBgmLoaded = false;
        ma_result result = ma_engine_init(NULL, &engine);
        if (result != MA_SUCCESS)
            std::cerr << "Failed to initialize miniaudio engine!" << std::endl;
    }

    ~AudioManager() 
    {
        if (isBgmLoaded) ma_sound_uninit(&currentBGM);
        ma_engine_uninit(&engine);
    }

    void PlayBGM(const std::string& filename) 
    {
        if (isBgmLoaded)
        {
            ma_sound_stop(&currentBGM);
            ma_sound_uninit(&currentBGM);
            isBgmLoaded = false;
        }

        std::string actualPath = FindAssetsFile(filename);
        ma_result result = ma_sound_init_from_file(&engine, actualPath.c_str(), 0, NULL, NULL, &currentBGM);
        if (result == MA_SUCCESS) 
        {
            ma_sound_set_looping(&currentBGM, MA_TRUE);
            ma_sound_start(&currentBGM);
            ma_sound_set_volume(&currentBGM, 0.5f);
            isBgmLoaded = true;
        }
    }

    void PlaySFX(const std::string& filename)
    {
        std::string actualPath = FindAssetsFile(filename);
        ma_result result = ma_engine_play_sound(&engine, actualPath.c_str(), NULL);

        if (result != MA_SUCCESS)
            std::cerr << "[Audio] ERROR: Miniaudio failed to play " << filename << " (Error Code: " << result << ")\n";
    }

    void SetMasterVolume(float volume) 
    {
        if (volume < 0.0f) volume = 0.0f;
        if (volume > 1.0f) volume = 1.0f;

        ma_engine_set_volume(&engine, volume);
    }
};