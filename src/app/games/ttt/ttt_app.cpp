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

// Set logging level for this file
#include "config.h"
#include <Arduino.h>
#include <memory>
#include <utility>

#include "gui/mainbar/mainbar.h"
#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/statusbar.h"
#include "hardware/display.h"
#include "hardware/motor.h"
#include "hardware/powermgm.h"
#include "hardware/sound.h"

#include "ttt_app.h"
#include "ttt_game.h"

#if defined( M5CORE2 )
    LV_IMG_DECLARE( bg1_320px );
    LV_IMG_DECLARE( bg2_320px );
    static const lv_img_dsc_t * menu_bg = &bg1_320px;
    static const lv_img_dsc_t * gameplay_bg = &bg2_320px;
#elif defined( M5PAPER )
    LV_IMG_DECLARE( bg1_540px );
    LV_IMG_DECLARE( bg2_540px );
    static const lv_img_dsc_t * menu_bg = &bg1_540px;
    static const lv_img_dsc_t * gameplay_bg = &bg2_540px;
#elif defined( WT32_SC01 )
    LV_IMG_DECLARE( bg1_480px );
    LV_IMG_DECLARE( bg2_480px );
    static const lv_img_dsc_t * menu_bg = &bg1_480px;
    static const lv_img_dsc_t * gameplay_bg = &bg2_480px;
#elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
    LV_IMG_DECLARE( bg1 );
    LV_IMG_DECLARE( bg2 );
    static const lv_img_dsc_t * menu_bg = &bg1;
    static const lv_img_dsc_t * gameplay_bg = &bg2;
#else
    LV_IMG_DECLARE( bg1_240px );
    LV_IMG_DECLARE( bg2_240px );
    static const lv_img_dsc_t * menu_bg = &bg1_240px;
    static const lv_img_dsc_t * gameplay_bg = &bg2_240px;
#endif

LV_FONT_DECLARE(Ubuntu_32px);

/* Board geometry, three 64px squares at a 72px pitch */
static constexpr int SQUARE_SIZE = 64;
static constexpr int SQUARE_POS[3] = {16, 88, 160};

static const uint8_t WIN_LINES[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, // rows
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, // columns
    {0, 4, 8}, {2, 4, 6}};           // diagonals

/* These would be unnecessary if LVGL supported a data param... */

static TicTacToeApp *gameInstance = 0;
static void OnSquareUL(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnTileClicked(0);
            break;
    }
}
static void OnSquareUC(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnTileClicked(1);
            break;
    }
}
static void OnSquareUR(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnTileClicked(2);
            break;
    }
}
static void OnSquareCL(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnTileClicked(3);
            break;
    }
}
static void OnSquareCC(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnTileClicked(4);
            break;
    }
}
static void OnSquareCR(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnTileClicked(5);
            break;
    }
}
static void OnSquareBL(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnTileClicked(6);
            break;
    }
}
static void OnSquareBC(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnTileClicked(7);
            break;
    }
}
static void OnSquareBR(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnTileClicked(8);
            break;
    }
}

static void OnOverlay(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->ClearBoard();
            break;
    }
}

static void OnExit(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnMenuClicked(TicTacToeApp::Exit);
            break;
    }
}

static void OnReset(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnMenuClicked(TicTacToeApp::Reset);
            break;
    }
}

static void OnSwitch(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_VALUE_CHANGED):
            gameInstance->OnTileChanged();
            break;
    }
}

static bool OnPower(EventBits_t event, void *arg)
{
    if (!gameInstance) return( true );

    switch( event ) {
        case( POWERMGM_STANDBY ):
            gameInstance->OnStandby();
            break;
    }
    return( true );
}

void tic_tac_toe_app_setup()
{
    powermgm_register_cb( POWERMGM_STANDBY, OnPower, "pong powermgm");
}

