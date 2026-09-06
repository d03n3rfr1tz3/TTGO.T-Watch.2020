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

/* Celebration effect shared by the games: a few bursts of sparks flying out
   of a given point, fading while they travel.

   USE:
   * Hold an instance in the game app and call Create() once the parent object exists.
   * Call Start() to celebrate. The sparks drop themselves when the parent deletes
     them, so Stop() and the destructor stay safe at any point.
*/

#pragma once

class GameFirework
{
private:
    static constexpr int NUM_SPARKS = 16;
    static constexpr int SPARK_SIZE = 8;

    lv_obj_t *mSparks[NUM_SPARKS] = {0};
    lv_task_t *mTask = 0;
    int mBurstsLeft = 0;
    lv_coord_t mCx = 0;
    lv_coord_t mCy = 0;
    lv_color_t mAccent = LV_COLOR_WHITE;

public:
    ~GameFirework();

    /* Allocate the spark objects. The parent keeps ownership of them. */
    void Create(lv_obj_t *parent);

    /* Fire bursts from (cx, cy), mixing white and yellow sparks with an accent color. */
    void Start(lv_coord_t cx, lv_coord_t cy, lv_color_t accent, int bursts = 3);

    /* Cancel the running effect. Safe to call more than once. */
    void Stop();

    /* Emit one burst, called by the internal task. */
    void OnTick();

    /* A spark was deleted by its parent, called by the spark event callback. */
    void OnSparkDeleted(lv_obj_t *spark);
};
