/****************************************************************************
 *   June 04 02:01:00 2021
 *   Copyright  2021  Dirk Sarodnick
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

#include "pong_app.h"
#include "pong_game.h"

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
LV_FONT_DECLARE(Ubuntu_48px);

/* These would be unnecessary if LVGL supported a data param... */

static PongApp *gameInstance = 0;
static void OnExit(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnMenuClicked(PongApp::Exit);
            break;
    }
}

static void OnReset(struct _lv_obj_t *obj, lv_event_t event)
{
    if (!gameInstance) return;

    switch (event)
    {
        case (LV_EVENT_CLICKED):
            gameInstance->OnMenuClicked(PongApp::Reset);
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

void pong_app_setup()
{
    powermgm_register_cb( POWERMGM_STANDBY, OnPower, "pong powermgm");
}

PongApp::PongApp(PongIcon *icon)
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

        log_d("Creating game styles");
        lv_style_init(&style_ball);
        lv_style_set_bg_opa(&style_ball, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_bg_color(&style_ball, LV_STATE_DEFAULT, LV_COLOR_GRAY);
        lv_style_set_bg_grad_color(&style_ball, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        lv_style_set_bg_grad_dir(&style_ball, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
        lv_style_set_border_color(&style_ball, LV_STATE_DEFAULT, LV_COLOR_WHITE);
        lv_style_set_border_width(&style_ball, LV_STATE_DEFAULT, 1);

        lv_style_init(&style_player1);
        lv_style_set_bg_opa(&style_player1, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_bg_color(&style_player1, LV_STATE_DEFAULT, LV_COLOR_RED);
        lv_style_set_bg_grad_color(&style_player1, LV_STATE_DEFAULT, LV_COLOR_MAROON);
        lv_style_set_bg_grad_dir(&style_player1, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
        lv_style_set_border_color(&style_player1, LV_STATE_DEFAULT, LV_COLOR_WHITE);
        lv_style_set_border_width(&style_player1, LV_STATE_DEFAULT, 1);

        lv_style_init(&style_player2);
        lv_style_set_bg_opa(&style_player2, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_style_set_bg_color(&style_player2, LV_STATE_DEFAULT, LV_COLOR_BLUE);
        lv_style_set_bg_grad_color(&style_player2, LV_STATE_DEFAULT, LV_COLOR_NAVY);
        lv_style_set_bg_grad_dir(&style_player2, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
        lv_style_set_border_color(&style_player2, LV_STATE_DEFAULT, LV_COLOR_WHITE);
        lv_style_set_border_width(&style_player2, LV_STATE_DEFAULT, 1);

        lv_style_init(&style_scoreboard);
        lv_style_set_text_font(&style_scoreboard, LV_STATE_DEFAULT, &Ubuntu_48px);
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
        lv_label_set_text_static(label, "Pong");
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
        offset += buttonSpacing;
    }

    log_d("Initializing game board");
    {
        bar_ball = lv_bar_create(gameplayTile, NULL);
        lv_obj_add_style(bar_ball, LV_OBJ_PART_MAIN, &style_ball);
        lv_obj_align(bar_ball, gameplayTile, LV_ALIGN_IN_TOP_LEFT, 0, 0 );
        lv_obj_set_click(bar_ball, false);
        lv_obj_set_size(bar_ball, BALL_WIDTH, BALL_HEIGHT);
	    lv_bar_set_anim_time(bar_ball, 50);

        bar_player1 = lv_bar_create(gameplayTile, NULL);
        lv_obj_add_style(bar_player1, LV_OBJ_PART_MAIN, &style_player1);
        lv_obj_align(bar_player1, gameplayTile, LV_ALIGN_IN_TOP_LEFT, 0, 0 );
        lv_obj_set_click(bar_player1, false);
        lv_obj_set_size(bar_player1, PLAYER_WIDTH, PLAYER_HEIGHT);
	    lv_obj_set_pos(bar_player1, PLAYER1_X, (FIELD_HEIGHT / 2) - (PLAYER_HEIGHT / 2));
	    lv_bar_set_anim_time(bar_player1, 50);

        bar_player2 = lv_bar_create(gameplayTile, NULL);
        lv_obj_add_style(bar_player2, LV_OBJ_PART_MAIN, &style_player2);
        lv_obj_align(bar_player2, gameplayTile, LV_ALIGN_IN_TOP_LEFT, 0, 0 );
        lv_obj_set_click(bar_player2, false);
        lv_obj_set_size(bar_player2, PLAYER_WIDTH, PLAYER_HEIGHT);
	    lv_obj_set_pos(bar_player2, PLAYER2_X, (FIELD_HEIGHT / 2) - (PLAYER_HEIGHT / 2));
	    lv_bar_set_anim_time(bar_player2, 50);

        label_scoreboard = lv_label_create(gameplayTile, NULL);
        lv_obj_add_style(label_scoreboard, LV_OBJ_PART_MAIN, &style_scoreboard);
        lv_obj_align(label_scoreboard, gameplayTile, LV_ALIGN_IN_TOP_LEFT, 0, 0 );
        lv_label_set_align(label_scoreboard, LV_LABEL_ALIGN_CENTER);
        lv_label_set_long_mode(label_scoreboard, LV_LABEL_LONG_CROP);
        lv_obj_set_click(label_scoreboard, false);
	    lv_obj_set_size(label_scoreboard, 240, 60);
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

        lv_obj_set_hidden(mOverlay, true);
    }

    ResetGame();
    
    pong_inited = true;
    log_d("Construction complete");
}

PongApp::~PongApp()
{
    mFirework.Stop();
    // LVGL Objects parented to the tiles should be deleted when the tiles are destroyed.
    FreeAppTiles();
    gameInstance = nullptr;
}

// Open from watch menu
void PongApp::OnLaunch()
{
    lv_tileview_set_tile_act(GetTileView(), 0, 0, LV_ANIM_OFF);
}

void PongApp::OnExitClicked()
{
    log_d("Exiting...");
    mFirework.Stop();
    // Pass along the message for differed deletion and return to main menu
    mParentIcon->OnExitClicked();
    FreeAppTiles();
}

void PongApp::Loop()
{
    if ( !pong_inited || !pong_active || mState != Playing ) return;

    UpdateBall();
    UpdatePlayer1();
    UpdatePlayer2();
    CheckCollision();

    lv_disp_trig_activity( NULL );
}

bool PongApp::CheckCollision()
{
    // check if ball hit p1 or p2
    if (ball_x <= 0 + PLAYER_WIDTH + (BALL_WIDTH / 2) && ball_y >= player1_y - (PLAYER_HEIGHT / 2) + (FIELD_HEIGHT / 2) && ball_y <= player1_y + (PLAYER_HEIGHT / 2) + (FIELD_HEIGHT / 2)) return BouncePlayer1();
    if (ball_x >= FIELD_WIDTH - PLAYER_WIDTH - (BALL_WIDTH / 2) && ball_y >= player2_y - (PLAYER_HEIGHT / 2) + (FIELD_HEIGHT / 2) && ball_y <= player2_y + (PLAYER_HEIGHT / 2) + (FIELD_HEIGHT / 2)) return BouncePlayer2();

    // check if ball hit the left or right wall
    if (ball_x <= 0 + (BALL_WIDTH / 2)) return ScorePlayer2();
    if (ball_x >= FIELD_WIDTH - (BALL_WIDTH / 2)) return ScorePlayer1();

    // check if ball hit top or bottom wall
    if (ball_y <= 0 + (BALL_HEIGHT / 2)) return BounceWallTop();
    if (ball_y >= FIELD_HEIGHT - (BALL_HEIGHT / 2)) return BounceWallBottom();

    return false;
}

/*
 * Reflect the ball off a surface whose outward normal points at base_degree.
 */
bool PongApp::TurnDegree(uint16_t base_degree)
{
    return SetDegree((base_degree * 2) - 180 - ball_degree);
}

/*
 * Take an absolute direction, for surfaces that pick the angle themselves.
 */
bool PongApp::SetDegree(int16_t degree)
{
    while (degree < 0) degree += 360;
    while (degree >= 360) degree -= 360;

    if (degree > 90 - BALL_ANGLE_MIN && degree <= 90) degree = 90 - BALL_ANGLE_MIN;
    else if (degree > 90 && degree < 90 + BALL_ANGLE_MIN) degree = 90 + BALL_ANGLE_MIN;
    else if (degree > 270 - BALL_ANGLE_MIN && degree <= 270) degree = 270 - BALL_ANGLE_MIN;
    else if (degree > 270 && degree < 270 + BALL_ANGLE_MIN) degree = 270 + BALL_ANGLE_MIN;

    log_d("Turn Degree from %d to %d", ball_degree, degree);
    ball_degree = degree;

    return true;
}

bool PongApp::BounceWallTop()
{
    if (ball_degree > 0 && ball_degree < 180) return false;
    log_d("Bounce Wall Top");

    if (ball_bounce > 0 && ball_bounce % 3 == 0) ball_speed++;

    TurnDegree(90);
    motor_vibe(1);

    return true;
}

bool PongApp::BounceWallBottom()
{
    if (ball_degree < 360 && ball_degree > 180) return false;
    log_d("Bounce Wall Bottom");

    if (ball_bounce > 0 && ball_bounce % 3 == 0) ball_speed++;

    TurnDegree(270);
    motor_vibe(1);

    return true;
}

bool PongApp::BouncePlayer1()
{
    if (ball_degree < 90 || ball_degree > 270) return false;

    // classic pong returns the ball by where it hit the paddle
    int16_t offset = map(ball_y, player1_y - (PLAYER_HEIGHT / 2) + (FIELD_HEIGHT / 2), player1_y + (PLAYER_HEIGHT / 2) + (FIELD_HEIGHT / 2), -PLAYER_ANGLE_MAX, PLAYER_ANGLE_MAX);
    offset = constrain(offset, -PLAYER_ANGLE_MAX, PLAYER_ANGLE_MAX);
    if (offset < 5 && offset > -5) offset = 0;

    log_d("Bounce Player 1 with offset %d", offset);
    SetDegree(offset);


    if (ball_bounce > 0) ball_speed++;
    ball_bounce++;

    sound_play_rtttl( SND_PONG_BOUNCE_P1, SOUND_TYPE_BACKGROUND );
    motor_vibe(3);

    return true;
}

bool PongApp::BouncePlayer2()
{
    if (ball_degree > 90 && ball_degree < 270) return false;

    int16_t offset = map(ball_y, player2_y - (PLAYER_HEIGHT / 2) + (FIELD_HEIGHT / 2), player2_y + (PLAYER_HEIGHT / 2) + (FIELD_HEIGHT / 2), -PLAYER_ANGLE_MAX, PLAYER_ANGLE_MAX);
    offset = constrain(offset, -PLAYER_ANGLE_MAX, PLAYER_ANGLE_MAX);
    if (offset < 5 && offset > -5) offset = 0;

    log_d("Bounce Player 2 with offset %d", offset);
    SetDegree(180 - offset);

    if (ball_bounce > 0) ball_speed++;
    ball_bounce++;

    sound_play_rtttl( SND_PONG_BOUNCE_P2, SOUND_TYPE_BACKGROUND );
    motor_vibe(3);

    return true;
}

bool PongApp::ScorePlayer1()
{
    log_d("Score Player 1");

    score_p1++;
    UpdateBoard();

    if (score_p1 >= PONG_WIN_SCORE)
    {
        EndGame(Won);
        return true;
    }

    ResetBall();

    sound_play_rtttl( SND_PONG_SCORE_P1, SOUND_TYPE_BACKGROUND );
    motor_vibe(10);

    return true;
}

bool PongApp::ScorePlayer2()
{
    log_d("Score Player 2");

    score_p2++;
    UpdateBoard();

    if (score_p2 >= PONG_WIN_SCORE)
    {
        EndGame(Lost);
        return true;
    }

    ResetBall();

    sound_play_rtttl( SND_PONG_SCORE_P2, SOUND_TYPE_BACKGROUND );
    motor_vibe(10);

    return true;
}

void PongApp::EndGame(GameState state)
{
    mState = state;
    lv_disp_trig_activity(NULL);

    char temp[32];
    if (state == Won)
    {
        snprintf(temp, sizeof(temp), "YOU WIN!\n%d : %d", score_p1, score_p2);
        lv_label_set_text(mResultLabel, temp);
        lv_obj_set_style_local_text_color(mResultLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_YELLOW);

        sound_play_rtttl(SND_PONG_WIN, SOUND_TYPE_BACKGROUND);
        motor_vibe(10);
        mFirework.Start(FIELD_WIDTH / 2, FIELD_HEIGHT / 2, LV_COLOR_ORANGE);
    }
    else
    {
        snprintf(temp, sizeof(temp), "CPU WINS\n%d : %d", score_p1, score_p2);
        lv_label_set_text(mResultLabel, temp);
        lv_obj_set_style_local_text_color(mResultLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

        sound_play_rtttl(SND_PONG_SCORE_P2, SOUND_TYPE_BACKGROUND);
        motor_vibe(10);
    }

    lv_obj_align(mResultLabel, mOverlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(mOverlay, false);
}

void PongApp::UpdateBall()
{
    if (ball_speed < BALL_SPEED_MIN) ball_speed = BALL_SPEED_MIN;
    if (ball_speed > BALL_SPEED_MAX) ball_speed = BALL_SPEED_MAX;

    ball_x = ball_x + ((float)ball_speed * cos((float)ball_degree * PI / 180));
    ball_y = ball_y + ((float)ball_speed * sin((float)ball_degree * PI / 180));

	lv_obj_set_pos(bar_ball, ball_x - (BALL_WIDTH / 2), ball_y - (BALL_HEIGHT / 2));
}

void PongApp::UpdatePlayer1()
{
    int16_t acc_x = 0;
    int16_t acc_y = 0;

    if ( !bma_get_accel_rotated( acc_x, acc_y ) ) return;

    // simple low pass
    control_acc_y += ( acc_y - control_acc_y ) / PLAYER_SMOOTHING;

    // set new position by accelerator
    int16_t new_position = control_acc_y * 0.2;

    if (new_position > 0 + PLAYER_BOUNDARY) new_position = 0 + PLAYER_BOUNDARY;
    if (new_position < 0 - PLAYER_BOUNDARY) new_position = 0 - PLAYER_BOUNDARY;

    // limit distance by maximum speed
    if (new_position < player1_y && player1_y - new_position > PLAYER_SPEED_MAX) new_position = player1_y - PLAYER_SPEED_MAX;
    if (new_position > player1_y && new_position - player1_y > PLAYER_SPEED_MAX) new_position = player1_y + PLAYER_SPEED_MAX;

    player1_y = new_position;
    lv_obj_set_pos(bar_player1, PLAYER1_X, PLAYER_BOUNDARY + player1_y);
}

void PongApp::UpdatePlayer2()
{
    if (cpu_reaction > 0) {
        cpu_reaction--;
    }
    else {
        // the faster the ball, the more often he answers late or misjudges it
        uint8_t chance = map((int16_t)ball_speed, (int16_t)BALL_SPEED_MIN, (int16_t)BALL_SPEED_MAX, CPU_ERROR_MIN, CPU_ERROR_MAX);
        cpu_reaction = random(0, 100) < chance ? CPU_REACTION * 2 : CPU_REACTION;

        cpu_aim -= cpu_aim / 4;
        cpu_aim += random(-4, 5);
        if (random(0, 100) < chance) {
            int16_t kick = random(12, 25);
            cpu_aim += random(0, 2) ? kick : 0 - kick;
        }
        cpu_aim = constrain(cpu_aim, 0 - CPU_AIM_MAX, 0 + CPU_AIM_MAX);

        float step_x = cos((float)ball_degree * PI / 180) * ball_speed;
        float step_y = sin((float)ball_degree * PI / 180) * ball_speed;
        float target = (FIELD_HEIGHT / 2);

        if (step_x > 0.5f) {
            float frames = (PLAYER2_X - ball_x) / step_x;
            if (frames > CPU_LOOKAHEAD) frames = CPU_LOOKAHEAD;

            target = ball_y + (step_y * frames);
            while (target < 0 || target > FIELD_HEIGHT) {
                if (target < 0) target = 0 - target;
                if (target > FIELD_HEIGHT) target = (2 * FIELD_HEIGHT) - target;
            }
        }

        cpu_target = target - (FIELD_HEIGHT / 2) + cpu_aim;
        cpu_target = constrain(cpu_target, 0 - PLAYER_BOUNDARY, 0 + PLAYER_BOUNDARY);
    }

    // accelerate towards the target and bleed off speed
    int16_t distance = cpu_target - player2_y;
    cpu_velocity += constrain(distance, 0 - CPU_ACCEL, 0 + CPU_ACCEL) - (cpu_velocity / CPU_DAMPING);
    cpu_velocity = constrain(cpu_velocity, 0 - PLAYER_SPEED_MAX, 0 + PLAYER_SPEED_MAX);

    // set new position by ball position
    int16_t new_position = player2_y + cpu_velocity;
    if (new_position > 0 + PLAYER_BOUNDARY) { new_position = 0 + PLAYER_BOUNDARY; cpu_velocity = 0; }
    if (new_position < 0 - PLAYER_BOUNDARY) { new_position = 0 - PLAYER_BOUNDARY; cpu_velocity = 0; }

    player2_y = new_position;
	lv_obj_set_pos(bar_player2, PLAYER2_X, PLAYER_BOUNDARY + player2_y);
}

void PongApp::UpdateBoard()
{
    log_d("Updating Board to %d : %d", score_p1, score_p2);

    char temp[10];
    snprintf(temp, sizeof(temp), "%d : %d", score_p1, score_p2);
    lv_label_set_text(label_scoreboard, temp);
    lv_event_send_refresh(label_scoreboard);
}

void PongApp::ResetBall()
{
    log_d("Resetting Ball...");

    ball_speed = BALL_SPEED_MIN;
    ball_bounce = 0;

    SetDegree(random(-45, 45));

    ball_x = (FIELD_WIDTH / 2);
    ball_y = (FIELD_WIDTH / 2);

	lv_obj_set_pos(bar_ball, ball_x - (BALL_WIDTH / 2), ball_y - (BALL_HEIGHT / 2));
}

void PongApp::ResetBoard()
{
    log_d("Resetting Board to 0 : 0");
    score_p1 = 0;
    score_p2 = 0;
    UpdateBoard();
}

void PongApp::ResetPlayer1()
{
    log_d("Resetting Player 1");
    player1_y = 0;
}

void PongApp::ResetPlayer2()
{
    log_d("Resetting Player 2");
    player2_y = 0;
    cpu_velocity = 0;
    cpu_target = 0;
    cpu_aim = 0;
    cpu_reaction = 0;
}

void PongApp::ResetGame()
{
    mFirework.Stop();
    lv_obj_set_hidden(mOverlay, true);
    mState = Playing;

    ResetBall();
    ResetBoard();
    ResetPlayer1();
    ResetPlayer2();
}

void PongApp::OnMenuClicked(MenuItem item)
{
    switch (item)
    {
        case Reset:
            ResetGame();
            lv_tileview_set_tile_act(GetTileView(), 1, 0, LV_ANIM_ON);
            pong_active = true;
            break;
        case Exit:
            OnExitClicked();
            pong_active = false;
            break;
        default:
            log_e("Unknown menu command %d", item);
    }
}

void PongApp::OnOverlayClicked()
{
    ResetGame();
}

void PongApp::OnTileChanged()
{
    lv_coord_t x;
    lv_coord_t y;
    lv_tileview_get_tile_act(GetTileView(), &x, &y);
    log_d("Tile changed to %d, %d", x, y);

    if (x == 1) pong_active = true;
    else pong_active = false;
}

void PongApp::OnStandby()
{
    pong_active = false;
}