TicTacToeApp::TicTacToeApp(TicTacToeIcon *icon)
{
    gameInstance = this;
    mParentIcon = icon;

    log_d("Creating game tiles...");
    if (!AllocateAppTiles(2, 1))
    {
        log_e("Could not allocate tiles. Aborting.");
        return;
    }
    lv_obj_t *menuTile = GetTile(0);
    lv_obj_t *gameplayTile = GetTile(1);

    log_d("Initializing styles");
    {
        // Create a general application style for all the app's tiles in the view
        lv_style_init(&mStyleApp);
        lv_style_set_radius(&mStyleApp, LV_OBJ_PART_MAIN, 0);
        lv_style_set_bg_color(&mStyleApp, LV_OBJ_PART_MAIN, LV_COLOR_NAVY);
        lv_style_set_bg_opa(&mStyleApp, LV_OBJ_PART_MAIN, LV_OPA_0);
        lv_style_set_border_width(&mStyleApp, LV_OBJ_PART_MAIN, 0);
        lv_style_set_text_color(&mStyleApp, LV_OBJ_PART_MAIN, LV_COLOR_WHITE);
        lv_style_set_image_recolor(&mStyleApp, LV_OBJ_PART_MAIN, LV_COLOR_WHITE);

        lv_tileview_set_edge_flash(GetTileView(), false);
        lv_obj_add_style(GetTileView(), LV_OBJ_PART_MAIN, &mStyleApp);
        lv_page_set_scrlbar_mode(GetTileView(), LV_SCRLBAR_MODE_DRAG);
        lv_obj_set_event_cb(GetTileView(), OnSwitch);

        // Initialize screen backgrounds
        log_d("Creating background for menu tile");
        lv_obj_t *img_bin = lv_img_create(menuTile, NULL);
        lv_img_set_src(img_bin, menu_bg);
        lv_obj_set_width(img_bin, LV_HOR_RES);
        lv_obj_set_height(img_bin, LV_VER_RES);
        lv_obj_align(img_bin, NULL, LV_ALIGN_CENTER, 0, 0);

        log_d("Creating background for gameplay tile");
        lv_obj_t *gameplay_img = lv_img_create(gameplayTile, NULL);
        lv_img_set_src(gameplay_img, gameplay_bg);
        lv_obj_set_width(gameplay_img, LV_HOR_RES);
        lv_obj_set_height(gameplay_img, LV_VER_RES);
        lv_obj_align(gameplay_img, NULL, LV_ALIGN_CENTER, 0, 0);

        log_d("Creating menu styles");
        lv_style_init(&mStyleMenu);
        lv_style_copy(&mStyleMenu, &mStyleApp);
        lv_style_set_text_color(&mStyleMenu, LV_LABEL_PART_MAIN, LV_COLOR_WHITE);
        lv_style_set_bg_opa(&mStyleMenu, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_bg_color(&mStyleMenu, LV_STATE_DEFAULT, LV_COLOR_BLUE);
        lv_style_set_bg_grad_color(&mStyleMenu, LV_STATE_DEFAULT, LV_COLOR_NAVY);
        lv_style_set_bg_grad_dir(&mStyleMenu, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
        lv_obj_add_style(menuTile, LV_LABEL_PART_MAIN, &mStyleMenu);

        log_d("Creating button styles");
        lv_style_init(&mStyleRed);
        lv_style_set_bg_opa(&mStyleRed, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_bg_color(&mStyleRed, LV_STATE_DEFAULT, LV_COLOR_RED);
        lv_style_set_bg_grad_color(&mStyleRed, LV_STATE_DEFAULT, LV_COLOR_MAROON);
        lv_style_set_bg_grad_dir(&mStyleRed, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
        lv_style_set_bg_color(&mStyleRed, LV_BTN_STATE_DISABLED, LV_COLOR_RED);
        lv_style_set_bg_grad_color(&mStyleRed, LV_BTN_STATE_DISABLED, LV_COLOR_MAROON);
        lv_style_set_bg_grad_dir(&mStyleRed, LV_BTN_STATE_DISABLED, LV_GRAD_DIR_VER);

        lv_style_init(&mStyleBlue);
        lv_style_set_bg_opa(&mStyleBlue, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_bg_color(&mStyleBlue, LV_STATE_DEFAULT, LV_COLOR_BLUE);
        lv_style_set_bg_grad_color(&mStyleBlue, LV_STATE_DEFAULT, LV_COLOR_NAVY);
        lv_style_set_bg_grad_dir(&mStyleBlue, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
        lv_style_set_bg_color(&mStyleBlue, LV_BTN_STATE_DISABLED, LV_COLOR_BLUE);
        lv_style_set_bg_grad_color(&mStyleBlue, LV_BTN_STATE_DISABLED, LV_COLOR_NAVY);
        lv_style_set_bg_grad_dir(&mStyleBlue, LV_BTN_STATE_DISABLED, LV_GRAD_DIR_VER);

        lv_style_init(&mStyleBlank);
        lv_style_set_bg_opa(&mStyleBlank, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_bg_color(&mStyleBlank, LV_STATE_DEFAULT, LV_COLOR_SILVER);
        lv_style_set_bg_grad_color(&mStyleBlank, LV_STATE_DEFAULT, LV_COLOR_GRAY);
        lv_style_set_bg_grad_dir(&mStyleBlank, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
        lv_style_set_bg_color(&mStyleBlank, LV_BTN_STATE_DISABLED, LV_COLOR_SILVER);
        lv_style_set_bg_grad_color(&mStyleBlank, LV_BTN_STATE_DISABLED, LV_COLOR_GRAY);
        lv_style_set_bg_grad_dir(&mStyleBlank, LV_BTN_STATE_DISABLED, LV_GRAD_DIR_VER);
    }

    log_d("Initializing menu");
    {
        lv_obj_t *button;
        lv_obj_t *label;

        int offset = 64; // starting offset down the screen
        constexpr int buttonSpacing = 32;
        constexpr int buttonWidth = 120;
        constexpr int buttonHeight = 24;

        label = lv_label_create(menuTile, NULL);
        lv_label_set_text_static(label, "Tic Tac Toe");
        lv_obj_align(label, menuTile, LV_ALIGN_IN_TOP_MID, 0, 10);
        lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);

        button = lv_btn_create(menuTile, NULL);
        label = lv_label_create(button, NULL);
        lv_label_set_text(label, "New Game");
        lv_obj_set_event_cb(button, OnReset);
        lv_obj_align(button, menuTile, LV_ALIGN_IN_TOP_MID, 0, offset);
        lv_obj_set_size(button, buttonWidth, buttonHeight);
        offset += buttonSpacing;

        button = lv_btn_create(menuTile, NULL);
        label = lv_label_create(button, NULL);
        lv_label_set_text(label, "Exit");
        lv_obj_set_event_cb(button, OnExit);
        lv_obj_align(button, menuTile, LV_ALIGN_IN_TOP_MID, 0, offset);
        lv_obj_set_size(button, buttonWidth, buttonHeight);
        offset += buttonSpacing;

        label = lv_label_create(menuTile, NULL);
        lv_label_set_text_static(label, "Two players, tap a square.\nSwipe left for the play area.");
        lv_obj_align(label, menuTile, LV_ALIGN_IN_TOP_MID, 0, offset);
        lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
        offset += buttonSpacing;
    }

    log_d("Initializing game board");
    {
        // Display a TTT grid on the gameplay page
        const lv_event_cb_t funcs[NUM_SQUARES] = {
            OnSquareUL, OnSquareUC, OnSquareUR,
            OnSquareCL, OnSquareCC, OnSquareCR,
            OnSquareBL, OnSquareBC, OnSquareBR};

        for (int i = 0; i < NUM_SQUARES; i++)
        {
            mButtons[i] = lv_btn_create(gameplayTile, NULL);
            if (!mButtons[i])
                log_e("Error creating button %d. Crash is immenent.", i);
            lv_obj_set_pos(mButtons[i], SQUARE_POS[i % 3], SQUARE_POS[i / 3]);
            lv_obj_set_size(mButtons[i], SQUARE_SIZE, SQUARE_SIZE);
            lv_obj_reset_style_list(mButtons[i], LV_BTN_PART_MAIN);
            lv_obj_add_style(mButtons[i], LV_BTN_PART_MAIN, &mStyleBlank);
            lv_obj_set_event_cb(mButtons[i], funcs[i]);
        }
    }

    log_d("Initializing game over overlay");
    {
        mOverlay = lv_obj_create(gameplayTile, NULL);
        lv_obj_set_size(mOverlay, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(mOverlay, 0, 0);
        lv_obj_reset_style_list(mOverlay, LV_OBJ_PART_MAIN);
        lv_obj_set_style_local_bg_opa(mOverlay, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
        lv_obj_set_click(mOverlay, true);
        lv_obj_set_event_cb(mOverlay, OnOverlay);
        lv_tileview_add_element(GetTileView(), mOverlay);

        lv_style_init(&mStyleResult);
        lv_style_set_text_font(&mStyleResult, LV_STATE_DEFAULT, &Ubuntu_32px);
        lv_style_set_text_color(&mStyleResult, LV_STATE_DEFAULT, LV_COLOR_WHITE);
        lv_style_set_bg_opa(&mStyleResult, LV_STATE_DEFAULT, LV_OPA_70);
        lv_style_set_bg_color(&mStyleResult, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        lv_style_set_radius(&mStyleResult, LV_STATE_DEFAULT, 6);
        lv_style_set_pad_left(&mStyleResult, LV_STATE_DEFAULT, 10);
        lv_style_set_pad_right(&mStyleResult, LV_STATE_DEFAULT, 10);
        lv_style_set_pad_top(&mStyleResult, LV_STATE_DEFAULT, 6);
        lv_style_set_pad_bottom(&mStyleResult, LV_STATE_DEFAULT, 6);

        mResultLabel = lv_label_create(mOverlay, NULL);
        lv_obj_add_style(mResultLabel, LV_LABEL_PART_MAIN, &mStyleResult);
        lv_label_set_align(mResultLabel, LV_LABEL_ALIGN_CENTER);
        lv_label_set_text_static(mResultLabel, "");
        lv_obj_set_click(mResultLabel, false);

        mFirework.Create(gameplayTile);
    }

    ClearBoard();

    ttt_inited = true;
    log_d("Construction complete");
}

TicTacToeApp::~TicTacToeApp()
{
    mFirework.Stop();
    // LVGL Objects parented to the tiles should be deleted when the tiles are destroyed.
    FreeAppTiles();
    gameInstance = nullptr;
}

// Open from watch menu
void TicTacToeApp::OnLaunch()
{
    lv_tileview_set_tile_act(GetTileView(), 0, 0, LV_ANIM_OFF);
}

void TicTacToeApp::OnExitClicked()
{
    log_d("Exiting...");
    mFirework.Stop();
    // Pass along the message for differed deletion and return to main menu
    mParentIcon->OnExitClicked();
    FreeAppTiles();
}

void TicTacToeApp::OnTileClicked(int index)
{
    if (mState != Playing)
    {
        ClearBoard();
        return;
    }

    if (index < 0 || index >= NUM_SQUARES)
    {
        log_e("Invalid tile number, expected 0-%d, received %d", NUM_SQUARES, index);
        return;
    }

    if (mBoard[index] == Owner::None)
    {
        lv_style_t *style = (mCurrentPlayer == Owner::Red) ? &mStyleRed : &mStyleBlue;

        mBoard[index] = mCurrentPlayer;
        mMoveCount++;

        lv_obj_add_style(mButtons[index], LV_BTN_PART_MAIN, style);
        lv_btn_set_state(mButtons[index], LV_BTN_STATE_DISABLED);

        sound_play_rtttl( (mCurrentPlayer == Owner::Red) ? SND_TTT_MOVE_RED : SND_TTT_MOVE_BLUE, SOUND_TYPE_BACKGROUND );
        motor_vibe(2);

        mWinLine = CheckWinner();
        if (mWinLine >= 0)
        {
            EndGame(Won);
        }
        else if (mMoveCount == NUM_SQUARES)
        {
            EndGame(Draw);
        }
        else
        {
            NextPlayer();
        }
    }
    else
    {
        /* tile already owned, do an error response, beep or something. */
    }
}

int TicTacToeApp::CheckWinner()
{
    for (int line = 0; line < 8; line++)
    {
        const Owner owner = mBoard[WIN_LINES[line][0]];
        if (owner == Owner::None)
            continue;
        if (owner == mBoard[WIN_LINES[line][1]] && owner == mBoard[WIN_LINES[line][2]])
            return line;
    }
    return -1;
}

void TicTacToeApp::EndGame(GameState state)
{
    mState = state;
    lv_disp_trig_activity(NULL);

    if (state == Won)
    {
        const bool red = (mCurrentPlayer == Owner::Red);
        log_d("%s won on line %d", red ? "Red" : "Blue", mWinLine);

        lv_label_set_text(mResultLabel, red ? "Red won!" : "Blue won!");
        lv_obj_set_style_local_text_color(mResultLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, red ? LV_COLOR_RED : LV_COLOR_BLUE);

        sound_play_rtttl(SND_TTT_WIN, SOUND_TYPE_BACKGROUND);
        motor_vibe(10);

        // burst from the middle of the winning line
        lv_coord_t cx = 0;
        lv_coord_t cy = 0;
        for (int i = 0; i < 3; i++)
        {
            const uint8_t square = WIN_LINES[mWinLine][i];
            cx += SQUARE_POS[square % 3] + (SQUARE_SIZE / 2);
            cy += SQUARE_POS[square / 3] + (SQUARE_SIZE / 2);
        }
        mFirework.Start(cx / 3, cy / 3, red ? LV_COLOR_RED : LV_COLOR_BLUE);
    }
    else
    {
        log_d("Draw");

        lv_label_set_text(mResultLabel, "Draw!");
        lv_obj_set_style_local_text_color(mResultLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

        sound_play_rtttl(SND_TTT_DRAW, SOUND_TYPE_BACKGROUND);
        motor_vibe(5);
    }

    lv_obj_align(mResultLabel, mOverlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(mOverlay, false);
}

void TicTacToeApp::ClearBoard()
{
    mFirework.Stop();

    for (Owner &c : mBoard)
    {
        c = Owner::None;
    }
    for (lv_obj_t *button : mButtons)
    {
        lv_obj_reset_style_list(button, LV_BTN_PART_MAIN);
        lv_obj_add_style(button, LV_BTN_PART_MAIN, &mStyleBlank);
        lv_btn_set_state(button, LV_BTN_STATE_RELEASED);
    }
    mCurrentPlayer = Red;
    mState = Playing;
    mMoveCount = 0;
    mWinLine = -1;

    if (mOverlay)
        lv_obj_set_hidden(mOverlay, true);
}

void TicTacToeApp::OnMenuClicked(MenuItem item)
{
    switch (item)
    {
    case Reset:
        ClearBoard();
        lv_tileview_set_tile_act(GetTileView(), 1, 0, LV_ANIM_ON);
        ttt_active = true;
        break;
    case Exit:
        OnExitClicked();
        ttt_active = false;
        break;
    default:
        log_e("Unknown menu command %d", item);
    }
}

void TicTacToeApp::OnTileChanged()
{
    lv_coord_t x;
    lv_coord_t y;
    lv_tileview_get_tile_act(GetTileView(), &x, &y);
    log_d("Tile changed to %d, %d", x, y);

    if (x == 1) ttt_active = true;
    else ttt_active = false;
}

void TicTacToeApp::OnStandby()
{
    ttt_active = false;
}