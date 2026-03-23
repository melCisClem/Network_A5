

#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <windows.h>

bool FileExists(const std::string& path)
{
    std::ifstream f(path.c_str());
    return f.good();
}

std::string FindAssetsFile(const std::string& filename)
{
    if (FileExists(filename)) // check current dir
        return filename;

    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string exePath(buffer);
    std::string exeDir = exePath.substr(0, exePath.find_last_of("\\/"));

    std::string pathsToTry[] = {
        exeDir + "\\" + filename,
        exeDir + "\\Assets\\" + filename,
        exeDir + "\\..\\" + filename,
        exeDir + "\\..\\Assets\\" + filename,
        exeDir + "\\..\\..\\" + filename,
        exeDir + "\\..\\..\\Assets\\" + filename
    };

    for (const std::string& path : pathsToTry)
    {
        if (FileExists(path))
        {
#ifdef _DEBUG
            std::cout << "Found " << filename << " at: " << path << "\n";
#endif
            return path;
        }
    }

    std::cerr << "WARNING: Could not find " << filename << " anywhere!\n";
    return filename;
}

void drawMap()
{
    float cellW = 2.0f / MAP_WIDTH;
    float cellH = 2.0f / MAP_HEIGHT;

    glBegin(GL_QUADS);
    glColor3f(0.3f, 0.3f, 0.3f); // Dark Gray Walls

    for (int row = 0; row < MAP_HEIGHT; row++)
    {
        for (int col = 0; col < MAP_WIDTH; col++)
        {
            if (ARENA_MAP[row][col] == 1) {
                // calc the bottom left corner of the grid cell
                float x1 = -1.0f + (col * cellW);
                float y1 = -1.0f + (row * cellH);

                // calc the top-right corner
                float x2 = x1 + cellW;
                float y2 = y1 + cellH;

                glVertex2f(x1, y1); // Bottom-left
                glVertex2f(x2, y1); // Bottom-right
                glVertex2f(x2, y2); // Top-right
                glVertex2f(x1, y2); // Top-left
            }
        }
    }
    glEnd();
}

// draws tank, hp bar, shot cooldown bar
void drawTank(float x, float y, float r, float g, float b, float facingAngle, int hp, int cooldown, bool isLocalPlayer)
{
    float width = tank_width;
    float height = tank_height;
    float gunLength = tank_gunLength;
    float outline_thickness = tank_outline_thickness;
    float hp_thickness = tank_hp_thickness;

    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    // Draw HP n Cooldown bar
    glPushMatrix();
    glTranslatef(0.0f, height + 0.04f, 0.0f); // hp bar above tank body

    float BarWidth = width * 0.9f;
    float BarHeight = hp_thickness;

    // Draw Dark Background
    glBegin(GL_QUADS);
    glColor3f(0.2f, 0.2f, 0.2f);
    glVertex2f(-BarWidth, -BarHeight);
    glVertex2f(BarWidth, -BarHeight);
    glVertex2f(BarWidth, BarHeight);
    glVertex2f(-BarWidth, BarHeight);
    glEnd();

    // Draw Red HP Bar
    float hpPct = fmax(0.0f, (float)hp / (float)MAX_HP);
    float currentWidth = -BarWidth + (2.0f * BarWidth * hpPct);

    glBegin(GL_QUADS);
    glColor3f(0.9f, 0.1f, 0.1f);
    glVertex2f(-BarWidth, -BarHeight);
    glVertex2f(currentWidth, -BarHeight);
    glVertex2f(currentWidth, BarHeight);
    glVertex2f(-BarWidth, BarHeight);
    glEnd();

    // Draw Shoot Cooldown bar
    float maxCD = (float)tank_shootCooldown;
    float cooldownPct = (maxCD - (float)cooldown) / maxCD;

    if (cooldownPct < 0.0f) cooldownPct = 0.0f;
    if (cooldownPct > 1.0f) cooldownPct = 1.0f;

    float cdFill = -BarWidth + (2.0f * BarWidth * cooldownPct);
    float cdY = -0.01f; // Positioned below HP bar
    float cdHeight = hp_thickness;

    // Background
    glColor3f(0.1f, 0.1f, 0.1f); // Dark gray
    glBegin(GL_QUADS);
    glVertex2f(-BarWidth, cdY);
    glVertex2f(BarWidth, cdY);
    glVertex2f(BarWidth, cdY + cdHeight);
    glVertex2f(-BarWidth, cdY + cdHeight);
    glEnd();

    if (cooldown > 0)
        glColor3f(0.3f, 0.8f, 1.0f); // Loading Cyan
    else
        glColor3f(0.2f, 0.8f, 0.2f); // Ready Green

    glBegin(GL_QUADS);
    glVertex2f(-BarWidth, cdY);
    glVertex2f(cdFill, cdY);
    glVertex2f(cdFill, cdY + cdHeight);
    glVertex2f(-BarWidth, cdY + cdHeight);
    glEnd();

    glPopMatrix();


    glRotatef(facingAngle, 0.0f, 0.0f, 1.0f);

    // draw gun barrel
    glLineWidth(4.0f);
    glBegin(GL_LINES);
    glColor3f(0.6f, 0.6f, 0.6f);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(width + gunLength, 0.0f);
    glEnd();
    glLineWidth(1.0f);

    // draw Body
    glBegin(GL_QUADS);
    glColor3f(r, g, b);
    glVertex2f(-width, -height); // Bottom-left
    glVertex2f(width, -height); // Bottom-right
    glVertex2f(width, height); // Top-right
    glVertex2f(-width, height); // Top-left
    glEnd();

    // draw outline only for local player
    if (isLocalPlayer) {
        glLineWidth(outline_thickness);
        glBegin(GL_LINE_LOOP);
        glColor3f(1.0f, 1.0f, 1.0f);

        float offset = 0.001f;
        glVertex2f(-width - offset, -height - offset);
        glVertex2f(width + offset, -height - offset);
        glVertex2f(width + offset, height + offset);
        glVertex2f(-width - offset, height + offset);
        glEnd();
        glLineWidth(1.0f);
    }

    glPopMatrix();
}

