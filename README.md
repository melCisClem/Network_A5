# TANK WARS - Networked Multiplayer Game
CSD2161 Assignment 5

## System Requirements
- Visual Studio 2022 with "Desktop development with C++"
- ISO C++ 17 Standard
- Windows Socket 2 (WSA)
- OpenGL & GLFW 3.4
- Freetype 2

## Configuration & Setup
1. **Server**: Run `Server.exe`. It will display your local IP Address (e.g., `192.168.1.10`).
2. **Client**: Run `Client.exe`. 
   - When prompted, enter the Server's IP address. Pressing Enter will attempt to auto-fill the detected local IP.
   - Enter a unique player name (max 15 characters).

## Controls
- **W/S**: Move Forward / Reverse
- **A/D**: Turn Left / Right
- **SPACE**: Shoot (0.5s cooldown)
- **ENTER**: Toggle Chat (Type message and press Enter again to send)
- **TAB**: Show Scoreboard in game and Global Top 5 Leaderboard in waiting room
- **ESC**: Pause Menu (Volume control / Quit)
- **P**: Cheat Win (Instantly win the round)

## Features
- **Lobby System**: Players must all be "READY" before the match starts.
- **Persistent Progression**: Kills are saved across sessions. Access the "EXP" menu from the main menu to spend 10 kills on a permanent Gun Upgrade (larger bullets).
- **Global Leaderboard**: The Top 5 highest-scoring players of all time (kills in a single match) are saved and displayed at the end of every round.
- **Reliable Networking**: Uses UDP for fast movement synchronization and TCP for critical events (Chat, Upgrades, Leaderboard).

## Test Steps (A-Grade Verification)
1. **Multi-Client Sync**: Launch 3+ clients. Verify all players can see each other's movement and rotation in real-time.
2. **Deterministic Shooting**: Fire a bullet from Client A. Verify the bullet path and collision are consistent across all other clients.
3. **Scoreboard & Win Condition**: Have one player reach 5 kills. Verify the "Game Over" screen appears for all players, showing the match winner and the updated Global Top 5 Leaderboard.
4. **Persistence**: Earn kills, quit the game, and restart. Access the EXP screen to verify kills were saved and can be spent on upgrades.
5. **Robustness**: Force-close a client during a match. Verify the server handles the disconnect gracefully and other players are unaffected.
