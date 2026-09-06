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
#ifndef _MOTION_H
    #define _MOTION_H

    #include "callback.h"
    #include "hardware/config/bmaconfig.h"
    
    #define BMACTL_EVENT_INT            _BV(0)              /** @brief event mask for bma interrupt */
    #define BMACTL_DOUBLECLICK          _BV(1)              /** @brief event mask for an doubleclick event */
    #define BMACTL_STEPCOUNTER          _BV(2)              /** @brief event mask for an stepcounter update event, callback arg is (uint32*) */
    #define BMACTL_STEPCOUNTER_RESET    _BV(3)              /** @brief event mask for an stepcounter reset event */
    #define BMACTL_TILT                 _BV(4)              /** @brief event mask for an tilt event */
    /**
     * @brief setup bma activity measurement
     */
    void bma_setup( void );
    /**
     * @brief put bma into standby, depending on ther config
     */
    void bma_standby( void );
    /**
     * @brief wakeup activity measurement
     */
    void bma_wakeup( void );
    /**
     * @brief reload config
     */
    void bma_reload_settings( void );
    /**
     * @brief save the config structure to SPIFFS
     */
    void bma_save_config( void );
    /**
     * @brief read the config structure from SPIFFS
     */
    void bma_read_config( void );
    /**
     * @brief get config
     * 
     * @param   config     configitem: BMA_STEPCOUNTER, BMA_DOUBLECLICK or BMA_CONFIG_NUM
     */
    bool bma_get_config( int config );
    /**
     * @brief set config
     * 
     * @param   config     configitem: BMA_STEPCOUNTER, BMA_DOUBLECLICK or BMA_CONFIG_NUM
     * @param   bool    true or false
     */
    void bma_set_config( int config, bool enable );
    /**
     * @brief get axis config
     *
     * @param   config     configitem: BMA_AXIS_SWAP_XY, BMA_AXIS_INVERT_X or BMA_AXIS_INVERT_Y
     */
    bool bma_get_axis_config( int config );
    /**
     * @brief set axis config, takes effect right away
     *
     * @param   config     configitem: BMA_AXIS_SWAP_XY, BMA_AXIS_INVERT_X or BMA_AXIS_INVERT_Y
     * @param   bool    true or false
     */
    void bma_set_axis_config( int config, bool enable );
    /**
     * @brief   rotate bma axis
     * 
     * @param   rotation on degree
     */
    void bma_set_rotate_tilt( uint32_t rotation );
    /**
     * @brief registers a callback function which is called on a corresponding event
     * 
     * @param   event           possible values: BMACTL_DOUBLECLICK, BMACTL_STEPCOUNTER and BMACTL_TILT
     * @param   callback_func   pointer to the callback function
     * @param   id              program id
     * 
     * @return  true if success, false if failed
     */
    bool bma_register_cb( EventBits_t event, CALLBACK_FUNC callback_func, const char *id );
    /**
     * @brief return the current step counter value
     *
     * @return steps from the stepcounter as uint32_t value
     */
    uint32_t bma_get_stepcounter( void );
    /**
     * @brief reset the stepcounter value
     */
    void bma_reset_stepcounter( void );
    /**
     * @brief read the current acceleration
     *
     * @param x     raw value of the x axis
     * @param y     raw value of the y axis
     * @param z     raw value of the z axis
     *
     * @return true if the board has an axis sensor and the values are valid
     */
    bool bma_get_accel( int16_t &x, int16_t &y, int16_t &z );
    /**
     * @brief read the current acceleration mapped onto the display orientation
     *
     * @param x     acceleration along the screen x axis, positive to the right
     * @param y     acceleration along the screen y axis, positive downwards
     *
     * @return true if the board has an axis sensor and the values are valid
     */
    bool bma_get_accel_rotated( int16_t &x, int16_t &y );

#endif // _MOTION_H
