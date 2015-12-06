#ifndef PION_STREAM_H
#define PION_STREAM_H

#include "init.h"

extern ZEND_API zend_class_entry * ion_ce_ION_Listener;
extern ZEND_API zend_class_entry * ion_ce_ION_Stream;
extern ZEND_API zend_class_entry * ion_ce_ION_StreamException;
extern ZEND_API zend_class_entry * ion_ce_ION_Stream_ConnectionException;

// ** state flags begin **
// stream types
#define ION_STREAM_STATE_SOCKET    (1<<0)
#define ION_STREAM_STATE_PAIR      (1<<1) // use with ION_STREAM_STATE_SOCKET
#define ION_STREAM_STATE_PIPE      (1<<2)
#define ION_STREAM_STATE_FILE      (1<<3) // reserved

// reading and writing states
#define ION_STREAM_STATE_FLUSHED   (1<<4)
#define ION_STREAM_STATE_HAS_DATA  (1<<5)

// connect direction
// Important: ION_STREAM_FROM_PEER == PION_NET_FROM_PEER and ION_STREAM_FROM_ME == PION_NET_FROM_ME
#define ION_STREAM_FROM_PEER       (1<<6)
#define ION_STREAM_FROM_ME         (1<<7)
#define ION_STREAM_DIRECTION_MASK  (ION_STREAM_FROM_PEER | ION_STREAM_FROM_ME)

// behavior flags
#define ION_STREAM_STATE_CLOSE_ON_FLUSH   1<<8

// runtime stream states
#define ION_STREAM_STATE_ENABLED   (1<<9)
#define ION_STREAM_STATE_CONNECTED (1<<10)
#define ION_STREAM_STATE_EOF       (1<<11)
#define ION_STREAM_STATE_ERROR     (1<<12)
#define ION_STREAM_STATE_SHUTDOWN  (1<<13)
#define ION_STREAM_STATE_CLOSED    (ION_STREAM_STATE_EOF | ION_STREAM_STATE_ERROR | ION_STREAM_STATE_SHUTDOWN)

#define ION_STREAM_NAME_IPV4       (1<<14)
#define ION_STREAM_NAME_IPV6       (1<<15)
#define ION_STREAM_NAME_UNIX       (1<<16)
#define ION_STREAM_NAME_MASK       (ION_STREAM_NAME_IPV4 | ION_STREAM_NAME_IPV6 | ION_STREAM_NAME_UNIX)

#define ION_STREAM_ENCRYPTED       (1<<17)
#define ION_STREAM_HAS_UNDERLYING  (1<<18)

// ** state flags end **

// mode for line reading (getLine() and readLine())
#define ION_STREAM_MODE_TRIM_TOKEN    1
#define ION_STREAM_MODE_WITH_TOKEN    2
#define ION_STREAM_MODE_WITHOUT_TOKEN 4

#define ION_STREAM_TOKEN_MODE_MASK    (ION_STREAM_MODE_TRIM_TOKEN | ION_STREAM_MODE_WITH_TOKEN | ION_STREAM_MODE_WITHOUT_TOKEN)
#define ION_STREAM_TOKEN_LIMIT        8

typedef struct _ion_listener {
    zend_object      std;
    uint             flags;
    zend_object    * accept;
    zend_object    * encrypt;
    zend_string    * name;
    ion_evlistener * listener;
} ion_listener;

typedef struct _ion_stream_token {
    zend_string    * token;
    zend_long        length;
    zend_long        offset;
    zend_long        flags;
    zend_long        position;
} ion_stream_token;

typedef struct _ion_stream {
    zend_object        std;
    zend_uint          state;   // flags
    ion_buffer       * buffer;  // input/output buffer
    size_t             length;  // bytes for reading
    size_t             input_size;
    int                priority;
    ion_stream_token * token;
    zend_object      * read;
    zend_object      * flush;
    zend_object      * connect;
    zend_object      * shutdown;
    zend_object      * on_data;
    zend_object      * encrypt;
    zend_string      * name_self;
    zend_string      * name_remote;
} ion_stream;

//#ifdef ZTS
//#define STREAM_BUFFER_DEFAULT_FLAGS BEV_OPT_DEFER_CALLBACKS | BEV_OPT_THREADSAFE
//#else
#define STREAM_BUFFER_DEFAULT_FLAGS 0
//#define STREAM_BUFFER_DEFAULT_FLAGS BEV_OPT_DEFER_CALLBACKS
//#endif

#define ion_stream_new(buffer, state)                  ion_stream_new_ex(buffer, state, NULL TSRMLS_CC)

#define ion_stream_input_length(stream) evbuffer_get_length( bufferevent_get_input(stream->buffer) )
#define ion_stream_output_length(stream) evbuffer_get_length( bufferevent_get_output(stream->buffer) )

#define ion_stream_is_valid_fd(stream) (bufferevent_getfd(stream->buffer) == -1)

#define ion_stream_store_encrypt(stream, encryptor) get_object_instance(stream, ion_stream)->encrypt = encryptor
#define ion_stream_set_peer_name(stream, name) get_object_instance(stream, ion_stream)->name_remote = name
#define ion_stream_set_name(stream, name) get_object_instance(stream, ion_stream)->name_self = name

int ion_stream_pair(zend_object ** stream_one, zend_object ** stream_two, zend_class_entry * ce);
zend_object * ion_stream_new_ex(ion_buffer * buffer, int flags, zend_class_entry * cls);
zend_string * ion_stream_get_name_self(zend_object * stream);
zend_string * ion_stream_get_name_remote(zend_object * stream);
zend_string * ion_stream_describe(zend_object * stream);
zend_string * ion_stream_read(ion_stream * stream, size_t size);
zend_string * ion_stream_read_token(ion_stream * stream, ion_stream_token * token);
long   ion_stream_search_token(struct evbuffer * buffer, ion_stream_token * token TSRMLS_DC);
int    ion_stream_close_fd(ion_stream * stream TSRMLS_DC);

#define CHECK_STREAM_BUFFER(stream)                          \
    if(stream->buffer == NULL) {                             \
        zend_throw_exception(ion_ce_ION_StreamException, "Stream buffer is not initialized", 0); \
        return;                                              \
    }

#define CHECK_STREAM_STATE(stream)                              \
    if(stream->state & ION_STREAM_STATE_CLOSED) {               \
        if(stream->state & ION_STREAM_STATE_EOF) {              \
            zend_throw_exception(ion_ce_ION_StreamException, "Stream has been terminated by EOF", 0); \
        } else if(stream->state & ION_STREAM_STATE_ERROR) {     \
            zend_throw_exception(ion_ce_ION_StreamException, "Stream is corrupted", 0); \
        } else if(stream->state & ION_STREAM_STATE_SHUTDOWN) {  \
            zend_throw_exception(ion_ce_ION_StreamException, "Stream has been shut down", 0); \
        }                                                       \
        return;                                                 \
    }

#define CHECK_STREAM(stream)      \
    CHECK_STREAM_BUFFER(stream);  \
    CHECK_STREAM_STATE(stream);


#endif //PION_STREAM_H
