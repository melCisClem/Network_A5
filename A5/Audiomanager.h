#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <windows.h>
#include "miniaudio.h"

class AudioManager {
private:
    ma_engine engine;
    ma_sound currentBGM;
    bool isBgmLoaded;

    bool FileExists(const std::string& path) {
        std::ifstream f(path.c_str());
        return f.good();
    }

    std::string FindAudioFile(const std::string& filename) {
        if (FileExists(filename)) // check current dir
            return filename;

        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string exePath(buffer);
        std::string exeDir = exePath.substr(0, exePath.find_last_of("\\/"));

        std::string pathsToTry[] = {
            exeDir + "\\" + filename,
            exeDir + "\\Audio\\" + filename,
            exeDir + "\\..\\" + filename,
            exeDir + "\\..\\Audio\\" + filename,
            exeDir + "\\..\\..\\" + filename, 
            exeDir + "\\..\\..\\Audio\\" + filename
        };

        for (const std::string& path : pathsToTry) {
            if (FileExists(path)) {
#ifdef _DEBUG
                std::cout << "[Audio] Found " << filename << " at: " << path << "\n";
#endif
                return path;
            }
        }

        std::cerr << "[Audio] WARNING: Could not find " << filename << " anywhere!\n";
        return filename;
    }

public:
    AudioManager() {
        isBgmLoaded = false;
        ma_result result = ma_engine_init(NULL, &engine);
        if (result != MA_SUCCESS)
            std::cerr << "Failed to initialize miniaudio engine!" << std::endl;
    }

    ~AudioManager() {
        if (isBgmLoaded) ma_sound_uninit(&currentBGM);
        ma_engine_uninit(&engine);
    }

    void PlayBGM(const std::string& filename) {
        if (isBgmLoaded) {
            ma_sound_stop(&currentBGM);
            ma_sound_uninit(&currentBGM);
            isBgmLoaded = false;
        }

        std::string actualPath = FindAudioFile(filename);
        ma_result result = ma_sound_init_from_file(&engine, actualPath.c_str(), 0, NULL, NULL, &currentBGM);
        if (result == MA_SUCCESS) {
            ma_sound_set_looping(&currentBGM, MA_TRUE);
            ma_sound_start(&currentBGM);
            ma_sound_set_volume(&currentBGM, 0.5f);
            isBgmLoaded = true;
        }
    }

    void PlaySFX(const std::string& filename) {
        std::string actualPath = FindAudioFile(filename);
        ma_result result = ma_engine_play_sound(&engine, actualPath.c_str(), NULL);

        if (result != MA_SUCCESS) {
            std::cerr << "[Audio] ERROR: Miniaudio failed to play " << filename << " (Error Code: " << result << ")\n";
        }
    }

    void SetMasterVolume(float volume) {
        if (volume < 0.0f) volume = 0.0f;
        if (volume > 1.0f) volume = 1.0f;

        ma_engine_set_volume(&engine, volume);
    }
};