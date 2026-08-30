/****************************************************************************
 *   Aug 3 12:17:11 2020
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
#include "uri_load.h"
#include "utils/alloc.h"

#ifdef NATIVE_64BIT
    #include "utils/logging.h"
    #include "utils/millis.h"
    #include <stdio.h>
    #include <stdlib.h>
    #include <iostream>
    #include <fstream>
    #include <string.h>
    #include <unistd.h>
    #include <sys/types.h>
    #include <pwd.h>
    #include <string.h>
    #include <string>
    #include <curl/curl.h>

    /**
     * @brief curl memory control structure
     */
    struct MemoryStruct {
        char *memory;
        size_t size;
    };
    /**
     * @brief curl memory write callback function
     */
    static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
        size_t realsize = size * nmemb;
        struct MemoryStruct *mem = (struct MemoryStruct *)userp;
        
        char *ptr = (char*)REALLOC(mem->memory, mem->size + realsize + 1);
        if(!ptr) {
            /* out of memory! */
            log_e("not enough memory (realloc returned NULL)");
            return 0;
        }
        
        mem->memory = ptr;
        memcpy(&(mem->memory[mem->size]), contents, realsize);
        mem->size += realsize;
        mem->memory[mem->size] = 0;
        
        return realsize;
    }

    using namespace std;
    #define String string
#else
    #include <HTTPClient.h>
    #include <SPIFFS.h>
#endif

uri_load_dsc_t *uri_load_create_dsc( void );
void uri_load_set_filename_from_uri( uri_load_dsc_t *uri_load_dsc, const char *uri );
void uri_load_set_url_from_uri( uri_load_dsc_t *uri_load_dsc, const char *uri );
uri_load_dsc_t *uri_load_file_to_ram( uri_load_dsc_t *uri_load_dsc );
uri_load_dsc_t *uri_load_http_to_ram( uri_load_dsc_t *uri_load_dsc, uint8_t depth );
uri_load_dsc_t *uri_load_https_to_ram( uri_load_dsc_t *uri_load_dsc, uint8_t depth );
static uri_load_dsc_t *uri_load_to_ram_depth( const char *uri, progress_cb_t *progresscb, uint8_t depth );

