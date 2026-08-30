/****************************************************************************
 *   Aug 30 12:00:00 2026
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
#ifndef _ASSIST_QR_H
    #define _ASSIST_QR_H

    #include "gui/widget_factory.h"

    #define ASSIST_QR_SIZE          200                             /** @brief canvas edge in pixel */
    #define ASSIST_QR_VERSION_MAX   10                              /** @brief 213 bytes at ecc medium, 408 bytes per buffer */

    /**
     * @brief a true color canvas, black on white, without a buffer yet
     *
     * lv_qrcode_update() is not usable here, it puts two 3918 byte buffers on the
     * stack and the arduino loop task, where lvgl runs, only has 8192
     */
    lv_obj_t *assist_qr_create( lv_obj_t *parent );
    /**
     * @brief take the 78 kb buffer from psram, idempotent
     */
    bool assist_qr_alloc( lv_obj_t *qr );
    /**
     * @brief give it back, the canvas hides until the next alloc
     */
    void assist_qr_free( lv_obj_t *qr );
    /**
     * @brief paint it white
     */
    void assist_qr_clear( lv_obj_t *qr );
    /**
     * @brief draw text, false when it does not fit into ASSIST_QR_VERSION_MAX
     */
    bool assist_qr_update( lv_obj_t *qr, const char *text );

#endif // _ASSIST_QR_H
