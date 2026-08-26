/****************************************************************************
 *   Tu May 22 21:23:51 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
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
#include "sound_settings.h"

#include "gui/mainbar/mainbar.h"
#include "gui/mainbar/setup_tile/setup_tile.h"
#include "gui/statusbar.h"
#include "gui/setup.h"
#include "gui/widget_factory.h"
#include "gui/widget_styles.h"

#include "gui/sound/piep.h"
#include "hardware/sound.h"
#include "hardware/motor.h"


icon_t *sound_setup_icon = NULL;

lv_obj_t *sound_settings_tile = NULL;
lv_obj_t *sound_settings_tile_2 = NULL;
lv_style_t sound_settings_style;
uint32_t sound_tile_num;

lv_obj_t *sound_volume_slider = NULL;
lv_obj_t *sound_volume_slider_label = NULL;
lv_obj_t *sound_enable = NULL;
lv_obj_t *sound_icon = NULL;
lv_obj_t *sound_vibe_onoff = NULL;
lv_obj_t *sound_silence_onoff = NULL;
lv_obj_t *sound_silence_start_hour = NULL;
lv_obj_t *sound_silence_start_minute = NULL;
lv_obj_t *sound_silence_end_hour = NULL;
lv_obj_t *sound_silence_end_minute = NULL;
lv_obj_t *sound_test_audio_btn = NULL;
lv_obj_t *sound_test_rtttl_btn = NULL;

static const char sound_hour_options[] = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
static const char sound_minute_options[] = "00\n05\n10\n15\n20\n25\n30\n35\n40\n45\n50\n55";

LV_IMG_DECLARE(sound_64px);
LV_IMG_DECLARE(sound_32px);
LV_IMG_DECLARE(sound_mute_32px);

static void enter_sound_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void exit_sound_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void down_sound_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void sound_volume_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void sound_enable_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void sound_mute_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void sound_vibe_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void sound_silence_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void sound_silence_time_setup_event_cb( lv_obj_t * obj, lv_event_t event );
static void sound_test_setup_event_cb( lv_obj_t * obj, lv_event_t event );

static lv_obj_t *sound_add_time_row( lv_obj_t *parent, const char *text, int hour, int minute, lv_obj_t **hour_list, lv_obj_t **minute_list );

bool sound_soundctl_event_cb( EventBits_t event, void *arg );

void sound_settings_tile_setup( void ) {
    if ( !sound_get_available() ) {
        return;
    }
    // get an app tile and copy mainstyle
    sound_tile_num = mainbar_add_setup_tile( 1, 2, "sound setup" );
    sound_settings_tile = mainbar_get_tile_obj( sound_tile_num );
    sound_settings_tile_2 = mainbar_get_tile_obj( sound_tile_num + 1 );

    lv_style_copy( &sound_settings_style, ws_get_setup_tile_style() );
    lv_obj_add_style( sound_settings_tile, LV_OBJ_PART_MAIN, &sound_settings_style );
    lv_obj_add_style( sound_settings_tile_2, LV_OBJ_PART_MAIN, &sound_settings_style );

    sound_setup_icon = setup_register( "sound", &sound_64px, enter_sound_setup_event_cb );
    setup_hide_indicator( sound_setup_icon );

    lv_obj_t *header = wf_add_settings_header( sound_settings_tile, "sound settings", exit_sound_setup_event_cb );
    lv_obj_align( header, sound_settings_tile, LV_ALIGN_IN_TOP_LEFT, 10, STATUSBAR_HEIGHT + 10 );

    lv_obj_t *down_btn = wf_add_down_button( sound_settings_tile, down_sound_setup_event_cb, SETUP_STYLE );
    lv_obj_align( down_btn, sound_settings_tile, LV_ALIGN_IN_TOP_RIGHT, -10, STATUSBAR_HEIGHT + 10 );

    lv_obj_t *vibe_cont = wf_add_labeled_switch( sound_settings_tile, "enable vibe", &sound_vibe_onoff, motor_get_vibe_config(), sound_vibe_setup_event_cb, ws_get_setup_tile_style() );
    lv_obj_align( vibe_cont, header, LV_ALIGN_OUT_BOTTOM_MID, 0, 5 );

    lv_obj_t *sound_enable_cont = wf_add_labeled_switch( sound_settings_tile, "enable sound", &sound_enable, sound_get_enabled_config(), sound_enable_setup_event_cb, ws_get_setup_tile_style() );
    lv_obj_align( sound_enable_cont, vibe_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 5 );

    lv_obj_t *sound_volume_cont = lv_obj_create( sound_settings_tile, NULL );
    lv_obj_set_size( sound_volume_cont, lv_disp_get_hor_res( NULL ) , 58 );
    lv_obj_add_style( sound_volume_cont, LV_OBJ_PART_MAIN, &sound_settings_style  );
    lv_obj_align( sound_volume_cont, sound_enable_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 0 );
    sound_volume_slider = lv_slider_create( sound_volume_cont, NULL );
    lv_obj_add_protect( sound_volume_slider, LV_PROTECT_CLICK_FOCUS);
    lv_obj_add_style( sound_volume_slider, LV_SLIDER_PART_INDIC, ws_get_slider_style() );
    lv_obj_add_style( sound_volume_slider, LV_SLIDER_PART_KNOB, ws_get_slider_style() );
    lv_slider_set_range( sound_volume_slider, 1, 100 );
    lv_obj_set_size(sound_volume_slider, lv_disp_get_hor_res( NULL ) - 100 , 10 );
    lv_obj_align( sound_volume_slider, sound_volume_cont, LV_ALIGN_IN_TOP_RIGHT, -30, 10 );
    lv_obj_set_event_cb( sound_volume_slider, sound_volume_setup_event_cb );
    sound_volume_slider_label = lv_label_create( sound_volume_cont, NULL );
    lv_obj_add_style( sound_volume_slider_label, LV_OBJ_PART_MAIN, &sound_settings_style  );
    lv_label_set_text( sound_volume_slider_label, "");
    lv_obj_align( sound_volume_slider_label, sound_volume_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, -5 );
    sound_icon = lv_img_create( sound_volume_cont, NULL );
    lv_img_set_src( sound_icon, sound_get_enabled_config() ? &sound_32px : &sound_mute_32px );
    lv_obj_set_click( sound_icon, true );
    lv_obj_set_ext_click_area( sound_icon, THEME_PADDING, THEME_PADDING, THEME_PADDING, THEME_PADDING );
    lv_obj_set_event_cb( sound_icon, sound_mute_setup_event_cb );
    lv_obj_align( sound_icon, sound_volume_cont, LV_ALIGN_IN_LEFT_MID, 15, 0 );

    lv_slider_set_value( sound_volume_slider, sound_get_volume_config(), LV_ANIM_OFF );
    char temp[16]="";
    snprintf( temp, sizeof( temp ), "volume %d", lv_slider_get_value( sound_volume_slider ) );
    lv_label_set_text( sound_volume_slider_label, temp );
    lv_obj_align( sound_volume_slider_label, sound_volume_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 15 );

    lv_tileview_add_element( sound_settings_tile, vibe_cont );
    lv_tileview_add_element( sound_settings_tile, sound_enable_cont );
    lv_tileview_add_element( sound_settings_tile, sound_volume_cont );

    lv_obj_t *header_2 = wf_add_settings_header( sound_settings_tile_2, "sound settings", exit_sound_setup_event_cb );
    lv_obj_align( header_2, sound_settings_tile_2, LV_ALIGN_IN_TOP_LEFT, 10, STATUSBAR_HEIGHT + 10 );

    lv_obj_t *silence_cont = wf_add_labeled_switch( sound_settings_tile_2, "silence timeframe", &sound_silence_onoff, sound_get_silence_config(), sound_silence_setup_event_cb, SETUP_STYLE );
    lv_obj_align( silence_cont, header_2, LV_ALIGN_OUT_BOTTOM_MID, 0, 3 );

    int hour = 0;
    int minute = 0;
    sound_get_silence_start_config( &hour, &minute );
    lv_obj_t *silence_start_cont = sound_add_time_row( sound_settings_tile_2, "from", hour, minute, &sound_silence_start_hour, &sound_silence_start_minute );
    lv_obj_align( silence_start_cont, silence_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 0 );

    sound_get_silence_end_config( &hour, &minute );
    lv_obj_t *silence_end_cont = sound_add_time_row( sound_settings_tile_2, "to", hour, minute, &sound_silence_end_hour, &sound_silence_end_minute );
    lv_obj_align( silence_end_cont, silence_start_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 0 );

    sound_test_audio_btn = wf_add_button( sound_settings_tile_2, "test audio", 108, 32, sound_test_setup_event_cb );
    lv_obj_align( sound_test_audio_btn, sound_settings_tile_2, LV_ALIGN_IN_BOTTOM_LEFT, 8, -8 );

    sound_test_rtttl_btn = wf_add_button( sound_settings_tile_2, "test rtttl", 108, 32, sound_test_setup_event_cb );
    lv_obj_align( sound_test_rtttl_btn, sound_settings_tile_2, LV_ALIGN_IN_BOTTOM_RIGHT, -8, -8 );

    lv_tileview_add_element( sound_settings_tile_2, silence_cont );
    lv_tileview_add_element( sound_settings_tile_2, silence_start_cont );
    lv_tileview_add_element( sound_settings_tile_2, silence_end_cont );
    lv_tileview_add_element( sound_settings_tile_2, sound_test_audio_btn );
    lv_tileview_add_element( sound_settings_tile_2, sound_test_rtttl_btn );

    sound_register_cb( SOUNDCTL_ENABLED | SOUNDCTL_VOLUME, sound_soundctl_event_cb, "sound settings");
}

static lv_obj_t *sound_add_time_row( lv_obj_t *parent, const char *text, int hour, int minute, lv_obj_t **hour_list, lv_obj_t **minute_list ) {
    lv_obj_t *container = lv_obj_create( parent, NULL );
    lv_obj_set_size( container, lv_disp_get_hor_res( NULL ), 32 );
    lv_obj_add_style( container, LV_OBJ_PART_MAIN, &sound_settings_style );

    lv_obj_t *label = wf_add_label( container, text, SETUP_STYLE );
    lv_obj_align( label, container, LV_ALIGN_IN_LEFT_MID, 2 * THEME_PADDING, 0 );

    *minute_list = wf_add_list( container, sound_minute_options, ws_get_setup_dropdown_style() );
    lv_obj_set_width( *minute_list, 64 );
    lv_dropdown_set_selected( *minute_list, ( minute % 60 ) / 5 );
    lv_obj_set_event_cb( *minute_list, sound_silence_time_setup_event_cb );
    lv_obj_align( *minute_list, container, LV_ALIGN_IN_RIGHT_MID, -2 * THEME_PADDING, 0 );

    lv_obj_t *colon = wf_add_label( container, ":", SETUP_STYLE );
    lv_obj_align( colon, *minute_list, LV_ALIGN_OUT_LEFT_MID, -4, 0 );

    *hour_list = wf_add_list( container, sound_hour_options, ws_get_setup_dropdown_style() );
    lv_obj_set_width( *hour_list, 64 );
    lv_dropdown_set_selected( *hour_list, hour % 24 );
    lv_obj_set_event_cb( *hour_list, sound_silence_time_setup_event_cb );
    lv_obj_align( *hour_list, colon, LV_ALIGN_OUT_LEFT_MID, -4, 0 );

    return( container );
}

static void enter_sound_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_to_tilenumber( sound_tile_num, LV_ANIM_OFF );
                                        break;
    }

}

static void exit_sound_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_jump_back();
                                        sound_save_config();
                                        break;
    }
}

static void down_sound_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):       mainbar_slide_to_tilenumber( sound_tile_num + 1, LV_ANIM_ON );
                                        break;
    }
}

static void sound_vibe_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ):     motor_set_vibe_config( lv_slider_get_value( obj ) );
                                            break;
    }
}

static void sound_enable_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ):     
                                            sound_set_enabled_config( lv_switch_get_state( obj ) );
                                            if (sound_get_enabled_config()) {
                                                lv_img_set_src( sound_icon, &sound_32px );
                                            }
                                            else {
                                                lv_img_set_src( sound_icon, &sound_mute_32px );
                                            }
                                            break;
    }
}

static void sound_mute_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):           sound_set_enabled_config( !sound_get_enabled_config() );
                                            break;
    }
}

static void sound_silence_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ):     sound_set_silence_config( lv_switch_get_state( obj ) );
                                            break;
    }
}

static void sound_silence_time_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ):
                                            if ( obj == sound_silence_start_hour || obj == sound_silence_start_minute ) {
                                                sound_set_silence_start_config( lv_dropdown_get_selected( sound_silence_start_hour ), lv_dropdown_get_selected( sound_silence_start_minute ) * 5 );
                                            }
                                            else {
                                                sound_set_silence_end_config( lv_dropdown_get_selected( sound_silence_end_hour ), lv_dropdown_get_selected( sound_silence_end_minute ) * 5 );
                                            }
                                            break;
    }
}

static void sound_test_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_CLICKED ):           if ( obj != sound_test_audio_btn ) {
                                                sound_play_rtttl( SND_SPEAKER_TEST, SOUND_TYPE_FOREGROUND );
                                            }
                                            else {
                                                char filename[ 64 ] = "";
                                                if ( sound_get_random_spiffs_mp3( filename, sizeof( filename ) ) )
                                                    sound_play_spiffs_mp3( filename, SOUND_TYPE_FOREGROUND );
                                                else
                                                    sound_play_progmem_wav( piep_wav, piep_wav_len, SOUND_TYPE_FOREGROUND );
                                            }
                                            break;
    }
}

static void sound_volume_setup_event_cb( lv_obj_t * obj, lv_event_t event ) {
    switch( event ) {
        case( LV_EVENT_VALUE_CHANGED ):     
                                            char temp[16]="";
                                            snprintf( temp, sizeof( temp ), "volume %d", lv_slider_get_value( sound_volume_slider ) );
                                            lv_label_set_text( sound_volume_slider_label, temp );
                                            lv_obj_align( sound_volume_slider_label, sound_volume_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 15 );
                                            sound_set_volume_config( lv_slider_get_value( obj ) );
                                            break;
    }
}

bool sound_soundctl_event_cb( EventBits_t event, void *arg ) {
    switch( event ) {
        case SOUNDCTL_ENABLED:  
            if ( *(bool*)arg ) {
                lv_switch_on( sound_enable, LV_ANIM_OFF);
                lv_img_set_src( sound_icon, &sound_32px );
            }
            else {
                lv_switch_off( sound_enable, LV_ANIM_OFF);
                lv_img_set_src( sound_icon, &sound_mute_32px );
            }
            break;
        case SOUNDCTL_VOLUME:
            lv_slider_set_value( sound_volume_slider, *(int8_t*)arg, LV_ANIM_OFF );
            char temp[16]="";
            snprintf( temp, sizeof( temp ), "volume %d", lv_slider_get_value( sound_volume_slider ) );
            lv_label_set_text( sound_volume_slider_label, temp );
            lv_obj_align( sound_volume_slider_label, sound_volume_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 15 );
            break;
    }
    return( true );
}

uint32_t sound_get_setup_tile_num( void ) {
    return ( sound_tile_num );
}