#ifndef NATIVE_64BIT
    /**
     * @brief grow the data buffer, doubling, the extra byte holds the string terminator
     *
     * @param   uri_load_dsc    descriptor to grow
     * @param   alloc           current allocation, updated on success
     * @param   need            bytes about to be written
     *
     * @return  true if success
     */
    static bool uri_load_reserve( uri_load_dsc_t *uri_load_dsc, uint32_t *alloc, uint32_t need ) {
        if ( uri_load_dsc->size + need > URI_LOAD_MAX_STREAM_SIZE ) {
            URI_LOAD_ERROR_LOG("download exceeds %d bytes", URI_LOAD_MAX_STREAM_SIZE );
            return( false );
        }

        if ( uri_load_dsc->size + need + 1 <= *alloc ) {
            return( true );
        }

        uint32_t new_alloc = *alloc ? *alloc : URI_BLOCK_SIZE * 4;
        while ( new_alloc < uri_load_dsc->size + need + 1 ) {
            new_alloc *= 2;
        }

        uint8_t *ptr = (uint8_t*)REALLOC( uri_load_dsc->data, new_alloc );
        if ( !ptr ) {
            URI_LOAD_ERROR_LOG("data alloc failed, %d bytes", new_alloc );
            return( false );
        }

        uri_load_dsc->data = ptr;
        *alloc = new_alloc;

        return( true );
    }
    /**
     * @brief read one crlf terminated line, gives the cpu away while it waits
     *
     * @param   stream      open stream
     * @param   line        buffer for the line, always terminated
     * @param   len         size of the line buffer
     * @param   timeout     abort after this many milliseconds without data
     *
     * @return  true if a line was read
     */
    static bool uri_load_read_line( WiFiClient *stream, char *line, size_t len, uint32_t timeout ) {
        uint32_t last_progress = millis();
        size_t pos = 0;

        line[ 0 ] = '\0';

        while ( true ) {
            if ( stream->available() ) {
                int c = stream->read();

                if ( c < 0 ) {
                    continue;
                }
                if ( c == '\n' ) {
                    line[ pos ] = '\0';
                    return( true );
                }
                if ( c != '\r' && pos < len - 1 ) {
                    line[ pos++ ] = ( char )c;
                }

                last_progress = millis();
            }
            else {
                if ( !stream->connected() ) {
                    return( false );
                }
                delay( 5 );
                if ( millis() - last_progress > timeout ) {
                    return( false );
                }
            }
        }
    }
    /**
     * @brief read data into the descriptor, gives the cpu away while it waits
     *
     * @param   uri_load_dsc    descriptor to write the data into
     * @param   alloc           current allocation, updated while growing
     * @param   stream          open stream
     * @param   len             bytes to read, negative reads until the peer closes
     * @param   timeout         abort after this many milliseconds without data
     *
     * @return  bytes read, -1 on error
     */
    static int32_t uri_load_read_data( uri_load_dsc_t *uri_load_dsc, uint32_t *alloc, WiFiClient *stream, int32_t len, uint32_t timeout ) {
        uint32_t last_progress = millis();
        int32_t bytes_read = 0;

        while ( len < 0 || bytes_read < len ) {
            int available = stream->available();

            if ( available > 0 ) {
                uint32_t chunk = available > URI_BLOCK_SIZE ? URI_BLOCK_SIZE : available;

                if ( len >= 0 && chunk > ( uint32_t )( len - bytes_read ) ) {
                    chunk = len - bytes_read;
                }
                if ( !uri_load_reserve( uri_load_dsc, alloc, chunk ) ) {
                    return( -1 );
                }

                int c = stream->read( uri_load_dsc->data + uri_load_dsc->size, chunk );
                if ( c <= 0 ) {
                    continue;
                }

                uri_load_dsc->size += c;
                bytes_read += c;
                last_progress = millis();
            }
            else {
                /**
                 * a closed connection ends an unknown length, but only after the buffer ran dry
                 */
                if ( !stream->connected() ) {
                    if ( len < 0 ) {
                        break;
                    }
                    URI_LOAD_ERROR_LOG("download closed early, %d bytes", uri_load_dsc->size );
                    return( -1 );
                }

                delay( 5 );
                if ( millis() - last_progress > timeout ) {
                    URI_LOAD_ERROR_LOG("download stalled, %d bytes", uri_load_dsc->size );
                    return( -1 );
                }
            }
        }

        return( bytes_read );
    }
    /**
     * @brief load a body without a content length, chunked or terminated by the connection close
     *
     * @param   uri_load_dsc    descriptor to write the data into
     * @param   download_client an open client, right after a successful GET
     *
     * @return  true if success
     */
    static bool uri_load_body_to_ram( uri_load_dsc_t *uri_load_dsc, HTTPClient &download_client ) {
        WiFiClient *stream = download_client.getStreamPtr();
        bool chunked = download_client.header("Transfer-Encoding").equalsIgnoreCase("chunked");
        uint32_t timeout = URI_LOAD_STREAM_TIMEOUT;
        uint32_t alloc = 0;
        bool success = true;

        if ( !stream ) {
            URI_LOAD_ERROR_LOG("no stream");
            return( false );
        }
        /**
         * unchunk here, HTTPClient does it only in writeToStream, and that one spins without
         * a yield while it waits for the next chunk header
         */
        if ( chunked ) {
            char line[ 32 ] = "";

            while ( success ) {
                if ( !uri_load_read_line( stream, line, sizeof( line ), timeout ) ) {
                    URI_LOAD_ERROR_LOG("chunk header missing, %d bytes", uri_load_dsc->size );
                    success = false;
                    break;
                }

                int32_t len = strtol( line, NULL, 16 );
                if ( len <= 0 ) {
                    break;
                }
                if ( uri_load_read_data( uri_load_dsc, &alloc, stream, len, timeout ) != len ) {
                    success = false;
                    break;
                }

                uri_load_read_line( stream, line, sizeof( line ), timeout );
                timeout = URI_LOAD_STALL_TIMEOUT;
            }
        }
        else {
            success = uri_load_read_data( uri_load_dsc, &alloc, stream, -1, timeout ) >= 0;
        }

        if ( !success ) {
            return( false );
        }

        if ( !uri_load_dsc->size ) {
            URI_LOAD_ERROR_LOG("download empty");
            return( false );
        }
        /**
         * terminate strings like the content length path does
         */
        uint8_t *ptr = (uint8_t*)REALLOC( uri_load_dsc->data, uri_load_dsc->size + 1 );
        if ( !ptr ) {
            URI_LOAD_ERROR_LOG("data alloc failed, %d bytes", uri_load_dsc->size + 1 );
            return( false );
        }
        uri_load_dsc->data = ptr;
        uri_load_dsc->data[ uri_load_dsc->size ] = '\0';

        URI_LOAD_LOG("%d bytes without a content length, %s", uri_load_dsc->size, chunked ? "chunked" : "identity" );

        if ( uri_load_dsc->progresscb ) {
            uri_load_dsc->progresscb( 100 );
        }

        return( true );
    }
#endif

uri_load_dsc_t *uri_load_to_ram( const char *uri, progress_cb_t *progresscb ) {
    return( uri_load_to_ram_depth( uri, progresscb, 0 ) );
}

