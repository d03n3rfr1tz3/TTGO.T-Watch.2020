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

#include "gui/app.h"
#include "gui/mainbar/mainbar.h"
#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/statusbar.h"
#include "hardware/display.h"
#include "hardware/motor.h"

#include "breakout_game.h"
#include "breakout_app.h"

// Use this icon image
LV_IMG_DECLARE(breakout_64px);

// The one and only.
static BreakoutIcon iconInstance;
lv_task_t * _breakout_app_task;

/*
 * automatic register the app setup function with explicit call in main.cpp
 */
static int registed = app_autocall_function( &breakout_game_setup, APP_PRIO( APP_GROUP_GAMES, 2 ) );           /** @brief app autocall function */

void breakout_app_task( lv_task_t * task )
{
    if ( !iconInstance.IsActive ) return;
    iconInstance.Loop();
}

void breakout_game_setup()
{
    /*
     * check if app already registered for autocall
     */
    if( !registed ) {
        return;
    }

    breakout_app_setup();
    iconInstance.RegisterAppIcon();
    _breakout_app_task = lv_task_create( breakout_app_task, 50, LV_TASK_PRIO_HIGH, NULL );
}

static void startGame(struct _lv_obj_t *obj, lv_event_t event)
{
    switch (event)
    {
        case (LV_EVENT_CLICKED):
            iconInstance.OnStartClicked();
            break;
    }
}

BreakoutIcon::BreakoutIcon()
{
    pAppname = "Breakout";
    pMenuIcon = &breakout_64px;
    pStartFunction = startGame;
}

void BreakoutIcon::OnStartClicked()
{
    motor_vibe(1);

    if(!mGameInstance)
    {
        log_d("Creating game instance.");
        mGameInstance = std::unique_ptr<BreakoutApp>(new BreakoutApp(this));
    }

    log_d("Launching game instance.");
    mGameInstance->OnLaunch();
    IsActive = true;
}

static void DelayedRelease(void* param)
{
    BreakoutIcon *me = reinterpret_cast<BreakoutIcon *>(param);

    me->DoDelayedRelease();
}

void BreakoutIcon::OnExitClicked()
{
    motor_vibe(1);
    ReturnToMenu();
    IsActive = false;

    /* Delay this until the next task handler cycle */
    log_d("Queuing async release");
    lv_async_call(DelayedRelease, this);
}

void BreakoutIcon::DoDelayedRelease()
{
    log_d("Triggering async release");
    mGameInstance.reset();
}

void BreakoutIcon::Loop()
{
    if (!mGameInstance) return;

    mGameInstance->Loop();
}
