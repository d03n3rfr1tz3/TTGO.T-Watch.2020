/****************************************************************************
 *   September 09 12:00:00 2026
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

#define BOARD_COLS 4
#define BOARD_ROWS 4
#define BOARD_CELLS (BOARD_COLS * BOARD_ROWS)

#define CELL_SIZE 50
#define CELL_GAP 3
#define BOARD_SIZE (BOARD_COLS * CELL_SIZE + (BOARD_COLS + 1) * CELL_GAP)

/* Exponent of the tile that wins the game: 2^11 = 2048. */
#define WIN_EXPONENT 11

/*
 * 2048: a soft click per move, a brighter one when tiles merge.
 */
#define SND_G2048_MOVE  "gm:d=32,o=6,b=220:32p,e"
#define SND_G2048_MERGE "gg:d=32,o=6,b=220:32p,a"
#define SND_G2048_WIN   "gw:d=32,o=6,b=200:16p,c,e,g,8c7"
#define SND_G2048_OVER  "go:d=16,o=5,b=180:e,c"

void g2048_app_setup();

class G2048Icon;

class G2048App : public GameBase
{

private:
    G2048Icon *mParentIcon = 0;

    enum GameState : uint8_t
    {
        Playing,
        Won,
        Lost,
    };

    // Gameplay data
    uint8_t mBoard[BOARD_CELLS] = {0};
    uint32_t mScore = 0;
    uint32_t mBestScore = 0;
    bool mWinShown = false;
    GameState mState = Playing;

    // Visual data
    lv_style_t mStyleApp;
    lv_style_t mStyleHeader;
    lv_style_t mStyleResult;
    lv_obj_t *mBoardObj = 0;
    lv_obj_t *mCellObj[BOARD_CELLS] = {0};
    lv_obj_t *mCellLabel[BOARD_CELLS] = {0};
    lv_obj_t *mScoreLabel = 0;
    lv_obj_t *mBestLabel = 0;
    lv_obj_t *mMenuOverlay = 0;
    lv_obj_t *mOverlay = 0;
    lv_obj_t *mResultLabel = 0;
    GameFirework mFirework;

    bool SlideRow(uint8_t *row, bool *merged);
    bool HasMove();
    void SpawnTile();

    void UpdateBoard();
    void UpdateScore();
    void EndGame(GameState state);

    void LoadGame();
    void SaveGame();

    void OnExitClicked();

public:
    enum Direction : uint8_t
    {
        Up,
        Down,
        Left,
        Right
    };

    enum MenuItem : uint8_t
    {
        Reset,
        Exit,
        NumMenuItems
    };

    G2048App(G2048Icon *callingIcon);
    ~G2048App();

    void ResetGame();
    void OnLaunch();
    void OnSwipe(Direction dir);
    void OnMenuOpen();
    void OnMenuClose();
    void OnMenuClicked(MenuItem item);
    void OnOverlayClicked();
    void OnStandby();
};