static uri_load_dsc_t *uri_load_to_ram_depth( const char *uri, progress_cb_t *progresscb, uint8_t depth ) {
    /**
     * alloc uri_load_dsc structure
     */
    uri_load_dsc_t *uri_load_dsc = uri_load_create_dsc();
    /**
     * 
     */
    if ( uri_load_dsc ) {
#ifdef NATIVE_64BIT
        URI_LOAD_LOG("uri_load_dsc: alloc %ld bytes at %p", sizeof( uri_load_dsc_t ), uri_load_dsc );
#else
        URI_LOAD_LOG("uri_load_dsc: alloc %d bytes at %p", sizeof( uri_load_dsc_t ), uri_load_dsc );
#endif        /**
         * set progress call back function
         */
        uri_load_dsc->progresscb = progresscb;
        /**
         * set download timestamp
         */
        uri_load_dsc->timestamp = millis();
        /**
         * set filename in the uri_load_dsc
         */
        uri_load_set_filename_from_uri( uri_load_dsc, uri );
        /**
         * set filename in the uri_load_dsc
         */
        uri_load_set_url_from_uri( uri_load_dsc, uri );
        /**
         * check for uri file source
         */
        if ( strstr( uri, "http://" ) ) {
            URI_LOAD_LOG("http source");
            uri_load_dsc = uri_load_http_to_ram( uri_load_dsc, depth );
        }
        else if ( strstr( uri, "https://" ) ) {
            URI_LOAD_LOG("https source");
            uri_load_dsc = uri_load_https_to_ram( uri_load_dsc, depth );
        }
        else if ( strstr( uri, "file://" ) ) {
            URI_LOAD_LOG("local files source");
            uri_load_dsc = uri_load_file_to_ram( uri_load_dsc );
        }
        else {
            URI_LOAD_ERROR_LOG("uri not supported");
            uri_load_free_all( uri_load_dsc );
            uri_load_dsc = NULL;
        }
    }
    else {
        URI_LOAD_ERROR_LOG("uri_load_dsc: alloc failed");
    }
    return( uri_load_dsc );
}

uri_load_dsc_t *uri_load_to_ram( const char *uri ) {
    return( uri_load_to_ram( uri, NULL ) );
}

bool uri_load_to_file( const char *uri, const char *path, const char *dest_filename, progress_cb_t *progresscb ) {
    bool retval = false;
    /**
     * alloc uri_load_dsc structure
     */
    uri_load_dsc_t *uri_load_dsc = uri_load_create_dsc();
    /**
     * 
     */
    if ( uri_load_dsc ) {
#ifdef NATIVE_64BIT
        URI_LOAD_LOG("uri_load_dsc: alloc %ld bytes at %p", sizeof( uri_load_dsc_t ), uri_load_dsc );
#else
        URI_LOAD_LOG("uri_load_dsc: alloc %d bytes at %p", sizeof( uri_load_dsc_t ), uri_load_dsc );
#endif
        /**
         * set progress call back function
         */
        uri_load_dsc->progresscb = progresscb;
        /**
         * set download timestamp
         */
        uri_load_dsc->timestamp = millis();
        /**
         * set filename in the uri_load_dsc
         */
        uri_load_set_filename_from_uri( uri_load_dsc, uri );
        /**
         * set filename in the uri_load_dsc
         */
        uri_load_set_url_from_uri( uri_load_dsc, uri );
        /**
         * check for uri file source
         */
        if ( strstr( uri, "http://" ) ) {
            URI_LOAD_LOG("http source");
            uri_load_dsc = uri_load_http_to_ram( uri_load_dsc, 0 );
        }
        else if ( strstr( uri, "https://" ) ) {
            URI_LOAD_LOG("https source");
            uri_load_dsc = uri_load_https_to_ram( uri_load_dsc, 0 );
        }
        else if ( strstr( uri, "file://" ) ) {
            URI_LOAD_LOG("local files source");
            uri_load_dsc = uri_load_file_to_ram( uri_load_dsc );
        }
        else {
            URI_LOAD_ERROR_LOG("uri not supported");
            uri_load_free_all( uri_load_dsc );
            uri_load_dsc = NULL;
        }
        /**
         * store to path if download was success
         */
        if ( uri_load_dsc ) {
            /**
             * alloc memory for filename
             */
            char *filename = NULL;
            if ( dest_filename ) {
                filename = (char*)MALLOC( strlen( path ) + strlen( dest_filename ) + 1 );
            }
            else {
                filename = (char*)MALLOC( strlen( path ) + strlen( uri_load_dsc->filename ) + 1 );
            }
            /**
             * check if alloc failed
             */
            if ( !filename ) {
                /**
                 * free uri_load_dsc
                 */
                uri_load_free_all( uri_load_dsc );
            }
            else {
                /**
                 * copy path and filename into a file location string
                 */
                if ( dest_filename ) {
                    strncpy( filename, path, strlen( path ) + strlen( dest_filename ) + 1 );
                    strncat( filename, dest_filename, strlen( path ) + strlen( dest_filename ) + 1 );
                }
                else {
                    strncpy( filename, path, strlen( path ) + strlen( uri_load_dsc->filename ) + 1 );
                    strncat( filename, uri_load_dsc->filename, strlen( path ) + strlen( uri_load_dsc->filename ) + 1 );
                }
                /**
                 * open file
                 */
                FILE *file = fopen( filename, "wb" );
                /**
                 * check if create was successfull
                 */
                if ( file ) {
                    URI_LOAD_LOG("open file: %s", filename );
                    size_t size = uri_load_dsc->size;
                    unsigned char *data_p = uri_load_dsc->data;
                    /**
                     * write all chunks out
                     */
                    while( size ) {
                        if ( size < URI_BLOCK_SIZE ) {
                            if ( fwrite( data_p, 1, size, file ) == size ) {
                                size = 0;
                            }
                            break;
                        }
                        if ( fwrite( data_p, 1, URI_BLOCK_SIZE, file ) == URI_BLOCK_SIZE ) {
                            data_p = data_p + URI_BLOCK_SIZE;
                            size = size - URI_BLOCK_SIZE;
                        }
                        else {
                            break;
                        }
                    }
                    /**
                     * all bytes written?
                     */
                    if ( size ) {
                        URI_LOAD_ERROR_LOG("error while write %s", filename );
                    }
                    else {
                        retval = true;
                    }
                }
                else {
                    URI_LOAD_LOG("error open file: %s", filename );
                }
                fclose( file );
            }
            free( filename );
        }
    }
    else {
        URI_LOAD_ERROR_LOG("uri_load_dsc: alloc failed");
    }
    return( retval );
}