void drawProjectile(float x, float y)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glBegin(GL_POLYGON);
    glColor3f(1.0f, 1.0f, 0.0f); // yellow
    for (int i = 0; i < 360; i += 30)
    {
        float theta = i * 3.14159f / 180.0f;
        glVertex2f(0.02f * cos(theta), 0.02f * sin(theta));
    }
    glEnd();
    glPopMatrix();
}


namespace UI 
{
    float barY = -0.1f;
    float barHalfWidth = 0.4f;

    float buttonRadius = 0.05f;
    float gap = 0.08f;

    float quitY = -0.4f;
    float quitSize = 0.06f;
}

void drawPauseMenu(float g_currentVolume)
{
    using namespace UI;
    // Dark Overlay
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f); glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f); glVertex2f(-1.0f, 1.0f);
    glEnd();
    glDisable(GL_BLEND);

    // Pause Icon
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(-0.08f, 0.2f); glVertex2f(-0.03f, 0.2f); glVertex2f(-0.03f, 0.4f); glVertex2f(-0.08f, 0.4f);
    glVertex2f(0.03f, 0.2f); glVertex2f(0.08f, 0.2f); glVertex2f(0.08f, 0.4f); glVertex2f(0.03f, 0.4f);
    glEnd();

    // Volume Bar Background
    glColor3f(0.3f, 0.3f, 0.3f);
    glBegin(GL_QUADS);
    glVertex2f(-barHalfWidth, barY - 0.02f); glVertex2f(barHalfWidth, barY - 0.02f);
    glVertex2f(barHalfWidth, barY + 0.02f); glVertex2f(-barHalfWidth, barY + 0.02f);
    glEnd();

    // Volume Bar Fill
    float fillRight = -barHalfWidth + (g_currentVolume * (barHalfWidth * 2.0f));
    glColor3f(0.2f, 0.8f, 0.2f);
    glBegin(GL_QUADS);
    glVertex2f(-barHalfWidth, barY - 0.02f); glVertex2f(fillRight, barY - 0.02f);
    glVertex2f(fillRight, barY + 0.02f); glVertex2f(-barHalfWidth, barY + 0.02f);
    glEnd();

    // Circular Buttons with a small gap

    // Minus Button (Left)
    glColor3f(0.8f, 0.2f, 0.2f);
    float minusCenterX = -barHalfWidth - gap;
    glBegin(GL_POLYGON);
    for (int i = 0; i < 360; i += 20)
    {
        float theta = i * 3.14159f / 180.0f;
        glVertex2f(minusCenterX + buttonRadius * cos(theta), barY + buttonRadius * sin(theta));
    }
    glEnd();

    // Plus Button (Right)
    glColor3f(0.2f, 0.8f, 0.2f);
    float plusCenterX = barHalfWidth + gap;
    glBegin(GL_POLYGON);
    for (int i = 0; i < 360; i += 20)
    {
        float theta = i * 3.14159f / 180.0f;
        glVertex2f(plusCenterX + buttonRadius * cos(theta), barY + buttonRadius * sin(theta));
    }
    glEnd();

    // Quit Button (A red square with a white X)
    glColor3f(0.8f, 0.2f, 0.2f);
    glBegin(GL_QUADS);
    glVertex2f(-quitSize, quitY - quitSize); glVertex2f(quitSize, quitY - quitSize);
    glVertex2f(quitSize, quitY + quitSize); glVertex2f(-quitSize, quitY + quitSize);
    glEnd();

    // White X
    glColor3f(1.0f, 1.0f, 1.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex2f(-0.03f, quitY - 0.03f); glVertex2f(0.03f, quitY + 0.03f);
    glVertex2f(0.03f, quitY - 0.03f); glVertex2f(-0.03f, quitY + 0.03f);
    glEnd();
    glLineWidth(1.0f);
}

