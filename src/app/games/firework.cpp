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

#include "hardware/motor.h"

#include "firework.h"

static void FireworkTask(lv_task_t *task)
{
    static_cast<GameFirework *>(task->user_data)->OnTick();
}

static void SetSparkOpa(void *obj, lv_anim_value_t value)
{
    lv_obj_set_style_local_bg_opa((lv_obj_t *)obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, value);
}

static void HideSpark(lv_anim_t *anim)
{
    lv_obj_set_hidden((lv_obj_t *)anim->var, true);
}

GameFirework::~GameFirework()
{
    if (mTask)
    {
        lv_task_del(mTask);
        mTask = 0;
    }
}

void GameFirework::Create(lv_obj_t *parent)
{
    for (int i = 0; i < NUM_SPARKS; i++)
    {
        mSparks[i] = lv_obj_create(parent, NULL);
        lv_obj_set_size(mSparks[i], SPARK_SIZE, SPARK_SIZE);
        lv_obj_set_click(mSparks[i], false);
        lv_obj_reset_style_list(mSparks[i], LV_OBJ_PART_MAIN);
        lv_obj_set_style_local_radius(mSparks[i], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
        lv_obj_set_style_local_border_width(mSparks[i], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
        lv_obj_set_hidden(mSparks[i], true);
    }
}

void GameFirework::Start(lv_coord_t cx, lv_coord_t cy, lv_color_t accent, int bursts)
{
    if (!mSparks[0])
        return;

    mCx = cx;
    mCy = cy;
    mAccent = accent;
    mBurstsLeft = bursts;

    if (!mTask)
        mTask = lv_task_create(FireworkTask, 400, LV_TASK_PRIO_MID, this);

    OnTick();
}

void GameFirework::OnTick()
{
    if (mBurstsLeft <= 0)
    {
        Stop();
        return;
    }
    mBurstsLeft--;

    lv_anim_path_t path = {0};
    lv_anim_path_set_cb(&path, lv_anim_path_ease_out);

    const lv_coord_t x0 = mCx - (SPARK_SIZE / 2);
    const lv_coord_t y0 = mCy - (SPARK_SIZE / 2);

    for (int i = 0; i < NUM_SPARKS; i++)
    {
        lv_obj_t *spark = mSparks[i];
        const float angle = (float)i * 2 * PI / NUM_SPARKS;
        const int radius = 50 + (rand() % 30);

        lv_obj_set_style_local_bg_color(spark, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, (i % 3 == 0) ? LV_COLOR_WHITE : ((i % 3 == 1) ? LV_COLOR_YELLOW : mAccent));
        lv_obj_set_style_local_bg_opa(spark, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
        lv_obj_set_pos(spark, x0, y0);
        lv_obj_set_hidden(spark, false);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, spark);
        lv_anim_set_time(&anim, 500);
        lv_anim_set_path(&anim, &path);

        lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_x);
        lv_anim_set_values(&anim, x0, x0 + (lv_coord_t)(radius * cos(angle)));
        lv_anim_start(&anim);

        lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_set_values(&anim, y0, y0 + (lv_coord_t)(radius * sin(angle)));
        lv_anim_start(&anim);

        lv_anim_set_exec_cb(&anim, SetSparkOpa);
        lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_ready_cb(&anim, HideSpark);
        lv_anim_start(&anim);
    }

    motor_vibe(2);
}

void GameFirework::Stop()
{
    if (mTask)
    {
        lv_task_del(mTask);
        mTask = 0;
    }
    mBurstsLeft = 0;

    // the animations must not outlive the objects they drive
    for (lv_obj_t *spark : mSparks)
    {
        if (!spark)
            continue;
        lv_anim_del(spark, NULL);
        lv_obj_set_hidden(spark, true);
    }
}