bool uri_load_to_file( const char *uri, const char *path ) {
    return( uri_load_to_file( uri, path, NULL, NULL ) );
}

bool uri_load_to_file( const char *uri, const char *path, const char *dest_filename ) {
    return( uri_load_to_file( uri, path, dest_filename, NULL ) );
}

uri_load_dsc_t *uri_load_http_to_ram( uri_load_dsc_t *uri_load_dsc, uint8_t depth ) {
#ifdef NATIVE_64BIT
    if ( uri_load_dsc ) {
        CURL *curl_handle;
        CURLcode res;
        struct MemoryStruct chunk;

        chunk.memory = (char*)MALLOC(1);  /* will be grown as needed by the realloc above */
        chunk.size = 0;    /* no data at this point */

        curl_global_init(CURL_GLOBAL_ALL);
        /**
         * init the curl session
         */
        curl_handle = curl_easy_init();
        /**
         * specify URL to get
         */
        curl_easy_setopt( curl_handle, CURLOPT_URL, uri_load_dsc->uri );
        /**
         * send all data to this function 
         */
        curl_easy_setopt( curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback );
        /**
         * we pass our 'chunk' struct to the callback function
         */
        curl_easy_setopt( curl_handle, CURLOPT_WRITEDATA, (void *)&chunk );
        /**
         * some servers don't like requests that are made without a user-agent
         * field, so we provide one
         */
        curl_easy_setopt( curl_handle, CURLOPT_USERAGENT, HARDWARE_NAME "-" __FIRMWARE__ );
        /**
         * get it!
         */
        res = curl_easy_perform( curl_handle );
        /*
        * check for errors
        */
        if( res != CURLE_OK ) {
            log_e( "curl_easy_perform() failed: %s\n", curl_easy_strerror( res ) );
            uri_load_free_all( uri_load_dsc );
            uri_load_dsc = NULL;
        }
        else {
            /*
             * Do something nice with it!
             */
            uri_load_dsc->data = (uint8_t *)chunk.memory;
            uri_load_dsc->size = chunk.size;
        }
        /**
         * cleanup curl stuff
         */
        curl_easy_cleanup( curl_handle );
        /**
         * we're done with libcurl, so clean it up
         */
        curl_global_cleanup();
    }

    if ( uri_load_dsc ) {
        /**
         * add a char to terminate strings
         */
        uint8_t *ptr = (uint8_t*)REALLOC( uri_load_dsc->data , uri_load_dsc->size + 1 );
        if( !ptr ) {
            log_e("no memory left");
            while( 1 );
        }
        uri_load_dsc->data = ptr;
        ptr += uri_load_dsc->size;
        *ptr = '\0';
    }

#else
    const char * headerKeys[] = {"location", "redirect", "Content-Type", "Content-Length", "Content-Disposition", "Transfer-Encoding" };
    const size_t numberOfHeaders = 6;
    /**
     * check if alloc was failed
     */
    if ( uri_load_dsc ) {
        URI_LOAD_LOG("load file from: %s", uri_load_dsc->uri );
        /**
         * open http connection
         */
        HTTPClient download_client;                     /** @brief http download client */
        download_client.setTimeout( 1500 );
        download_client.begin( uri_load_dsc->uri );
        download_client.collectHeaders( headerKeys, numberOfHeaders );
        download_client.setUserAgent( HARDWARE_NAME "-" __FIRMWARE__ );
        int httpCode = download_client.GET();
        /**
         * request successfull?
         */
        if ( httpCode == HTTP_CODE_OK  ) {
            int content_length = download_client.getSize();                         /** @brief -1 if the server sends no content length */
            /**
             * without a content length the body is chunked or ends with the connection
             */
            if ( content_length <= 0 ) {
                if ( !uri_load_body_to_ram( uri_load_dsc, download_client ) ) {
                    download_client.end();
                    uri_load_free_all( uri_load_dsc );
                    return( NULL );
                }
            }
            else {
                /**
                 * get file size and alloc memory for the file
                 */
                uri_load_dsc->size = content_length;
                uri_load_dsc->data = (uint8_t*)CALLOC( 1, uri_load_dsc->size + 1 );
                URI_LOAD_LOG("uri_load_dsc->data: alloc %d bytes at %p", uri_load_dsc->size, uri_load_dsc->data );
                /**
                 * check if alloc success
                 */
                if ( uri_load_dsc->data ) {
                    /**
                     * setup data write counter/pointer/buffer and data stream
                     */
                    uint32_t bytes_left = uri_load_dsc->size;                           /** @brief download left byte counter */
                    uint8_t *data_write_p = uri_load_dsc->data;                         /** @brief write pointer for the raw file download */
                    WiFiClient *download_stream = download_client.getStreamPtr();       /** @brief get streampointer */
                    uint32_t last_progress = millis();                                  /** @brief timestamp of the last received chunk */
                    /**
                     * get download data
                     */
                    while( download_client.connected() && ( bytes_left > 0 ) ) {
                        /**
                         * get bytes in buffer and store them
                         */
                        size_t size = download_stream->available();
                        if ( size > 0 ) {
                            size_t c = download_stream->readBytes( data_write_p, size < bytes_left ? size : bytes_left );
                            bytes_left -= c;
                            data_write_p = data_write_p + c;
                            if ( uri_load_dsc->progresscb ) {
                                uri_load_dsc->progresscb( ( 100 * ( uri_load_dsc->size - bytes_left ) ) / uri_load_dsc->size );
                            }
                            last_progress = millis();
                        }
                        else {
                            /**
                             * give the cpu to other tasks and abort if the download stalls
                             */
                            delay( 5 );
                            if ( millis() - last_progress > URI_LOAD_STALL_TIMEOUT ) {
                                URI_LOAD_ERROR_LOG("download stalled");
                                break;
                            }
                        }
                    }
                    if ( bytes_left != 0 ) {
                        URI_LOAD_ERROR_LOG("download failed");
                        download_client.end();
                        uri_load_free_all( uri_load_dsc );
                        return( NULL );
                    }
                }
                else {
                    URI_LOAD_ERROR_LOG("data alloc failed, %d bytes", uri_load_dsc->size );
                    download_client.end();
                    uri_load_free_all( uri_load_dsc );
                    return( NULL );
                }
            }
        }
        else {
            String location = "";
            /**
             * check for a 301/302 redirect
             */
            if ( httpCode == 301 || httpCode == 302 ) {
                if ( download_client.header("location") != "" ) {
                    location = download_client.header("location");
                }
                else {
                    location = download_client.header("redirect");
                }
                URI_LOAD_INFO_LOG("301/302 redirect to: %s", location.c_str() );
            }
            /**
             * clean old connection
             */
            download_client.end();
            /**
             * if we have a new location, try it
             */
            if ( location.isEmpty() ) {
                uri_load_free_all( uri_load_dsc );
                uri_load_dsc = NULL;
                URI_LOAD_ERROR_LOG("http connection abort, code: %d", httpCode );
            }
            else if ( depth >= URI_LOAD_MAX_REDIRECT ) {
                uri_load_free_all( uri_load_dsc );
                uri_load_dsc = NULL;
                URI_LOAD_ERROR_LOG("too many redirects");
            }
            else {
                /**
                 * get new location data
                 */
                uri_load_dsc_t *_uri_load_dsc = uri_load_to_ram_depth( location.c_str(), uri_load_dsc->progresscb, depth + 1 );
                /**
                 * if was success, set data and file size to the old uri_load_dsc to save
                 * old filename and uri to hide redirect
                 */
                if ( _uri_load_dsc ) {
                    uri_load_dsc->data = _uri_load_dsc->data;
                    uri_load_dsc->size = _uri_load_dsc->size;
                    uri_load_free_without_data( _uri_load_dsc );
                }
                else {
                    /**
                     * clear old uri_load_dsc
                     */
                    uri_load_free_all( uri_load_dsc );
                    uri_load_dsc = NULL;                    
                    URI_LOAD_ERROR_LOG("redirect failed");
                }
            }
            return( uri_load_dsc );
        }
        /**
         * close http connection
         */
        download_client.end();
    }
    else {
        URI_LOAD_ERROR_LOG("uri_load_dsc: alloc failed");
    }
#endif
    return( uri_load_dsc );
}

