

#pragma once

#define STB_TRUETYPE_IMPLEMENTATION
#include "../stb_truetype.h"

GLuint g_fontScoreboardTitle, g_fontScoreboard, g_fontPlayerName;
stbtt_bakedchar g_dataScoreboardTitle[96], g_dataScoreboard[96], g_dataPlayerName[96];

#include <string>
#include <fstream>
#include <ostream>

GLuint loadFont(const std::string& fontName, float pixelHeight, stbtt_bakedchar* outData)
{
    std::string fullPath = FindAssetsFile(fontName);
    FILE* fp;
    if (fopen_s(&fp, fullPath.c_str(), "rb") != 0) return 0;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    std::vector<unsigned char> ttf_buffer(size);
    fread(ttf_buffer.data(), 1, size, fp);
    fclose(fp);

    std::vector<unsigned char> temp_bitmap(512 * 512);

    stbtt_BakeFontBitmap(ttf_buffer.data(), 0, pixelHeight, temp_bitmap.data(), 512, 512, 32, 96, outData);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 512, 512, 0, GL_ALPHA, GL_UNSIGNED_BYTE, temp_bitmap.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return tex;
}

void drawTextScreen(float x, float y, const std::string& text, float r, float g, float b, GLuint tex, stbtt_bakedchar* data)
{
    if (tex == 0)
        return;

    int winW, winH;
    glfwGetWindowSize(glfwGetCurrentContext(), &winW, &winH);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, winW, winH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor3f(r, g, b); // Set text color

    // draw the quads for each letter
    glBegin(GL_QUADS);
    for (char c : text)
    {
        if (c >= 32 && c < 128)  // check if valid ASCII
        {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(data, 512, 512, c - 32, &x, &y, &q, 1);

            glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0); // Top-Left
            glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0); // Top-Right
            glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1); // Bottom-Right
            glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1); // Bottom-Left
        }
    }
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

