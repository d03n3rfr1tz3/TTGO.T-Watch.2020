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

#include "g2048_app.h"
#include "g2048_game.h"
#include "g2048_config.h"

#if defined( M5CORE2 )
    LV_IMG_DECLARE( bg2_320px );
    static const lv_img_dsc_t * gameplay_bg = &bg2_320px;
#elif defined( M5PAPER )
    LV_IMG_DECLARE( bg2_540px );
    static const lv_img_dsc_t * gameplay_bg = &bg2_540px;
#elif defined( WT32_SC01 )
    LV_IMG_DECLARE( bg2_480px );
    static const lv_img_dsc_t * gameplay_bg = &bg2_480px;
#elif defined( LILYGO_WATCH_2020_V1 ) || defined( LILYGO_WATCH_2020_V2 ) || defined( LILYGO_WATCH_2020_V3 )
    LV_IMG_DECLARE( bg2 );
    static const lv_img_dsc_t * gameplay_bg = &bg2;
#else
    LV_IMG_DECLARE( bg2_240px );
    static const lv_img_dsc_t * gameplay_bg = &bg2_240px;
#endif

LV_FONT_DECLARE(Ubuntu_16px);
LV_FONT_DECLARE(Ubuntu_32px);

#define MAX_COLOR_EXPONENT 11
static const lv_color_t CELL_COLORS[MAX_COLOR_EXPONENT + 1] = {
    LV_COLOR_MAKE(0xcd, 0xc1, 0xb4),
    LV_COLOR_MAKE(0xee, 0xe4, 0xda),
    LV_COLOR_MAKE(0xed, 0xe0, 0xc8),
    LV_COLOR_MAKE(0xf2, 0xb1, 0x79),
    LV_COLOR_MAKE(0xf5, 0x95, 0x63),
    LV_COLOR_MAKE(0xf6, 0x7c, 0x5f),
    LV_COLOR_MAKE(0xf6, 0x5e, 0x3b),
    LV_COLOR_MAKE(0xed, 0xcf, 0x72),
    LV_COLOR_MAKE(0xed, 0xcc, 0x61),
    LV_COLOR_MAKE(0xed, 0xc8, 0x50),
    LV_COLOR_MAKE(0xed, 0xc5, 0x3f),
    LV_COLOR_MAKE(0xed, 0xc2, 0x2e),
};

#define BOARD_COLOR LV_COLOR_MAKE(0xbb, 0xad, 0xa0)
#define CELL_TEXT_DARK LV_COLOR_MAKE(0x77, 0x6e, 0x65)

static g2048_config_t g2048_config;

static G2048App *gameInstance = 0;

static void OnGesture(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;
    if (event != LV_EVENT_GESTURE) return;

    switch (lv_indev_get_gesture_dir(lv_indev_get_act()))
    {
        case (LV_GESTURE_DIR_TOP):    gameInstance->OnSwipe(G2048App::Up); break;
        case (LV_GESTURE_DIR_BOTTOM): gameInstance->OnSwipe(G2048App::Down); break;
        case (LV_GESTURE_DIR_LEFT):   gameInstance->OnSwipe(G2048App::Left); break;
        case (LV_GESTURE_DIR_RIGHT):  gameInstance->OnSwipe(G2048App::Right); break;
    }
}

static void OnSettings(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnMenuOpen();
            break;
    }
}

static void OnMenuBackground(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnMenuClose();
            break;
    }
}

static void OnExit(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnMenuClicked(G2048App::Exit);
            break;
    }
}

static void OnReset(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnMenuClicked(G2048App::Reset);
            break;
    }
}

static void OnOverlay(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnOverlayClicked();
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

void g2048_app_setup()
{
    g2048_config.load();
    powermgm_register_cb( POWERMGM_STANDBY, OnPower, "g2048 powermgm");
}

static char EncodeExponent(uint8_t exponent)
{
    return (exponent < 10) ? ('0' + exponent) : ('a' + exponent - 10);
}

static uint8_t DecodeExponent(char digit)
{
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'z') return digit - 'a' + 10;
    return 0;
}