uri_load_dsc_t *uri_load_https_to_ram( uri_load_dsc_t *uri_load_dsc, uint8_t depth ) {
#ifdef NATIVE_64BIT
    if ( uri_load_dsc ) {
        CURL *curl_handle;
        CURLcode res;
        struct MemoryStruct chunk;
        /**
         * will be grown as needed by the realloc above
         * no data at this point
         */
        chunk.memory = (char*)MALLOC( 1 );  
        chunk.size = 0;
        /**
         * global curl init
         */
        curl_global_init(CURL_GLOBAL_ALL);
        /**
         * init the curl session
         */
        curl_handle = curl_easy_init();
        /**
         * specify URL to get
         */
        curl_easy_setopt( curl_handle, CURLOPT_URL, uri_load_dsc->uri );
        /**
         * send all data to this function 
         */
        curl_easy_setopt( curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback );
        /**
         * we pass our 'chunk' struct to the callback function
         */
        curl_easy_setopt( curl_handle, CURLOPT_WRITEDATA, (void *)&chunk );
        /**
         * some servers don't like requests that are made without a user-agent
         * field, so we provide one
         */
        curl_easy_setopt( curl_handle, CURLOPT_USERAGENT, HARDWARE_NAME "-" __FIRMWARE__ );
        /**
         * get it!
         */
        res = curl_easy_perform( curl_handle );
        /*
        * check for errors
        */
        if( res != CURLE_OK ) {
            URI_LOAD_ERROR_LOG( "curl_easy_perform() failed: %s\n", curl_easy_strerror( res ) );
            uri_load_free_all( uri_load_dsc );
            uri_load_dsc = NULL;
        }
        else {
            /*
            * Now, our chunk.memory points to a memory block that is chunk.size
            * bytes big and contains the remote file.
            *
            * Do something nice with it!
            */
            uri_load_dsc->data = (uint8_t *)chunk.memory;
            uri_load_dsc->size = chunk.size;
        }
        /**
         * cleanup curl stuff
         */
        curl_easy_cleanup(curl_handle);
        /**
         * we're done with libcurl, so clean it up
         */
        curl_global_cleanup();
    }

    if ( uri_load_dsc ) {
        /**
         * add a char to terminate strings
         */
        uint8_t *ptr = (uint8_t*)REALLOC( uri_load_dsc->data , uri_load_dsc->size + 1 );
        if( !ptr ) {
            URI_LOAD_ERROR_LOG("no memory left");
            while( 1 );
        }
        uri_load_dsc->data = ptr;
        ptr += uri_load_dsc->size;
        *ptr = '\0';
    }

#else
    const char * headerKeys[] = {"location", "redirect", "Content-Type", "Content-Length", "Content-Disposition", "Transfer-Encoding" };
    const size_t numberOfHeaders = 6;
    /**
     * check if alloc was failed
     */
    if ( uri_load_dsc ) {
        URI_LOAD_LOG("load file from: %s", uri_load_dsc->uri );
        /**
         * open http connection
         */
        heap_caps_malloc_extmem_enable( 1 );
        WiFiClientSecure client;                                                    /** @brief SSL/TLS client connection */
        client.setInsecure();                                                       /** allow insecure connection */
        HTTPClient download_client;                                                 /** @brief http download client */
        download_client.setTimeout( 1500 );
        download_client.begin( client, uri_load_dsc->uri );
        download_client.collectHeaders( headerKeys, numberOfHeaders );
        download_client.setUserAgent( HARDWARE_NAME "-" __FIRMWARE__ );
        int httpCode = download_client.GET();
        /**
         * request successfull?
         */
        if ( httpCode == HTTP_CODE_OK  ) {
            int content_length = download_client.getSize();                         /** @brief -1 if the server sends no content length */
            /**
             * without a content length the body is chunked or ends with the connection
             */
            if ( content_length <= 0 ) {
                if ( !uri_load_body_to_ram( uri_load_dsc, download_client ) ) {
                    download_client.end();
                    client.stop();
                    uri_load_free_all( uri_load_dsc );
                    heap_caps_malloc_extmem_enable( 16 * 1024 );
                    return( NULL );
                }
            }
            else {
                /**
                 * get file size and alloc memory for the file
                 */
                uri_load_dsc->size = content_length;
                uri_load_dsc->data = (uint8_t*)CALLOC( 1, uri_load_dsc->size + 1 );
                URI_LOAD_LOG("uri_load_dsc->data: alloc %d bytes at %p", uri_load_dsc->size, uri_load_dsc->data );
                /**
                 * check if alloc success
                 */
                if ( uri_load_dsc->data ) {
                    /**
                     * setup data write counter/pointer/buffer and data stream
                     */
                    uint32_t bytes_left = uri_load_dsc->size;                           /** @brief download left byte counter */
                    uint8_t *data_write_p = uri_load_dsc->data;                         /** @brief write pointer for the raw file download */
                    WiFiClient *download_stream = download_client.getStreamPtr();       /** @brief get streampointer */
                    uint32_t last_progress = millis();                                  /** @brief timestamp of the last received chunk */
                    /**
                     * get download data
                     */
                    while( download_client.connected() && ( bytes_left > 0 ) ) {
                        /**
                         * get bytes in buffer and store them
                         */
                        size_t size = download_stream->available();
                        if ( size > 0 ) {
                            size_t c = download_stream->readBytes( data_write_p, size < bytes_left ? size : bytes_left );
                            bytes_left -= c;
                            data_write_p = data_write_p + c;
                            if ( uri_load_dsc->progresscb ) {
                                uri_load_dsc->progresscb( ( 100 * ( uri_load_dsc->size - bytes_left ) ) / uri_load_dsc->size );
                            }
                            last_progress = millis();
                        }
                        else {
                            /**
                             * give the cpu to other tasks and abort if the download stalls
                             */
                            delay( 5 );
                            if ( millis() - last_progress > URI_LOAD_STALL_TIMEOUT ) {
                                URI_LOAD_ERROR_LOG("download stalled");
                                break;
                            }
                        }
                    }
                    if ( bytes_left != 0 ) {
                        URI_LOAD_ERROR_LOG("download failed");
                        download_client.end();
                        client.stop();
                        uri_load_free_all( uri_load_dsc );
                        heap_caps_malloc_extmem_enable( 16 * 1024 );
                        return( NULL );
                    }
                }
                else {
                    URI_LOAD_ERROR_LOG("data alloc failed, %d bytes", uri_load_dsc->size );
                    download_client.end();
                    client.stop();
                    uri_load_free_all( uri_load_dsc );
                    heap_caps_malloc_extmem_enable( 16 * 1024 );
                    return( NULL );
                }
            }
        }
        else {
            String location = "";
            /**
             * check for a 301/302 redirect
             */
            if ( httpCode == 301 || httpCode == 302 ) {
                if ( download_client.header("location") != "" ) {
                    location = download_client.header("location");    
                }
                else {
                    location = download_client.header("redirect");    
                }
                URI_LOAD_INFO_LOG("301/302 redirect to: %s", location.c_str() );
            }
            /**
             * clean old connection
             */
            download_client.end();
            client.stop();
            heap_caps_malloc_extmem_enable( 16 * 1024 );
            /**
             * if we have a new location, try it
             */
            if ( location.isEmpty() ) {
                uri_load_free_all( uri_load_dsc );
                uri_load_dsc = NULL;
                URI_LOAD_ERROR_LOG("http connection abort, code: %d", httpCode );
            }
            else if ( depth >= URI_LOAD_MAX_REDIRECT ) {
                uri_load_free_all( uri_load_dsc );
                uri_load_dsc = NULL;
                URI_LOAD_ERROR_LOG("too many redirects");
            }
            else {
                /**
                 * get new location data
                 */
                uri_load_dsc_t *_uri_load_dsc = uri_load_to_ram_depth( location.c_str(), uri_load_dsc->progresscb, depth + 1 );
                /**
                 * if was success, set data and file size to the old uri_load_dsc to save
                 * old filename and uri to hide redirect
                 */
                if ( _uri_load_dsc ) {
                    uri_load_dsc->data = _uri_load_dsc->data;
                    uri_load_dsc->size = _uri_load_dsc->size;
                    uri_load_free_without_data( _uri_load_dsc );
                }
                else {
                    /**
                     * clear old uri_load_dsc
                     */
                    uri_load_free_all( uri_load_dsc );
                    uri_load_dsc = NULL;                    
                    URI_LOAD_ERROR_LOG("redirect failed");
                }
            }
            return( uri_load_dsc );
        }
        /**
         * close http connection
         */
        download_client.end();
        client.stop();
        heap_caps_malloc_extmem_enable( 16 * 1024 );
    }
    else {
        URI_LOAD_ERROR_LOG("uri_load_dsc: alloc failed");
    }
#endif
    return( uri_load_dsc );
}

