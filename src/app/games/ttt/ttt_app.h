/*   July 31 21:33:35 2020
 *   Copyright  2020  Bryan Wagstaff
 *   Email: programmer@bryanwagstaff.com
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

/*
 * Tic Tac Toe: move and game ending tones.
 */
#define SND_TTT_MOVE_RED        "mr:d=32,o=6,b=200:32p,b"
#define SND_TTT_MOVE_BLUE       "mb:d=32,o=6,b=200:32p,a"
#define SND_TTT_WIN             "win:d=32,o=6,b=200:16p,c,e,g,8c7"
#define SND_TTT_DRAW            "draw:d=32,o=5,b=200:16p,g,e,8c"

void tic_tac_toe_app_setup();

class TicTacToeIcon;

class TicTacToeApp : public GameBase
{

private:
    static constexpr int NUM_SQUARES = 9;
    static constexpr int NUM_SPARKS = 16;
    static constexpr int NUM_BURSTS = 3;

    TicTacToeIcon *mParentIcon = 0;
    bool ttt_inited = false;
    bool ttt_active = false;

    enum Owner : uint8_t
    {
        None = 0,
        Red = 'X',
        Blue = 'O',
    };

    enum GameState : uint8_t
    {
        Playing,
        Won,
        Draw,
    };

    // Gameplay data
    Owner mBoard[NUM_SQUARES];
    Owner mCurrentPlayer = Red;
    GameState mState = Playing;
    int mMoveCount = 0;
    int mWinLine = -1;

    // Visual data
    lv_style_t mStyleApp;
    lv_style_t mStyleMenu;
    lv_style_t mStyleRed;
    lv_style_t mStyleBlue;
    lv_style_t mStyleBlank;
    lv_style_t mStyleResult;
    lv_obj_t *mButtons[NUM_SQUARES] = {0};
    lv_obj_t *mOverlay = 0;
    lv_obj_t *mResultLabel = 0;
    lv_obj_t *mSparks[NUM_SPARKS] = {0};
    lv_task_t *mFireworkTask = 0;
    int mBurstsLeft = 0;

    void NextPlayer() { mCurrentPlayer = (mCurrentPlayer == Red) ? Blue : Red; };

    void OnExitClicked();

    int CheckWinner();
    void EndGame(GameState state);
    void StartFirework();
    void StopFirework();

public:
    void OnFireworkTick();

    enum MenuItem : uint8_t
    {
        Reset,
        Exit,
        NumMenuItems
    };

    TicTacToeApp(TicTacToeIcon *callingIcon);
    ~TicTacToeApp();

    void ClearBoard();

    // Launch from watch mainbar
    void OnLaunch();

    // TTT square pressed
    void OnTileClicked(int index);

    // A menu item was clicked.
    void OnMenuClicked(MenuItem item);

    // A menu item was clicked.
    void OnTileChanged();

    // Watch goes into standby.
    void OnStandby();
};