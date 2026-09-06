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

#include "config.h"
#include <Arduino.h>
#include <memory>
#include <utility>

#include "gui/mainbar/mainbar.h"
#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/statusbar.h"
#include "hardware/display.h"
#include "hardware/motion.h"
#include "hardware/motor.h"
#include "hardware/powermgm.h"
#include "hardware/sound.h"

#include "breakout_app.h"
#include "breakout_game.h"

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

LV_FONT_DECLARE(Ubuntu_16px);
LV_FONT_DECLARE(Ubuntu_32px);

/* One color per brick row, from the top down. */
static const lv_color_t BRICK_COLORS[BRICK_ROWS] = {
    LV_COLOR_RED, LV_COLOR_ORANGE, LV_COLOR_YELLOW, LV_COLOR_LIME, LV_COLOR_AQUA};

/* Hard bricks take two hits and show this color until the first one lands. */
#define BRICK_HARD_COLOR LV_COLOR_SILVER

/* These would be unnecessary if LVGL supported a data param... */

static BreakoutApp *gameInstance = 0;

static void OnExit(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnMenuClicked(BreakoutApp::Exit);
            break;
    }
}

static void OnReset(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnMenuClicked(BreakoutApp::Reset);
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

void breakout_app_setup()
{
    powermgm_register_cb( POWERMGM_STANDBY, OnPower, "breakout powermgm");
}

BreakoutApp::BreakoutApp(BreakoutIcon *icon)
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

        log_d("Creating game styles");
        lv_style_init(&style_ball);
        lv_style_set_bg_opa(&style_ball, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_bg_color(&style_ball, LV_STATE_DEFAULT, LV_COLOR_WHITE);
        lv_style_set_bg_grad_color(&style_ball, LV_STATE_DEFAULT, LV_COLOR_SILVER);
        lv_style_set_bg_grad_dir(&style_ball, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
        lv_style_set_radius(&style_ball, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
        lv_style_set_border_width(&style_ball, LV_STATE_DEFAULT, 0);

        lv_style_init(&style_paddle);
        lv_style_set_bg_opa(&style_paddle, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_bg_color(&style_paddle, LV_STATE_DEFAULT, LV_COLOR_BLUE);
        lv_style_set_bg_grad_color(&style_paddle, LV_STATE_DEFAULT, LV_COLOR_NAVY);
        lv_style_set_bg_grad_dir(&style_paddle, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
        lv_style_set_border_color(&style_paddle, LV_STATE_DEFAULT, LV_COLOR_WHITE);
        lv_style_set_border_width(&style_paddle, LV_STATE_DEFAULT, 1);
        lv_style_set_radius(&style_paddle, LV_STATE_DEFAULT, 4);

        lv_style_init(&style_scoreboard);
        lv_style_set_text_font(&style_scoreboard, LV_STATE_DEFAULT, &Ubuntu_16px);
        lv_style_set_text_color(&style_scoreboard, LV_STATE_DEFAULT, LV_COLOR_WHITE);
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
        lv_label_set_text_static(label, "Breakout");
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
        lv_label_set_text_static(label, "Tilt the watch to move.\nSwipe left for the play area.");
        lv_obj_align(label, menuTile, LV_ALIGN_IN_TOP_MID, 0, offset);
        lv_label_set_align(label, LV_LABEL_ALIGN_CENTER);
    }

    log_d("Initializing game board");
    {
        for (int row = 0; row < BRICK_ROWS; row++)
        {
            for (int col = 0; col < BRICK_COLS; col++)
            {
                lv_obj_t *brick = lv_obj_create(gameplayTile, NULL);
                lv_obj_set_size(brick, BRICK_WIDTH, BRICK_HEIGHT);
                lv_obj_set_pos(brick, BRICK_LEFT + (col * BRICK_PITCH_X), BRICK_TOP + (row * BRICK_PITCH_Y));
                lv_obj_set_click(brick, false);
                lv_obj_reset_style_list(brick, LV_OBJ_PART_MAIN);
                lv_obj_set_style_local_bg_opa(brick, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
                lv_obj_set_style_local_border_width(brick, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1);
                lv_obj_set_style_local_border_color(brick, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
                lv_obj_set_style_local_radius(brick, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
                lv_obj_set_hidden(brick, true);
                mBrickObj[row][col] = brick;
            }
        }

        bar_paddle = lv_obj_create(gameplayTile, NULL);
        lv_obj_add_style(bar_paddle, LV_OBJ_PART_MAIN, &style_paddle);
        lv_obj_set_click(bar_paddle, false);
        lv_obj_set_size(bar_paddle, PADDLE_WIDTH, PADDLE_HEIGHT);
        lv_obj_set_pos(bar_paddle, (FIELD_WIDTH - PADDLE_WIDTH) / 2, PADDLE_Y);

        bar_ball = lv_obj_create(gameplayTile, NULL);
        lv_obj_add_style(bar_ball, LV_OBJ_PART_MAIN, &style_ball);
        lv_obj_set_click(bar_ball, false);
        lv_obj_set_size(bar_ball, BALL_SIZE, BALL_SIZE);

        label_scoreboard = lv_label_create(gameplayTile, NULL);
        lv_obj_add_style(label_scoreboard, LV_OBJ_PART_MAIN, &style_scoreboard);
        lv_label_set_align(label_scoreboard, LV_LABEL_ALIGN_CENTER);
        lv_label_set_long_mode(label_scoreboard, LV_LABEL_LONG_CROP);
        lv_obj_set_click(label_scoreboard, false);
        lv_obj_set_size(label_scoreboard, FIELD_WIDTH, 20);
        lv_obj_set_pos(label_scoreboard, 0, 4);
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

        // created last so the sparks fly over the result banner
        mFirework.Create(gameplayTile);

        lv_obj_set_hidden(mOverlay, true);
    }

    ResetGame();

    breakout_inited = true;
    log_d("Construction complete");
}

BreakoutApp::~BreakoutApp()
{
    mFirework.Stop();
    // LVGL Objects parented to the tiles should be deleted when the tiles are destroyed.
    FreeAppTiles();
    gameInstance = nullptr;
}

// Open from watch menu
void BreakoutApp::OnLaunch()
{
    lv_tileview_set_tile_act(GetTileView(), 0, 0, LV_ANIM_OFF);
}

void BreakoutApp::OnExitClicked()
{
    log_d("Exiting...");
    mFirework.Stop();
    // Pass along the message for differed deletion and return to main menu
    mParentIcon->OnExitClicked();
    FreeAppTiles();
}

void BreakoutApp::Loop()
{
    if ( !breakout_inited || !breakout_active || mState != Playing ) return;

    UpdateBall();
    UpdatePaddle();

    if (!CheckBricks())
    {
        if (!CheckPaddle()) CheckWalls();
    }

    lv_disp_trig_activity( NULL );
}

/*
 * Reflect the ball off a surface whose outward normal points at base_degree.
 */
bool BreakoutApp::TurnDegree(uint16_t base_degree)
{
    return SetDegree((base_degree * 2) - 180 - ball_degree);
}

/*
 * Take an absolute direction, for surfaces that pick the angle themselves.
 */
bool BreakoutApp::SetDegree(int16_t degree)
{
    while (degree < 0) degree += 360;
    while (degree >= 360) degree -= 360;

    // keep the ball off the horizontal, a shallow one crosses the field a dozen
    // times before it reaches a brick or the paddle
    if (degree < BALL_ANGLE_MIN) degree = BALL_ANGLE_MIN;
    else if (degree > 360 - BALL_ANGLE_MIN) degree = 360 - BALL_ANGLE_MIN;
    else if (degree > 180 - BALL_ANGLE_MIN && degree <= 180) degree = 180 - BALL_ANGLE_MIN;
    else if (degree > 180 && degree < 180 + BALL_ANGLE_MIN) degree = 180 + BALL_ANGLE_MIN;

    log_d("Turn Degree from %d to %d", ball_degree, degree);
    ball_degree = degree;

    return true;
}

void BreakoutApp::UpdateBall()
{
    if (ball_speed < BALL_SPEED_MIN) ball_speed = BALL_SPEED_MIN;
    if (ball_speed > BALL_SPEED_MAX) ball_speed = BALL_SPEED_MAX;

    ball_x = ball_x + ((float)ball_speed * cos((float)ball_degree * PI / 180));
    ball_y = ball_y + ((float)ball_speed * sin((float)ball_degree * PI / 180));

    lv_obj_set_pos(bar_ball, ball_x - (BALL_SIZE / 2), ball_y - (BALL_SIZE / 2));
}

void BreakoutApp::UpdatePaddle()
{
    int16_t acc_x = 0;
    int16_t acc_y = 0;

    if ( !bma_get_accel_rotated( acc_x, acc_y ) ) return;

    // simple low pass
    control_acc_x += ( acc_x - control_acc_x ) / PADDLE_SMOOTHING;

    // the paddle runs horizontally
    int16_t new_position = control_acc_x * 0.2;

    const int16_t boundary = (FIELD_WIDTH - PADDLE_WIDTH) / 2;
    if (new_position > boundary) new_position = boundary;
    if (new_position < 0 - boundary) new_position = 0 - boundary;

    // limit distance by maximum speed
    if (new_position < paddle_x && paddle_x - new_position > PADDLE_SPEED_MAX) new_position = paddle_x - PADDLE_SPEED_MAX;
    if (new_position > paddle_x && new_position - paddle_x > PADDLE_SPEED_MAX) new_position = paddle_x + PADDLE_SPEED_MAX;

    paddle_x = new_position;
    lv_obj_set_pos(bar_paddle, boundary + paddle_x, PADDLE_Y);
}

bool BreakoutApp::CheckWalls()
{
    constexpr int half = BALL_SIZE / 2;

    // left wall, normal points right
    if (ball_x <= half && (ball_degree > 90 && ball_degree < 270))
    {
        sound_play_rtttl( SND_BREAKOUT_WALL, SOUND_TYPE_BACKGROUND );
        motor_vibe(1);
        return TurnDegree(0);
    }

    // right wall, normal points left
    if (ball_x >= FIELD_WIDTH - half && (ball_degree < 90 || ball_degree > 270))
    {
        sound_play_rtttl( SND_BREAKOUT_WALL, SOUND_TYPE_BACKGROUND );
        motor_vibe(1);
        return TurnDegree(180);
    }

    // ceiling, normal points down
    if (ball_y <= half && ball_degree > 180)
    {
        sound_play_rtttl( SND_BREAKOUT_WALL, SOUND_TYPE_BACKGROUND );
        motor_vibe(1);
        return TurnDegree(90);
    }

    // the floor is the only surface that does not bounce
    if (ball_y >= FIELD_HEIGHT - half)
    {
        LoseBall();
        return true;
    }

    return false;
}

bool BreakoutApp::CheckPaddle()
{
    constexpr int half = BALL_SIZE / 2;

    // only while falling, otherwise the ball can get caught inside the paddle
    if (ball_degree >= 180) return false;
    if (ball_y + half < PADDLE_Y) return false;
    if (ball_y - half > PADDLE_Y + PADDLE_HEIGHT) return false;

    const int16_t left = ((FIELD_WIDTH - PADDLE_WIDTH) / 2) + paddle_x;
    if (ball_x + half < left || ball_x - half > left + PADDLE_WIDTH) return false;

    // classic breakout returns the ball by where it hit the paddle,
    // the incoming angle plays no part in it
    int16_t offset = map(ball_x, left, left + PADDLE_WIDTH, -PADDLE_ANGLE_MAX, PADDLE_ANGLE_MAX);
    offset = constrain(offset, -PADDLE_ANGLE_MAX, PADDLE_ANGLE_MAX);
    if (offset < 5 && offset > -5) offset = 0;

    if (ball_bounce > 0 && ball_bounce % 2 == 0) ball_speed++;
    ball_bounce++;

    sound_play_rtttl( SND_BREAKOUT_PADDLE, SOUND_TYPE_BACKGROUND );
    motor_vibe(3);

    return SetDegree(270 + offset);
}

bool BreakoutApp::CheckBricks()
{
    constexpr int half = BALL_SIZE / 2;

    const int16_t top = BRICK_TOP;
    const int16_t bottom = BRICK_TOP + (BRICK_ROWS * BRICK_PITCH_Y);
    if (ball_y + half < top || ball_y - half > bottom) return false;

    const int row = ((int)ball_y - top) / BRICK_PITCH_Y;
    const int col = ((int)ball_x - BRICK_LEFT) / BRICK_PITCH_X;
    if (row < 0 || row >= BRICK_ROWS || col < 0 || col >= BRICK_COLS) return false;
    if (mBrickHp[row][col] == 0) return false;

    const int16_t bx = BRICK_LEFT + (col * BRICK_PITCH_X);
    const int16_t by = BRICK_TOP + (row * BRICK_PITCH_Y);

    // the ball may still be over the gap between two bricks
    const int16_t overlap_x = min((int)ball_x + half, bx + BRICK_WIDTH) - max((int)ball_x - half, (int)bx);
    const int16_t overlap_y = min((int)ball_y + half, by + BRICK_HEIGHT) - max((int)ball_y - half, (int)by);
    if (overlap_x <= 0 || overlap_y <= 0) return false;

    HitBrick(row, col);

    // the shallower overlap tells which edge was crossed.
    // push the ball back out of it, otherwise a surviving hard brick can be hit twice in a row
    if (overlap_x < overlap_y)
    {
        const bool fromLeft = ball_x < bx + (BRICK_WIDTH / 2);
        ball_x = fromLeft ? (bx - half) : (bx + BRICK_WIDTH + half);
        TurnDegree(fromLeft ? 180 : 0);
    }
    else
    {
        const bool fromTop = ball_y < by + (BRICK_HEIGHT / 2);
        ball_y = fromTop ? (by - half) : (by + BRICK_HEIGHT + half);
        TurnDegree(fromTop ? 270 : 90);
    }

    lv_obj_set_pos(bar_ball, ball_x - (BALL_SIZE / 2), ball_y - (BALL_SIZE / 2));

    return true;
}

void BreakoutApp::HitBrick(int row, int col)
{
    if (brick_bounce > 0 && brick_bounce % 4 == 0) ball_speed++;
    brick_bounce++;

    mBrickHp[row][col]--;

    if (mBrickHp[row][col] > 0)
    {
        // a hard brick drops to its row color and takes one more hit
        lv_obj_set_style_local_bg_color(mBrickObj[row][col], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, BRICK_COLORS[row]);
        // the rest of its value follows when it finally breaks
        mScore += SCORE_PER_HARD_BRICK - SCORE_PER_BRICK;
        UpdateBoard();

        sound_play_rtttl( SND_BREAKOUT_HARD, SOUND_TYPE_BACKGROUND );
        motor_vibe(2);
        return;
    }

    lv_obj_set_hidden(mBrickObj[row][col], true);
    mBricksLeft--;
    mScore += SCORE_PER_BRICK;
    UpdateBoard();

    sound_play_rtttl( SND_BREAKOUT_BRICK, SOUND_TYPE_BACKGROUND );
    motor_vibe(2);

    if (mBricksLeft == 0) EndGame(Won);
}

/*
 * Rolls a fresh map. Only the left half is random, the right half mirrors it,
 * which reads as a designed layout instead of noise and costs nothing.
 */
void BreakoutApp::GenerateBricks()
{
    constexpr int halfCols = BRICK_COLS / 2;

    mBricksLeft = 0;

    for (int row = 0; row < BRICK_ROWS; row++)
    {
        uint8_t left[halfCols];
        int count = 0;

        // roll until the row holds at least one brick
        while (count == 0)
        {
            const int density = random(50, 101);
            for (int col = 0; col < halfCols; col++)
            {
                if (random(0, 100) >= density)
                {
                    left[col] = 0;
                    continue;
                }
                left[col] = (random(0, 100) < 20) ? 2 : 1;
                count++;
            }
        }

        for (int col = 0; col < halfCols; col++)
        {
            const uint8_t hp = left[col];
            const int mirror = BRICK_COLS - 1 - col;

            mBrickHp[row][col] = hp;
            mBrickHp[row][mirror] = hp;

            const int pair[2] = {col, mirror};
            for (int i = 0; i < 2; i++)
            {
                lv_obj_t *brick = mBrickObj[row][pair[i]];
                if (hp == 0)
                {
                    lv_obj_set_hidden(brick, true);
                    continue;
                }
                lv_obj_set_style_local_bg_color(brick, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, (hp > 1) ? BRICK_HARD_COLOR : BRICK_COLORS[row]);
                lv_obj_set_hidden(brick, false);
                mBricksLeft++;
            }
        }
    }

    log_d("Generated %d bricks", mBricksLeft);
}

void BreakoutApp::LoseBall()
{
    if (mLives > 0) mLives--;

    log_d("Ball lost, %d lives left", mLives);

    sound_play_rtttl( SND_BREAKOUT_LOSE, SOUND_TYPE_BACKGROUND );
    motor_vibe(10);

    UpdateBoard();

    if (mLives == 0)
    {
        EndGame(Lost);
        return;
    }

    ResetBall();
}

void BreakoutApp::EndGame(GameState state)
{
    mState = state;
    lv_disp_trig_activity(NULL);

    char temp[32];
    if (state == Won)
    {
        // clearing the map is the whole game, so it gets the full celebration
        snprintf(temp, sizeof(temp), "WIN!\n%d", mScore);
        lv_label_set_text(mResultLabel, temp);
        lv_obj_set_style_local_text_color(mResultLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_YELLOW);

        sound_play_rtttl(SND_BREAKOUT_WIN, SOUND_TYPE_BACKGROUND);
        motor_vibe(10);
        mFirework.Start(FIELD_WIDTH / 2, FIELD_HEIGHT / 2, LV_COLOR_ORANGE);
    }
    else
    {
        snprintf(temp, sizeof(temp), "GAME OVER\n%d", mScore);
        lv_label_set_text(mResultLabel, temp);
        lv_obj_set_style_local_text_color(mResultLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    }

    lv_obj_align(mResultLabel, mOverlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(mOverlay, false);
}

void BreakoutApp::UpdateBoard()
{
    char temp[24];
    snprintf(temp, sizeof(temp), "%d   -   %d", mScore, mLives);
    lv_label_set_text(label_scoreboard, temp);
    lv_event_send_refresh(label_scoreboard);
}

void BreakoutApp::ResetBall()
{
    ball_speed = BALL_SPEED_MIN;
    ball_bounce = 0;
    brick_bounce = 0;

    // start upwards, leaning to either side
    ball_degree = 270 + random(-40, 41);
    while (ball_degree >= 360) ball_degree -= 360;

    ball_x = (FIELD_WIDTH / 2);
    ball_y = PADDLE_Y - 20;

    lv_obj_set_pos(bar_ball, ball_x - (BALL_SIZE / 2), ball_y - (BALL_SIZE / 2));
}

void BreakoutApp::ResetPaddle()
{
    paddle_x = 0;
    lv_obj_set_pos(bar_paddle, (FIELD_WIDTH - PADDLE_WIDTH) / 2, PADDLE_Y);
}

void BreakoutApp::ResetGame()
{
    mFirework.Stop();
    lv_obj_set_hidden(mOverlay, true);

    mState = Playing;
    mScore = 0;
    mLives = BREAKOUT_LIVES;

    GenerateBricks();
    ResetBall();
    ResetPaddle();
    UpdateBoard();
}

void BreakoutApp::OnMenuClicked(MenuItem item)
{
    switch (item)
    {
        case Reset:
            ResetGame();
            lv_tileview_set_tile_act(GetTileView(), 1, 0, LV_ANIM_ON);
            breakout_active = true;
            break;
        case Exit:
            OnExitClicked();
            breakout_active = false;
            break;
        default:
            log_e("Unknown menu command %d", item);
    }
}

void BreakoutApp::OnOverlayClicked()
{
    ResetGame();
}

void BreakoutApp::OnTileChanged()
{
    lv_coord_t x;
    lv_coord_t y;
    lv_tileview_get_tile_act(GetTileView(), &x, &y);
    log_d("Tile changed to %d, %d", x, y);

    breakout_active = (x == 1);
}

void BreakoutApp::OnStandby()
{
    breakout_active = false;
}