uri_load_dsc_t *uri_load_file_to_ram( uri_load_dsc_t *uri_load_dsc ) {
    /**
     * check if alloc was failed
     */
    if ( uri_load_dsc ) {
        URI_LOAD_LOG("load file from: %s", uri_load_dsc->uri );
        /**
         * try to open file
         */
#ifdef NATIVE_64BIT
        char filepath[512] = "";
        /**
         * resolve local filepath on native uni*x maschine
         */
        if ( getenv("HOME") )
            snprintf( filepath, sizeof( filepath ), "%s/.hedge%s", getpwuid(getuid())->pw_dir, strstr( uri_load_dsc->uri, "://" ) + 3 );
#else
        const char *filepath = strstr( uri_load_dsc->uri, "://" ) + 3;
#endif
        URI_LOAD_LOG("open file from %s", filepath );
        FILE* file;
        file = fopen( filepath, "rb" );

        if ( file ) {
            URI_LOAD_LOG("file open success");
            /**
             * get file len
             */
            fseek( file, 0, SEEK_END );
            uri_load_dsc->size = ftell( file );
            fseek( file, 0, SEEK_SET );
            /**
             * alloc data mamory
             */
            uri_load_dsc->data = (uint8_t*)CALLOC( 1, uri_load_dsc->size + 1 );
            if( uri_load_dsc->data ) {
                fread( uri_load_dsc->data, uri_load_dsc->size, 1, file );
                uri_load_dsc->timestamp = millis();
            }
            else {
                URI_LOAD_ERROR_LOG("uri_load_dsc->data: alloc failed");
                uri_load_free_all( uri_load_dsc );
                uri_load_dsc = NULL;
            }
            fclose( file );
        }
        else {
            URI_LOAD_ERROR_LOG("file open failed");
            uri_load_free_all( uri_load_dsc );
            uri_load_dsc = NULL;
        }
    }
    else {
        URI_LOAD_ERROR_LOG("uri_load_dsc: alloc failed");
    }
    return( uri_load_dsc );
}

