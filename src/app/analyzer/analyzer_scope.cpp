/****************************************************************************
 *   Aug 22 23:00:00 2026
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

#include "analyzer_app.h"
#include "analyzer_dsp.h"
#include "analyzer_scope.h"

#include "gui/mainbar/mainbar.h"
#include "gui/widget_factory.h"

static lv_obj_t *analyzer_scope_tile = NULL;
static lv_obj_t *analyzer_scope_label = NULL;

void analyzer_scope_setup( uint32_t tile_num ) {

    analyzer_scope_tile = mainbar_get_tile_obj( tile_num );

    analyzer_scope_label = wf_add_label( analyzer_scope_tile, "scope" );
    lv_label_set_align( analyzer_scope_label, LV_LABEL_ALIGN_CENTER );
    lv_obj_align( analyzer_scope_label, analyzer_scope_tile, LV_ALIGN_CENTER, 0, 0 );

    analyzer_app_add_footer( analyzer_scope_tile );

    lv_tileview_add_element( analyzer_scope_tile, analyzer_scope_label );
}
