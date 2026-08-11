#ifndef _RESOLVE_OWM_ICON_H
    #define _RESOLVE_OWM_ICON_H

    #include <stddef.h>
    #include <stdint.h>

    struct owm_icon {
        char iconname[8];
        const void *icon;
    };

    const void * resolve_owm_icon( char * iconname );

    /**
     * @brief build an owm icon name like "10n" from an owm condition code
     *
     * @param code      owm condition code, e.g. 802
     * @param night     use the night variant
     * @param iconname  target buffer
     * @param len       size of the target buffer
     */
    void resolve_owm_iconname( uint16_t code, bool night, char *iconname, size_t len );

#endif // _RESOLVE_OWM_ICON_H