uri_load_dsc_t *uri_load_create_dsc( void ) {
    uri_load_dsc_t *uri_load_dsc = (uri_load_dsc_t *)MALLOC( sizeof( uri_load_dsc_t) );
    /**
     * if alloc was success, init structure
     */
    if( uri_load_dsc ) {
        uri_load_dsc->data = NULL;
        uri_load_dsc->filename = NULL;
        uri_load_dsc->uri = NULL;
        uri_load_dsc->progresscb = NULL;
        uri_load_dsc->timestamp = millis();
        uri_load_dsc->size = 0;
    }
    return( uri_load_dsc );
}

void uri_load_free_all( uri_load_dsc_t *uri_load_dsc ) {
    if ( uri_load_dsc ) {
        /**
         * free name momory
         */
        uri_load_free_name( uri_load_dsc );
        /**
         * free url momory
         */
        uri_load_free_url( uri_load_dsc );
        /**
         * free data momory
         */
        uri_load_free_data( uri_load_dsc );
        /**
         * free uri_load_dsc memory
         */
        uri_load_free_dsc( uri_load_dsc );
    }
}

void uri_load_free_name( uri_load_dsc_t *uri_load_dsc ) {
    if ( uri_load_dsc->filename ) {
        URI_LOAD_LOG("free uri_load->filename: %p", uri_load_dsc->filename );
        free( uri_load_dsc->filename );
        uri_load_dsc->filename = NULL;
    }
}