G2048App::G2048App(G2048Icon *icon)
{
    gameInstance = this;
    mParentIcon = icon;

    log_d("Creating game tiles...");
    if (!AllocateAppTiles(1, 1))
    {
        log_e("Could not allocate tiles. Aborting.");
        return;
    }
    lv_obj_t *gameplayTile = GetTile(0);

    {
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

        lv_style_init(&mStyleHeader);
        lv_style_set_text_font(&mStyleHeader, LV_STATE_DEFAULT, &Ubuntu_16px);
        lv_style_set_text_color(&mStyleHeader, LV_STATE_DEFAULT, LV_COLOR_WHITE);

        lv_obj_t *gameplay_img = lv_img_create(gameplayTile, NULL);
        lv_img_set_src(gameplay_img, gameplay_bg);
        lv_obj_set_width(gameplay_img, LV_HOR_RES);
        lv_obj_set_height(gameplay_img, LV_VER_RES);
        lv_obj_align(gameplay_img, NULL, LV_ALIGN_CENTER, 0, 0);

        /* The whole tile is the play area, so the gesture stops bubbling here. */
        lv_obj_set_gesture_parent(gameplayTile, false);
        lv_obj_set_event_cb(gameplayTile, OnGesture);
    }

    {
        mScoreLabel = lv_label_create(gameplayTile, NULL);
        lv_obj_add_style(mScoreLabel, LV_LABEL_PART_MAIN, &mStyleHeader);
        lv_label_set_text_static(mScoreLabel, "");
        lv_obj_set_click(mScoreLabel, false);
        lv_obj_align(mScoreLabel, gameplayTile, LV_ALIGN_IN_TOP_LEFT, 4, 4);

        mBestLabel = lv_label_create(gameplayTile, NULL);
        lv_obj_add_style(mBestLabel, LV_LABEL_PART_MAIN, &mStyleHeader);
        lv_label_set_text_static(mBestLabel, "");
        lv_obj_set_click(mBestLabel, false);
        lv_obj_align(mBestLabel, gameplayTile, LV_ALIGN_IN_TOP_MID, 8, 4);

        lv_obj_t *button = lv_btn_create(gameplayTile, NULL);
        lv_obj_t *label = lv_label_create(button, NULL);
        lv_label_set_text(label, LV_SYMBOL_SETTINGS);
        lv_obj_set_event_cb(button, OnSettings);
        lv_obj_set_size(button, 32, 20);
        lv_obj_align(button, gameplayTile, LV_ALIGN_IN_TOP_RIGHT, -2, 1);
    }

    {
        mBoardObj = lv_obj_create(gameplayTile, NULL);
        lv_obj_set_size(mBoardObj, BOARD_SIZE, BOARD_SIZE);
        lv_obj_reset_style_list(mBoardObj, LV_OBJ_PART_MAIN);
        lv_obj_set_style_local_bg_opa(mBoardObj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_obj_set_style_local_bg_color(mBoardObj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, BOARD_COLOR);
        lv_obj_set_style_local_radius(mBoardObj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 4);
        lv_obj_align(mBoardObj, gameplayTile, LV_ALIGN_CENTER, 0, 10);
        
        /* Board and cells stay unclickable, otherwise LVGL hands the press from cell to cell
         * while the finger travels and resets the gesture with every crossed border. */
        lv_obj_set_click(mBoardObj, false);

        for (int row = 0; row < BOARD_ROWS; row++)
        {
            for (int col = 0; col < BOARD_COLS; col++)
            {
                const int idx = row * BOARD_COLS + col;

                lv_obj_t *cell = lv_obj_create(mBoardObj, NULL);
                lv_obj_set_size(cell, CELL_SIZE, CELL_SIZE);
                lv_obj_set_pos(cell, CELL_GAP + col * (CELL_SIZE + CELL_GAP), CELL_GAP + row * (CELL_SIZE + CELL_GAP));
                lv_obj_reset_style_list(cell, LV_OBJ_PART_MAIN);
                lv_obj_set_style_local_bg_opa(cell, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
                lv_obj_set_style_local_radius(cell, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 3);
                lv_obj_set_click(cell, false);
                mCellObj[idx] = cell;

                lv_obj_t *label = lv_label_create(cell, NULL);
                lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
                lv_label_set_text_static(label, "");
                mCellLabel[idx] = label;
            }
        }
    }

    {
        mMenuOverlay = lv_obj_create(gameplayTile, NULL);
        lv_obj_set_size(mMenuOverlay, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(mMenuOverlay, 0, 0);
        lv_obj_reset_style_list(mMenuOverlay, LV_OBJ_PART_MAIN);
        lv_obj_set_style_local_bg_opa(mMenuOverlay, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_70);
        lv_obj_set_style_local_bg_color(mMenuOverlay, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        lv_obj_set_click(mMenuOverlay, true);
        lv_obj_set_event_cb(mMenuOverlay, OnMenuBackground);
        lv_tileview_add_element(GetTileView(), mMenuOverlay);

        lv_obj_t *button;
        lv_obj_t *label;

        int offset = 80;
        constexpr int buttonSpacing = 32;
        constexpr int buttonWidth = 120;
        constexpr int buttonHeight = 24;

        button = lv_btn_create(mMenuOverlay, NULL);
        label = lv_label_create(button, NULL);
        lv_label_set_text(label, "New Game");
        lv_obj_set_event_cb(button, OnReset);
        lv_obj_align(button, mMenuOverlay, LV_ALIGN_IN_TOP_MID, 0, offset);
        lv_obj_set_size(button, buttonWidth, buttonHeight);
        offset += buttonSpacing;

        button = lv_btn_create(mMenuOverlay, NULL);
        label = lv_label_create(button, NULL);
        lv_label_set_text(label, "Exit");
        lv_obj_set_event_cb(button, OnExit);
        lv_obj_align(button, mMenuOverlay, LV_ALIGN_IN_TOP_MID, 0, offset);
        lv_obj_set_size(button, buttonWidth, buttonHeight);

        lv_obj_set_hidden(mMenuOverlay, true);
    }

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

        lv_obj_set_hidden(mOverlay, true);
    }

    mBestScore = g2048_config.best_score;
    LoadGame();

    log_d("Construction complete");
}

G2048App::~G2048App()
{
    mFirework.Stop();
    FreeAppTiles();
    gameInstance = nullptr;
}

void G2048App::OnLaunch()
{
    lv_tileview_set_tile_act(GetTileView(), 0, 0, LV_ANIM_OFF);
}

void G2048App::OnExitClicked()
{
    log_d("Exiting...");
    mFirework.Stop();
    SaveGame();
    mParentIcon->OnExitClicked();
    FreeAppTiles();
}

void G2048App::LoadGame()
{
    if (strlen(g2048_config.board) == BOARD_CELLS)
    {
        bool empty = true;
        for (int i = 0; i < BOARD_CELLS; i++)
        {
            mBoard[i] = DecodeExponent(g2048_config.board[i]);
            if (mBoard[i]) empty = false;
            if (mBoard[i] >= WIN_EXPONENT) mWinShown = true;
        }

        if (!empty)
        {
            mScore = g2048_config.score;
            mState = Playing;
            log_d("Restored a game with score %d", mScore);
            UpdateBoard();
            return;
        }
    }

    ResetGame();
}

void G2048App::SaveGame()
{
    for (int i = 0; i < BOARD_CELLS; i++)
    {
        g2048_config.board[i] = EncodeExponent(mBoard[i]);
    }
    g2048_config.board[BOARD_CELLS] = '\0';
    g2048_config.score = mScore;
    g2048_config.best_score = mBestScore;
    g2048_config.save();
}

void G2048App::SpawnTile()
{
    uint8_t empty[BOARD_CELLS];
    int count = 0;

    for (int i = 0; i < BOARD_CELLS; i++)
    {
        if (!mBoard[i]) empty[count++] = i;
    }
    if (!count) return;

    mBoard[empty[random(0, count)]] = random(0, 10) ? 1 : 2;
}

bool G2048App::SlideRow(uint8_t *row, bool *merged)
{
    uint8_t result[BOARD_COLS] = {0};
    bool locked[BOARD_COLS] = {false};
    int pos = 0;

    for (int i = 0; i < BOARD_COLS; i++)
    {
        if (!row[i]) continue;

        if (pos > 0 && result[pos - 1] == row[i] && !locked[pos - 1])
        {
            result[pos - 1]++;
            locked[pos - 1] = true;
            mScore += (1u << result[pos - 1]);
            *merged = true;
        }
        else
        {
            result[pos++] = row[i];
        }
    }

    bool changed = memcmp(result, row, BOARD_COLS) != 0;
    memcpy(row, result, BOARD_COLS);
    return changed;
}

bool G2048App::HasMove()
{
    for (int row = 0; row < BOARD_ROWS; row++)
    {
        for (int col = 0; col < BOARD_COLS; col++)
        {
            const uint8_t value = mBoard[row * BOARD_COLS + col];
            if (!value) return true;
            if (col + 1 < BOARD_COLS && mBoard[row * BOARD_COLS + col + 1] == value) return true;
            if (row + 1 < BOARD_ROWS && mBoard[(row + 1) * BOARD_COLS + col] == value) return true;
        }
    }
    return false;
}

void G2048App::OnSwipe(Direction dir)
{
    if (mState != Playing) return;
    if (!lv_obj_get_hidden(mMenuOverlay)) return;

    bool changed = false;
    bool merged = false;

    for (int line = 0; line < BOARD_COLS; line++)
    {
        int index[BOARD_COLS];
        uint8_t row[BOARD_COLS];

        /* gather each line with the leading cell first, so one slide covers all four directions */
        for (int i = 0; i < BOARD_COLS; i++)
        {
            switch (dir)
            {
                case Left:  index[i] = line * BOARD_COLS + i; break;
                case Right: index[i] = line * BOARD_COLS + (BOARD_COLS - 1 - i); break;
                case Up:    index[i] = i * BOARD_COLS + line; break;
                default:    index[i] = (BOARD_ROWS - 1 - i) * BOARD_COLS + line; break;
            }
            row[i] = mBoard[index[i]];
        }

        if (SlideRow(row, &merged)) changed = true;

        for (int i = 0; i < BOARD_COLS; i++) mBoard[index[i]] = row[i];
    }

    if (!changed) return;

    SpawnTile();
    UpdateBoard();
    lv_disp_trig_activity(NULL);
    sound_play_rtttl( merged ? SND_G2048_MERGE : SND_G2048_MOVE, SOUND_TYPE_BACKGROUND );

    if (!mWinShown)
    {
        for (int i = 0; i < BOARD_CELLS; i++)
        {
            if (mBoard[i] < WIN_EXPONENT) continue;

            mWinShown = true;
            EndGame(Won);
            return;
        }
    }

    if (!HasMove()) EndGame(Lost);
}

void G2048App::UpdateBoard()
{
    char temp[8];

    for (int i = 0; i < BOARD_CELLS; i++)
    {
        const uint8_t exponent = mBoard[i];
        const uint8_t color = (exponent > MAX_COLOR_EXPONENT) ? MAX_COLOR_EXPONENT : exponent;

        lv_obj_set_style_local_bg_color(mCellObj[i], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, CELL_COLORS[color]);

        if (!exponent)
        {
            lv_label_set_text_static(mCellLabel[i], "");
            continue;
        }

        snprintf(temp, sizeof(temp), "%d", (int)(1u << exponent));
        lv_label_set_text(mCellLabel[i], temp);
        lv_obj_set_style_local_text_font(mCellLabel[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, (exponent > 6) ? &Ubuntu_16px : &Ubuntu_32px);
        lv_obj_set_style_local_text_color(mCellLabel[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, (exponent > 2) ? LV_COLOR_WHITE : CELL_TEXT_DARK);
        lv_obj_align(mCellLabel[i], mCellObj[i], LV_ALIGN_CENTER, 0, 0);
    }

    UpdateScore();
}

void G2048App::UpdateScore()
{
    char temp[24];

    if (mScore > mBestScore) mBestScore = mScore;

    snprintf(temp, sizeof(temp), "SCORE %d", mScore);
    lv_label_set_text(mScoreLabel, temp);

    snprintf(temp, sizeof(temp), "BEST %d", mBestScore);
    lv_label_set_text(mBestLabel, temp);
}

void G2048App::EndGame(GameState state)
{
    mState = state;
    lv_disp_trig_activity(NULL);

    char temp[32];
    if (state == Won)
    {
        snprintf(temp, sizeof(temp), "2048!\n%d", mScore);
        lv_label_set_text(mResultLabel, temp);
        lv_obj_set_style_local_text_color(mResultLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_YELLOW);

        sound_play_rtttl(SND_G2048_WIN, SOUND_TYPE_BACKGROUND);
        motor_vibe(10);
        mFirework.Start(LV_HOR_RES / 2, LV_VER_RES / 2, LV_COLOR_ORANGE);
    }
    else
    {
        snprintf(temp, sizeof(temp), "GAME OVER\n%d", mScore);
        lv_label_set_text(mResultLabel, temp);
        lv_obj_set_style_local_text_color(mResultLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

        sound_play_rtttl(SND_G2048_OVER, SOUND_TYPE_BACKGROUND);
        motor_vibe(10);
    }

    lv_obj_align(mResultLabel, mOverlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(mOverlay, false);

    SaveGame();
}

void G2048App::ResetGame()
{
    mFirework.Stop();
    lv_obj_set_hidden(mOverlay, true);

    mState = Playing;
    mScore = 0;
    mWinShown = false;
    memset(mBoard, 0, sizeof(mBoard));

    SpawnTile();
    SpawnTile();
    UpdateBoard();
}

void G2048App::OnMenuOpen()
{
    motor_vibe(1);
    lv_obj_set_hidden(mMenuOverlay, false);
}

void G2048App::OnMenuClose()
{
    lv_obj_set_hidden(mMenuOverlay, true);
}

void G2048App::OnMenuClicked(MenuItem item)
{
    switch (item)
    {
        case Reset:
            lv_obj_set_hidden(mMenuOverlay, true);
            ResetGame();
            break;
        case Exit:
            lv_obj_set_hidden(mMenuOverlay, true);
            OnExitClicked();
            break;
        default:
            log_e("Unknown menu command %d", item);
    }
}

void G2048App::OnOverlayClicked()
{
    mFirework.Stop();
    lv_obj_set_hidden(mOverlay, true);

    if (mState == Won) mState = Playing;
    else ResetGame();
}

void G2048App::OnStandby()
{
    SaveGame();
}
