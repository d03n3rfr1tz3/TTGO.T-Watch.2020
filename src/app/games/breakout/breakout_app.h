/****************************************************************************
 *   September 06 12:00:00 2026
 *   Copyright  2026  Dirk Sarodnick
 *   Email: programmer@dirk-sarodnick.de
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#pragma once

#include "app/games/gamebase.h"
#include "app/games/firework.h"

#define FIELD_WIDTH 240
#define FIELD_HEIGHT 240

#define BALL_SIZE 8
#define BALL_SPEED_MIN 3.5f
#define BALL_SPEED_MAX 13.7f
#define BALL_ANGLE_MIN 25

#define PADDLE_WIDTH 44
#define PADDLE_HEIGHT 8
#define PADDLE_Y (FIELD_HEIGHT - 16)
#define PADDLE_SPEED_MAX 12
#define PADDLE_SMOOTHING 2
#define PADDLE_ANGLE_MAX 60

#define BRICK_COLS 8
#define BRICK_ROWS 5
#define BRICK_WIDTH 28
#define BRICK_HEIGHT 12
#define BRICK_PITCH_X 30
#define BRICK_PITCH_Y 15
#define BRICK_LEFT 1
#define BRICK_TOP 26

#define BREAKOUT_LIVES 3
#define SCORE_PER_BRICK 10
#define SCORE_PER_HARD_BRICK 25

/*
 * Breakout: one octave apart per surface so the brick, the paddle and the walls stay tellable apart by ear alone.
 */
#define SND_BREAKOUT_BRICK      "bb:d=16,o=7,b=180:c"
#define SND_BREAKOUT_HARD       "bh:d=16,o=6,b=180:f"
#define SND_BREAKOUT_PADDLE     "bp:d=16,o=6,b=180:b"
#define SND_BREAKOUT_WALL       "bw:d=16,o=5,b=180:b"
#define SND_BREAKOUT_LOSE       "bl:d=16,o=5,b=180:e,c"
#define SND_BREAKOUT_WIN        "bn:d=32,o=6,b=200:16p,c,e,g,8c7"

void breakout_app_setup();

class BreakoutIcon;

class BreakoutApp : public GameBase
{

private:
    BreakoutIcon *mParentIcon = 0;
    bool breakout_inited = false;
    bool breakout_active = false;
    int16_t control_acc_x = 0;

    enum GameState : uint8_t
    {
        Playing,
        Won,
        Lost,
    };

    // Gameplay data
    float ball_speed = BALL_SPEED_MIN;
    uint16_t ball_bounce = 0;
    uint16_t brick_bounce = 0;
    uint16_t ball_degree = 0;
    float ball_x = (FIELD_WIDTH / 2);
    float ball_y = (FIELD_HEIGHT / 2);
    int16_t paddle_x = 0;
    uint8_t mBrickHp[BRICK_ROWS][BRICK_COLS];
    uint16_t mBricksLeft = 0;
    uint16_t mScore = 0;
    uint8_t mLives = BREAKOUT_LIVES;
    GameState mState = Playing;

    // Visual data
    lv_style_t mStyleApp;
    lv_style_t mStyleMenu;
    lv_style_t style_ball;
    lv_style_t style_paddle;
    lv_style_t style_scoreboard;
    lv_style_t mStyleResult;
    lv_obj_t *bar_ball = 0;
    lv_obj_t *bar_paddle = 0;
    lv_obj_t *label_scoreboard = 0;
    lv_obj_t *mBrickObj[BRICK_ROWS][BRICK_COLS] = {{0}};
    lv_obj_t *mOverlay = 0;
    lv_obj_t *mResultLabel = 0;
    GameFirework mFirework;

    bool TurnDegree(uint16_t base_degree);
    bool SetDegree(int16_t degree);

    void UpdateBall();
    void UpdatePaddle();
    void UpdateBoard();

    bool CheckWalls();
    bool CheckPaddle();
    bool CheckBricks();

    void HitBrick(int row, int col);
    void GenerateBricks();
    void LoseBall();
    void EndGame(GameState state);

    void ResetBall();
    void ResetPaddle();

    void OnExitClicked();

public:
    enum MenuItem : uint8_t
    {
        Reset,
        Exit,
        NumMenuItems
    };

    BreakoutApp(BreakoutIcon *callingIcon);
    ~BreakoutApp();

    // Loops some typical game logic
    void Loop();

    // Resets the whole game and rolls a new brick map
    void ResetGame();

    // Launch from watch mainbar
    void OnLaunch();

    // A menu item was clicked.
    void OnMenuClicked(MenuItem item);

    // The game over overlay was clicked.
    void OnOverlayClicked();

    // The active tile changed.
    void OnTileChanged();

    // Watch goes into standby.
    void OnStandby();
};