void uri_load_free_url( uri_load_dsc_t *uri_load_dsc ) {
    if ( uri_load_dsc->uri ) {
        URI_LOAD_LOG("free uri_load->uri: %p", uri_load_dsc->uri );
        free( uri_load_dsc->uri );
        uri_load_dsc->uri = NULL;
    }
}

void uri_load_free_data( uri_load_dsc_t *uri_load_dsc ) {
    if ( uri_load_dsc->data ) {
        URI_LOAD_LOG("free uri_load->data: %p", uri_load_dsc->data );
        free( uri_load_dsc->data );
        uri_load_dsc->data = NULL;
    }
}

void uri_load_free_dsc( uri_load_dsc_t *uri_load_dsc ) {
    if ( uri_load_dsc ) {
        /**
         * free uri_load_dsc memory
         */
        URI_LOAD_LOG("free: %p", uri_load_dsc );
        free( uri_load_dsc );
    }
}

void uri_load_free_without_data( uri_load_dsc_t *uri_load_dsc ) {
    if ( uri_load_dsc ) {
        /**
         * free name momory
         */
        uri_load_free_name( uri_load_dsc );
        /**
         * free url momory
         */
        uri_load_free_url( uri_load_dsc );
        /**
         * free uri_load_dsc memory
         */
        uri_load_free_dsc( uri_load_dsc );
    }
}

void uri_load_set_filename_from_uri( uri_load_dsc_t *uri_load_dsc, const char *uri ) {
    const char *filename_from_uri = NULL;
    const char *tld = NULL;
    const char index[] = "index.html";
    /**
     * check for http or https url
     */
    if ( uri_load_dsc ) {
        if ( strstr( uri, "http://" ) || strstr( uri, "https://" )) {
            tld = strstr( uri, "://" ) + 3;
            /**
             * reverse search for the lash
             */
            filename_from_uri = (const char*)strrchr( tld, '/' );
            /**
             * if not lash found, set index.html as filename
             */
            if ( !filename_from_uri ) {
                filename_from_uri = index;
            }
            /**
             * alloc filename momory
             */
            uri_load_dsc->filename = (char *)MALLOC( strlen( filename_from_uri ) + 2 );
            if ( uri_load_dsc->filename ) {
                /**
                 * copy filename into alloc mamory
                 */
#ifdef NATIVE_64BIT
                URI_LOAD_LOG("uri_load_dsc->filename: alloc %ld bytes at %p", strlen( filename_from_uri ), uri_load_dsc->filename );
#else
                URI_LOAD_LOG("uri_load_dsc->filename: alloc %d bytes at %p", strlen( filename_from_uri ), uri_load_dsc->filename );
#endif
                strncpy( uri_load_dsc->filename, filename_from_uri, strlen( filename_from_uri ) + 2 );
            }
            else {
                URI_LOAD_ERROR_LOG("uri_load_dsc->filename: alloc failed");
            }
        }
        else if ( strstr( uri, "file://" ) ) {
            filename_from_uri = strstr( uri, "://" ) + 3;
            /**
             * alloc filename momory
             */
            uri_load_dsc->filename = (char *)MALLOC( strlen( filename_from_uri ) + 2 );
            if ( uri_load_dsc->filename ) {
                /**
                 * copy filename into alloc mamory
                 */
#ifdef NATIVE_64BIT
                URI_LOAD_LOG("uri_load_dsc->filename: alloc %ld bytes at %p", strlen( filename_from_uri ), uri_load_dsc->filename );
#else
                URI_LOAD_LOG("uri_load_dsc->filename: alloc %d bytes at %p", strlen( filename_from_uri ), uri_load_dsc->filename );
#endif
                strncpy( uri_load_dsc->filename, filename_from_uri, strlen( filename_from_uri ) + 2 );
            }
            else {
                URI_LOAD_ERROR_LOG("uri_load_dsc->filename: alloc failed");
            }
        }
    }
}

void uri_load_set_url_from_uri( uri_load_dsc_t *uri_load_dsc, const char *uri ) {
    /**
     * check for http or https url
     */
    if ( uri_load_dsc ) {
        /**
         * alloc url momory
         */
        uri_load_dsc->uri = (char *)MALLOC( strlen( uri ) + 2 );
        if ( uri_load_dsc->uri ) {
            /**
             * copy url into alloc mamory
             */
#ifdef NATIVE_64BIT
            URI_LOAD_LOG("uri_load_dsc->uri: alloc %ld bytes at %p", strlen( uri ), uri_load_dsc->uri );
#else
            URI_LOAD_LOG("uri_load_dsc->uri: alloc %d bytes at %p", strlen( uri ), uri_load_dsc->uri );
#endif
            strncpy( uri_load_dsc->uri, uri, strlen( uri )  + 2 );
        }
        else {
            URI_LOAD_ERROR_LOG("uri_load->uri: alloc failed");
        }
    }
}