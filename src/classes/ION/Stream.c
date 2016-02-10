#include "ion.h"
#include <ext/standard/url.h>
#include <sys/fcntl.h>
#include <event2/bufferevent_ssl.h>
#include <openssl/err.h>

#ifndef O_NOATIME
# define O_NOATIME 0
#endif

zend_class_entry     * ion_ce_ION_Stream;
zend_object_handlers   ion_oh_ION_Stream;
zend_class_entry     * ion_ce_ION_StreamException;
zend_object_handlers   ion_oh_ION_StreamException;

pion_cb * stream_input   = NULL;
pion_cb * stream_output  = NULL;
pion_cb * stream_eof     = NULL;
pion_cb * stream_error   = NULL;
pion_cb * stream_connect = NULL;


const ion_stream_token empty_stream_token = { NULL, 0, 0, ION_STREAM_MODE_TRIM_TOKEN, -1 };


zend_object * ion_stream_init(zend_class_entry * ce) {
    ion_stream * istream = ecalloc(1, sizeof(ion_stream));
    istream->priority = -1;
    RETURN_INSTANCE(ION_Stream, istream);
}

void ion_stream_free(zend_object * stream_object) {
    ion_stream * stream = get_object_instance(stream_object, ion_stream);
    if(stream->read) {
        zend_object_release(stream->read);
    }
    if(stream->connect) {
        zend_object_release(stream->connect);
    }
    if(stream->shutdown) {
        zend_object_release(stream->shutdown);
    }
    if(stream->flush) {
        zend_object_release(stream->flush);
    }
    if(stream->incoming) {
        zend_object_release(stream->incoming);
    }
    if(stream->buffer) {
        if(stream->state & ION_STREAM_STATE_ENABLED) {
            bufferevent_disable(stream->buffer, EV_READ | EV_WRITE);
        }
        if(stream->encrypt) {
            SSL *ctx = bufferevent_openssl_get_ssl(stream->buffer);
            SSL_set_shutdown(ctx, SSL_RECEIVED_SHUTDOWN);
            SSL_shutdown(ctx);
            zend_object_release(stream->encrypt);
        }
        bufferevent_free(stream->buffer);
    }
    if(stream->error) {
        zend_object_release(stream->error);
    }
    if(stream->name_self) {
        zend_string_release(stream->name_self);
    }
    if(stream->name_remote) {
        zend_string_release(stream->name_remote);
    }

}

zend_string * ion_stream_get_name_self(zend_object * stream_obj) {
    ion_stream * stream = get_object_instance(stream_obj, ion_stream);
    int type   = 0;
    evutil_socket_t socket;
    if(stream->name_self == NULL) {
        socket = bufferevent_getfd(stream->buffer);
        if(socket == -1) {
            return NULL;
        } else {
            type = pion_net_sock_name(socket, PION_NET_NAME_LOCAL, &stream->name_self);
            if(type == PION_NET_NAME_IPV6) {
                stream->state |= ION_STREAM_NAME_IPV6;
            } else if(type == PION_NET_NAME_UNIX) {
                stream->state |= ION_STREAM_NAME_UNIX;
            } else if(type == PION_NET_NAME_UNKNOWN || type == FAILURE) {
                return NULL;
            } else {
                stream->state |= ION_STREAM_NAME_IPV4;
            }
        }
    }
    return zend_string_copy(stream->name_self);
}

zend_string * ion_stream_get_name_remote(zend_object * stream_obj) {
    ion_stream * stream = get_object_instance(stream_obj, ion_stream);
    int type   = 0;
    evutil_socket_t socket;
    if(stream->name_remote == NULL) {
        socket = bufferevent_getfd(stream->buffer);
        if(socket == -1) {
            return NULL;
        } else {
            type = pion_net_sock_name(socket, PION_NET_NAME_REMOTE, &stream->name_remote);
            if(type == PION_NET_NAME_UNKNOWN || type == FAILURE) {
                return NULL;
            }
        }
    }
    return zend_string_copy(stream->name_remote);
}

zend_string * ion_stream_describe(zend_object * stream_object) {
    ion_stream * stream = get_object_instance(stream_object, ion_stream);
    zend_string * describe = NULL;
    zend_string * address_remote;
    zend_string * address_local;

    if(stream->buffer == NULL) {
        return zend_string_init(STRARGS("Stream(empty)"), 0);
    }
    if (stream->state & ION_STREAM_RESERVED) {
        switch(bufferevent_getfd(stream->buffer)) {
            case STDIN_FILENO:
                return zend_string_init(STRARGS("Stream(stdin)"), 0);
            case STDOUT_FILENO:
                return zend_string_init(STRARGS("Stream(stdout)"), 0);
            case STDERR_FILENO:
                return zend_string_init(STRARGS("Stream(stderr)"), 0);
            default:
                ZEND_ASSERT(0);
        }
    }
    if(stream->state & ION_STREAM_STATE_SOCKET) {
        if(ion_stream_is_valid_fd(stream)) {
            return zend_string_init(STRARGS("Stream(invalid)"), 0);
        }
        address_local   = ion_stream_get_name_self(stream_object);
        address_remote  = ion_stream_get_name_remote(stream_object);
        if(address_remote == NULL) {
            address_remote = zend_string_init("undefined", strlen("undefined"), 0);
        }
        if(address_local == NULL) {
            address_remote = zend_string_init("undefined", strlen("undefined"), 0);
        }

        if(stream->state & ION_STREAM_FROM_PEER) {
            describe = strpprintf(MAX_DOMAIN_LENGTH, "Stream(%s<-%s)", address_local->val, address_remote->val);
        } else if(stream->state & ION_STREAM_FROM_ME) {
            describe = strpprintf(MAX_DOMAIN_LENGTH, "Stream(%s->%s)", address_local->val, address_remote->val);
        } else {
            describe = strpprintf(MAX_DOMAIN_LENGTH, "Stream(%s<->%s)", address_local->val, address_remote->val);
        }
        zend_string_release(address_local);
        zend_string_release(address_remote);
        return describe;
    } else if(stream->state & ION_STREAM_STATE_PAIR) {
        return zend_string_init(STRARGS("Stream(twin)"), 0);
    } else {
        return zend_string_init(STRARGS("Stream(pipe)"), 0);
    }
}

long ion_stream_search_token(struct evbuffer * buffer, ion_stream_token * token) {
    struct evbuffer_ptr ptr_end;
    struct evbuffer_ptr ptr_start;
    struct evbuffer_ptr ptr_result;
    size_t current_size = evbuffer_get_length(buffer);
    size_t end = (size_t)token->offset + (size_t)token->length - 1;
    size_t length = (size_t)token->length;
    if(current_size == 0) {
        token->position = -1;
        return SUCCESS;
    }
    if(token->offset >= current_size || ZSTR_LEN(token->token) > current_size) {
        token->position = -1;
        return SUCCESS;
    }
    if(end >= current_size - 1) { // libevent bug? if <end> is last element - evbuffer_search_range can't find token
        length = 0;
    }

    if(evbuffer_ptr_set(buffer, &ptr_start, (size_t)token->offset, EVBUFFER_PTR_SET) == FAILURE) {
        return FAILURE;
    }
    if(length) {
        if(evbuffer_ptr_set(buffer, &ptr_end, end, EVBUFFER_PTR_SET) == FAILURE) {
            return FAILURE;
        }
        ptr_result = evbuffer_search_range(buffer, ZSTR_VAL(token->token), ZSTR_LEN(token->token), &ptr_start, &ptr_end);
    } else {
        ptr_result = evbuffer_search(buffer, ZSTR_VAL(token->token), ZSTR_LEN(token->token), &ptr_start);
    }
    if(token->length > 0 && current_size >= token->length) {
        token->flags |= ION_STREAM_TOKEN_LIMIT;
    }
    token->offset = (zend_long)(current_size - ZSTR_LEN(token->token) + 1);
    token->position = (long)ptr_result.pos;
    return SUCCESS;
}

void _ion_stream_input(ion_buffer * bev, void * ctx) {
    ION_LOOP_CB_BEGIN();
    ion_stream      * stream = get_object_instance(ctx, ion_stream);
    ion_evbuffer    * input;
    zend_string     * data = NULL;

    zend_object_addref(&stream->std);
    if(stream->read) {
        input = bufferevent_get_input(stream->buffer);
        if(stream->token) { // readLine()
            if(ion_stream_search_token(bufferevent_get_input(stream->buffer), stream->token) == FAILURE) {
                ion_promisor_exception_eg(
                        stream->read,
                        ion_ce_ION_StreamException,
                        "Stream corrupted: failed to read token from buffer", 0
                );
            } else if(stream->token->position != -1) { // found
                data = ion_stream_read_token(stream, stream->token);
                if(data) {
                    ion_promisor_done_string(stream->read, data, 0);
                    zend_string_release(data);
                }
            } else if(stream->token->flags & ION_STREAM_TOKEN_LIMIT) {
                ion_promisor_done_false(stream->read);
            }
        } else if(stream->length) { // read()

            if(evbuffer_get_length(input) >= stream->length) {
                data = ion_stream_read(stream, stream->length);
                ion_stream_set_input_size(stream, stream->input_size);
//                bufferevent_setwatermark(stream->buffer, EV_READ, 0, stream->input_size);
                ion_promisor_done_string(stream->read, data, 0);
                zend_string_release(data);
            }
        } // else readAll()

        if(evbuffer_get_length(input)) {
            stream->state |= ION_STREAM_STATE_HAS_DATA;
        } else {
            stream->state &= ~ION_STREAM_STATE_HAS_DATA;
        }
    } else {
        stream->state |= ION_STREAM_STATE_HAS_DATA;
    }

    if(stream->read == NULL                              // not reading
       && stream->incoming != NULL                       // has incoming handler
       && !(stream->state & ION_STREAM_SUSPENDED)        // not suspended
       && (stream->state & ION_STREAM_STATE_HAS_DATA)) { // has incoming data
        zval zstream;
        ZVAL_OBJ(&zstream, &stream->std);
        ion_promisor_sequence_invoke(stream->incoming, &zstream);
    }

    zend_object_release(&stream->std);
    ION_LOOP_CB_END();
}

void _ion_stream_output(ion_buffer * bev, void *ctx) {
    ION_LOOP_CB_BEGIN();
    ion_stream *stream = get_object_instance(ctx, ion_stream);

    obj_add_ref(&stream->std);
    if(stream->flush) {
        ion_promisor_done_true(stream->flush);
        stream->flush = NULL;
    }
    stream->state |= ION_STREAM_STATE_FLUSHED;
    if(stream->state & ION_STREAM_STATE_CLOSE_ON_FLUSH) {
        ion_stream_close_fd(stream);
    }
    zend_object_release(&stream->std);
    ION_LOOP_CB_END();
}

void _ion_stream_notify(ion_buffer * bev, short what, void * ctx) {
    ION_LOOP_CB_BEGIN();
    ion_stream * stream = get_object_instance(ctx, ion_stream);

    obj_add_ref(&stream->std);
    if(what & BEV_EVENT_EOF) {
        stream->state |= ION_STREAM_STATE_EOF;
        if(stream->read) {
            if(stream->token || stream->length) {
                ion_promisor_done_false(stream->read);
            } else { // readAll
                zend_string * data = ion_stream_read(stream, ion_stream_input_length(stream));
                if(!data) {
                    ion_promisor_exception_eg(
                            stream->read,
                            ion_ce_ION_StreamException,
                            "Stream corrupted: failed to read data from buffer", 0
                    );
                } else {
                    ion_promisor_done_string(stream->read, data, 0);
                    zend_string_release(data);
                }
            }
        }
        if(stream->storage) {
            ion_storage_handler_close(stream->storage, ctx);
        }
        if(stream->shutdown) {
            ion_promisor_done_object(stream->shutdown, ctx);
        }
    } else if(what & BEV_EVENT_ERROR) {
        stream->state |= ION_STREAM_STATE_ERROR;
        if(stream->connect || stream->read || stream->flush) {
            zend_ulong    error_ulong = 0;
            int           error_int = 0;
            const char  * error_message;
            zend_object * exception;
            zval          zex;
            zend_class_entry * exception_ce;
            zend_string * desc = ion_stream_describe(&stream->std);

            if((error_ulong =  bufferevent_get_openssl_error(bev))) { // problem with openssl connection
                error_message = ERR_error_string(error_ulong, NULL);
                exception_ce = ion_ce_ION_CryptoException;
            } else if((error_int =  bufferevent_socket_get_dns_error(bev))) { // DNS problem
                error_message = evutil_gai_strerror(error_int);
                exception_ce = ion_ce_ION_DNSException;
            } else if((error_int = EVUTIL_SOCKET_ERROR())) { // socket problem
                error_message = evutil_socket_error_to_string(error_int);
                exception_ce = ion_ce_ION_StreamException;
            } else { // magic problem
                error_message = "unknown error";
                exception_ce = ion_ce_ION_StreamException;
            }

            exception = pion_exception_new_ex(
                exception_ce, 0,
                "%s error: %s", desc->val, error_message
            );
            zend_string_release(desc);
            stream->error = exception;
            ZVAL_OBJ(&zex, exception);
            if(stream->connect) {
                ion_promisor_fail(stream->connect, &zex);
            }
            if(stream->read) {
                ion_promisor_fail(stream->read, &zex);
            }
            if(stream->flush) {
                ion_promisor_fail(stream->flush, &zex);
            }
        }

        if(stream->storage) {
            ion_storage_handler_close(stream->storage, ctx);
        }
        if(stream->shutdown) {
            ion_promisor_done_object(stream->shutdown, ctx);
        }
    } else if(what & BEV_EVENT_TIMEOUT) {
        // we do not use this feature yet
    } else if(what & BEV_EVENT_CONNECTED) {
        stream->state |= ION_STREAM_STATE_CONNECTED;
        if(stream->name_remote) {
            zend_string_release(stream->name_remote);
            stream->name_remote = NULL;
        }
        if(stream->connect) {
            ion_promisor_done_object(stream->connect, &stream->std);
        }

    } else {
        zend_error(E_WARNING, "Unknown type notification: %d", what);
    }
    zend_object_release(&stream->std);

    ION_LOOP_CB_END();
}

int ion_stream_pair(zend_object ** stream_one, zend_object ** stream_two, zend_class_entry * ce) {
    int flags = STREAM_BUFFER_DEFAULT_FLAGS | BEV_OPT_CLOSE_ON_FREE;
    int state = ION_STREAM_STATE_SOCKET | ION_STREAM_STATE_PAIR | ION_STREAM_STATE_ENABLED | ION_STREAM_STATE_CONNECTED;

    ion_buffer  * pair[2];
    zend_object * one;
    zend_object * two;

    if(bufferevent_pair_new(GION(base), flags, pair) == FAILURE) {
        zend_throw_exception(ion_ce_ION_StreamException, "Failed to create pair", 0);
        return FAILURE;
    }
    if(bufferevent_enable(pair[0], EV_READ | EV_WRITE) == FAILURE ||
       bufferevent_enable(pair[1], EV_READ | EV_WRITE) == FAILURE) {
        bufferevent_free(pair[0]);
        bufferevent_free(pair[1]);
        zend_throw_exception(ion_ce_ION_StreamException, "Failed to enable stream", 0);
        return FAILURE;
    }

    one = ion_stream_new_ex(pair[0], state, ce);
    two = ion_stream_new_ex(pair[1], state, ce);
    if(!one || !two) { // constructor failed
        if(one) {
            zend_object_release(one);
        }
        if(two) {
            zend_object_release(two);
        }
        // todo check EG(exception)
        return FAILURE;
    }

    *stream_one = one;
    *stream_two = two;

    return SUCCESS;
}

zend_object * ion_stream_new_ex(ion_buffer * buffer, int flags, zend_class_entry * cls) {
    ion_stream * stream;
    zval         zstream;
    if(!cls) {
        cls = ion_class_entry(ION_Stream);
    }
    object_init_ex(&zstream, cls);
    stream = get_instance(&zstream, ion_stream);
    stream->id = GION(stream_index)++;
    stream->buffer = buffer;
    stream->state |= flags;
    if(flags & ION_STREAM_FROM_PEER) {
        stream->state |= ION_STREAM_STATE_FLUSHED;
    } else {
        if(ion_stream_output_length(stream) == 0) {
            stream->state |= ION_STREAM_STATE_FLUSHED;
        }
        if(ion_stream_input_length(stream)) {
            stream->state |= ION_STREAM_STATE_HAS_DATA;
        }
    }
    bufferevent_setcb(buffer, _ion_stream_input, _ion_stream_output, _ion_stream_notify, (void *) stream);
    if((flags & ION_STREAM_STATE_ENABLED)) {
        if(bufferevent_enable(buffer, EV_READ | EV_WRITE) == FAILURE) {
            bufferevent_free(buffer);
            zval_ptr_dtor(&zstream);
            zend_throw_exception(ion_ce_ION_StreamException, "Failed to enable stream", 0);
            return NULL;
        }
    }
    if (cls->constructor) {
        if(pion_call_constructor_without_args(cls, Z_OBJ(zstream)) == FAILURE) {
            zval_ptr_dtor(&zstream);
            return NULL;
        }
    }
    return Z_OBJ(zstream);
}


zend_string * ion_stream_read(ion_stream * stream, size_t size) {
    size_t incoming_length = ion_stream_input_length(stream);
    zend_string * data;

    if(!incoming_length) {
        return zend_string_init("", 0, 0);
    }
    if(size > incoming_length) {
        size = incoming_length;
    }

    data = zend_string_alloc(size, 0);
    ZSTR_LEN(data) = bufferevent_read(stream->buffer, ZSTR_VAL(data), size);
    if (ZSTR_LEN(data) > 0) {
        ZSTR_VAL(data)[ZSTR_LEN(data)] = '\0';
        return data;
    } else {
        zend_string_free(data);
        return NULL;
    }
}

zend_string * ion_stream_read_token(ion_stream * stream, ion_stream_token * token) {
    if(token->position == 0) {
        if(token->flags & (ION_STREAM_MODE_WITH_TOKEN | ION_STREAM_MODE_TRIM_TOKEN)) {
            if(evbuffer_drain(bufferevent_get_input(stream->buffer), token->token->len) == FAILURE) {
                zend_throw_exception(ion_class_entry(ION_RuntimeException), "Failed to drain token", 0);
                return NULL;
            }
        }
        if(token->flags & ION_STREAM_MODE_WITH_TOKEN) {
            return zend_string_dup(token->token, 0);
        } else {
            return ZSTR_EMPTY_ALLOC();
        }
    } else {
        zend_string * data;
        if(token->flags & ION_STREAM_MODE_WITH_TOKEN) {
            token->position += token->token->len;
        }

        data = zend_string_alloc((size_t)token->position + 1, 0);
        ZSTR_LEN(data) = bufferevent_read(stream->buffer, ZSTR_VAL(data), (size_t)token->position);
        ZSTR_VAL(data)[ZSTR_LEN(data)] = '\0';
        if(token->flags & ION_STREAM_MODE_TRIM_TOKEN) {
            if(evbuffer_drain(bufferevent_get_input(stream->buffer), token->token->len) == FAILURE) {
                zend_string_release(data);
                zend_throw_exception(ion_class_entry(ION_RuntimeException), "Failed to drain token", 0);
                return NULL;
            }
        }
        if (ZSTR_LEN(data) == 0) {
            zend_string_release(data);
            return zend_string_init("", 0, 0);
        }
        return data;
    }
}

int ion_stream_close_fd(ion_stream * stream) {
    evutil_socket_t socket;

    if(stream->state & ION_STREAM_RESERVED) {
        return SUCCESS;
    }

    bufferevent_disable(stream->buffer, EV_READ | EV_WRITE);

    stream->state |= ION_STREAM_STATE_SHUTDOWN;
    socket = bufferevent_getfd(stream->buffer);
    if(socket == -1) {
        return SUCCESS;
    }
    if(stream->state & ION_STREAM_STATE_SOCKET) {
        evutil_closesocket(socket);
    } else if(socket > 2) { // skip stdin, stdout, stderr
        close(socket);
    }
    bufferevent_setfd(stream->buffer, -1);
    if(stream->storage) {
        ion_storage_handler_close(stream->storage, &stream->std);
    }
    if(stream->shutdown) {
        ion_promisor_done_object(stream->shutdown, &stream->std);
    }
    return SUCCESS;
}

/** public static function ION\Stream::resource(resource $resource) : self */
CLASS_METHOD(ION_Stream, resource) {
    zval        * zfd = NULL;
    int           fd = -1;
    int           fd2;
    int           flags = STREAM_BUFFER_DEFAULT_FLAGS | BEV_OPT_CLOSE_ON_FREE;
    int           state = 0;
    ion_buffer  * buffer = NULL;
    zend_object * stream = NULL;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(zfd)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    php_stream * stream_resource;
    php_stream_from_zval_no_verify(stream_resource, zfd);
    if (stream_resource) {
        if(php_stream_cast(stream_resource, PHP_STREAM_AS_FD | PHP_STREAM_CAST_INTERNAL | PHP_STREAM_AS_SOCKETD, (void *) &fd, 0) == SUCCESS) {
            state = ION_STREAM_STATE_SOCKET;
        } else if (php_stream_cast(stream_resource, PHP_STREAM_AS_FD_FOR_SELECT | PHP_STREAM_CAST_INTERNAL, (void *) &fd, 0) == FAILURE) {
            zend_throw_exception(ion_class_entry(InvalidArgumentException), "Argument must be either valid PHP stream resource", 0);
            return;
        }
    }
    if(fd >= 0 && fd <= 2) { // stdin, stdout, stderr
        switch(fd) {
            case 0:
                stream = GION(input);
                break;
            case 1:
                stream = GION(output);
                break;
            case 2:
                stream = GION(error);
                break;
            default:
                ZEND_ASSERT(0);
        }
        zend_object_addref(stream);
    } else {
        fd2 = dup(fd);
        if (fd2 == -1) {
            zend_throw_exception_ex(ion_ce_ION_StreamException, errno, "Failed to duplicate fd: %s", strerror(errno));
            return;
        }

        buffer = bufferevent_socket_new(GION(base), fd2, flags);
        if(NULL == buffer) {
            zend_throw_exception(ion_ce_ION_StreamException, "Failed to create Stream: buffer corrupted", 0);
            return;
        }
        stream = ion_stream_new_ex(buffer, state | ION_STREAM_STATE_ENABLED, zend_get_called_scope(execute_data));
        if(!stream) {
            // todo check EG(exception)
            return;
        }
    }

    RETURN_OBJ(stream);
}

METHOD_ARGS_BEGIN(ION_Stream, resource, 1)
    METHOD_ARG(resource, 0)
METHOD_ARGS_END()

/** public static function ION\Stream::pair() : self[] */
CLASS_METHOD(ION_Stream, pair) {
    zend_get_called_scope(execute_data);
    zend_object * one;
    zend_object * two;
    zval          zstream_one;
    zval          zstream_two;

    ion_stream_pair(&one, &two, zend_get_called_scope(execute_data));

    ZVAL_OBJ(&zstream_one, one);
    ZVAL_OBJ(&zstream_two, two);

    array_init(return_value);
    add_next_index_zval(return_value, &zstream_one);
    add_next_index_zval(return_value, &zstream_two);
}

METHOD_WITHOUT_ARGS(ION_Stream, pair)


/** public static function ION\Stream::socket(string $host, ION\Crypto $crypto) : self */
CLASS_METHOD(ION_Stream, socket) {
    zval        * encrypt = NULL;
    zend_string * host = NULL;
    zend_uint     state = ION_STREAM_STATE_SOCKET | ION_STREAM_STATE_ENABLED | ION_STREAM_FROM_ME;
    ion_buffer  * buffer   = NULL;
    zend_object * stream = NULL;

    ZEND_PARSE_PARAMETERS_START(1,2)
        Z_PARAM_STR(host)
        Z_PARAM_OPTIONAL
        Z_PARAM_OBJECT_EX(encrypt, 1, 0)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    errno = 0;

    if(encrypt) {
        SSL * ssl_handler = ion_crypto_client_stream_handler(Z_OBJ_P(encrypt));
        if(!ssl_handler) {
            zend_throw_exception_ex(ion_ce_ION_StreamException, 0, "Failed to setup SSL/TLS handler for stream %s", host->val);
            return;
        }
        buffer = bufferevent_openssl_socket_new(GION(base), -1, ssl_handler, BUFFEREVENT_SSL_CONNECTING, STREAM_BUFFER_DEFAULT_FLAGS | BEV_OPT_CLOSE_ON_FREE);
        state |= ION_STREAM_ENCRYPTED;
        SSL_set_ex_data(ssl_handler, GION(ssl_index), Z_OBJ_P(encrypt));
    } else {
        buffer = bufferevent_socket_new(GION(base), -1, STREAM_BUFFER_DEFAULT_FLAGS | BEV_OPT_CLOSE_ON_FREE);
    }

    if(buffer == NULL) {
        zend_throw_exception_ex(ion_ce_ION_StreamException, 0, "Error creating the socket %s", host->val);
        return;
    }

    stream = ion_stream_new_ex(buffer, state, zend_get_called_scope(execute_data));
    ion_stream_set_peer_name(stream, zend_string_copy(host));
    if(!stream) {
        if(encrypt) {
            SSL * ctx = bufferevent_openssl_get_ssl(buffer);
            SSL_set_shutdown(ctx, SSL_RECEIVED_SHUTDOWN);
            SSL_shutdown(ctx);
        }
        bufferevent_free(buffer);
        return;
    } else if(encrypt) {
        zval_add_ref(encrypt);
        ion_stream_store_encrypt(stream, Z_OBJ_P(encrypt));
    }

    if(strchr(host->val, DEFAULT_SLASH) == NULL) { // ipv4:port, [ipv6]:port, hostname:port
        pion_net_host * net_host = pion_net_host_parse(host->val, host->len);
        if(!net_host) {
            zend_object_release(stream);
            zend_throw_exception_ex(ion_ce_InvalidArgumentException, 0, "Host %s is not well-formed", host->val);
            return;
        }
        if(bufferevent_socket_connect_hostname(buffer, GION(evdns), AF_UNSPEC, net_host->hostname->val, (int)net_host->port) == FAILURE) {
            pion_net_host_free(net_host);
            zend_object_release(stream);
            if(bufferevent_socket_get_dns_error(buffer)) {
                zend_throw_exception_ex(
                        ion_ce_ION_StreamException, 0,
                        "Failed to connect to host %s: %s",
                        host->val, evutil_gai_strerror(bufferevent_socket_get_dns_error(buffer)));
            } else if(errno) {
                zend_throw_exception_ex(
                        ion_ce_ION_StreamException, 0,
                        "Failed to connect to %s: %s", host->val, strerror(errno));
            } else {
                zend_throw_exception_ex(ion_ce_ION_StreamException, 0, "Failed to connect to %s", host->val);
            }
            return;
        }
        pion_net_host_free(net_host);
    } else { // unix domain socket: /path/to/socket.sock
        char pathname[MAXPATHLEN];
        char realpath[MAXPATHLEN];
        size_t pathname_len = host->len;
        if(host->len > MAXPATHLEN) {
            zend_throw_exception_ex(ion_ce_InvalidArgumentException, 0, "Host '%s' too long", host->val);
            return;
        }
        memset(&pathname, 0, sizeof(pathname));
        memcpy(&pathname, host->val, pathname_len);
        pathname_len = zend_dirname(pathname, pathname_len);
        if(!pathname_len) {
            zend_throw_exception_ex(ion_ce_InvalidArgumentException, 0, "Host %s is not well-formed", host->val);
            return;
        }
        if(!VCWD_REALPATH(pathname, realpath)) {
            zend_throw_exception_ex(ion_ce_InvalidArgumentException, 0, "Failed to open socket %s: No such directory", host->val);
            return;
        }
        // todo
    }

    RETURN_OBJ(stream);
}

METHOD_ARGS_BEGIN(ION_Stream, socket, 1)
    METHOD_ARG_STRING(host, 0)
    METHOD_ARG_OBJECT(encrypt, ION\\Crypto, 0, 0)
METHOD_ARGS_END()

/** private function ION\Stream::_input() : void */
CLASS_METHOD(ION_Stream, _input) {

}

METHOD_WITHOUT_ARGS(ION_Stream, _input)

/** private function ION\Stream::_output() : void */
CLASS_METHOD(ION_Stream, _output) {

}

METHOD_WITHOUT_ARGS(ION_Stream, _output)

/** private function ION\Stream::_eof() : void */
CLASS_METHOD(ION_Stream, _eof) {

}

METHOD_WITHOUT_ARGS(ION_Stream, _eof)

/** private function ION\Stream::_eof() : void */
CLASS_METHOD(ION_Stream, _error) {

}

METHOD_WITHOUT_ARGS(ION_Stream, _error)

/** private function ION\Stream::_connect() : void */
CLASS_METHOD(ION_Stream, _connect) {

}

METHOD_WITHOUT_ARGS(ION_Stream, _connect)

/** public function ION\Stream::enable() : self */
CLASS_METHOD(ION_Stream, enable) {
    ion_stream * stream = get_this_instance(ion_stream);

    CHECK_STREAM_BUFFER(stream);
    if(bufferevent_enable(stream->buffer, EV_READ | EV_WRITE)) {
        zend_throw_exception(ion_ce_ION_StreamException, "Failed to enable stream", 0);
        return;
    }
    stream->state |= ION_STREAM_STATE_ENABLED;
    RETURN_THIS();
}

METHOD_WITHOUT_ARGS(ION_Stream, enable)

/** public function ION\Stream::disable() : self */
CLASS_METHOD(ION_Stream, disable) {
    ion_stream * stream = get_this_instance(ion_stream);

    CHECK_STREAM_BUFFER(stream);
    if(bufferevent_disable(stream->buffer, EV_READ | EV_WRITE) == SUCCESS) {
        stream->state &= ~ION_STREAM_STATE_ENABLED;
    }
    RETURN_THIS();
}

METHOD_WITHOUT_ARGS(ION_Stream, disable)

void _ion_stream_connect_dtor(ion_promisor * deferred) {
    ion_stream * stream = deferred->object;
    if(stream->connect) {
        zend_object_release(stream->connect);
        stream->connect = NULL;
    }
}

/** public function ION\Stream::connect() : Deferred|true */
CLASS_METHOD(ION_Stream, connect) {
    ion_stream * stream = get_this_instance(ion_stream);

    CHECK_STREAM_BUFFER(stream);

    if(stream->state & ION_STREAM_STATE_CONNECTED) {
        RETURN_TRUE;
    } else {
        zend_object * deferred = ion_promisor_deferred_new_ex(NULL);
        ion_promisor_store(deferred, stream);
        ion_promisor_dtor(deferred, _ion_stream_connect_dtor);
        stream->connect = deferred;
        obj_add_ref(deferred);
        RETURN_OBJ(deferred);
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, connect)

/** public function ION\Stream::setTimeouts(double $read_timeout, double $write_timeout) : self */
//CLASS_METHOD(ION_Stream, setTimeouts) {
//    ion_stream * stream = get_this_instance(ion_stream);
//    double read_timeout = 0.0, write_timeout = 0.0;
//    struct timeval read_tv = { 0, 0 }, write_tv = { 0, 0 };
//
//    CHECK_STREAM_BUFFER(stream);
//    ZEND_PARSE_PARAMETERS_START(2,2)
//        Z_PARAM_DOUBLE(read_timeout)
//        Z_PARAM_DOUBLE(write_timeout)
//    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);
//    if(read_timeout < 0 || write_timeout < 0) {
//        zend_throw_exception(ion_ce_InvalidArgumentException, "timeout should be unsigned", 0);
//        return;
//    }
//    SET_TIMEVAL(read_tv, read_timeout);
//    SET_TIMEVAL(write_tv, write_timeout);
//    bufferevent_set_timeouts(stream->buffer, &read_tv, &write_tv);
//    RETURN_THIS();
//}

//METHOD_ARGS_BEGIN(ION_Stream, setTimeouts, 2)
//    METHOD_ARG_DOUBLE(read_timeout, 0)
//    METHOD_ARG_DOUBLE(write_timeout, 0)
//METHOD_ARGS_END()

/** public function ION\Stream::setPriority(int $priority) : self */
CLASS_METHOD(ION_Stream, setPriority) {
    zend_long    prio = ION_PRIORITY_DEFAULT;
    ion_stream * stream = get_this_instance(ion_stream);

    CHECK_STREAM_BUFFER(stream);
    ZEND_PARSE_PARAMETERS_START(1,1)
        Z_PARAM_LONG(prio)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);
    if(prio < 0 || prio > ION_MAX_PRIORITY) {
        zend_throw_exception(ion_ce_InvalidArgumentException, "Invalid priority value", 0);
        return;
    }
    // do not check result, non-socket buffers always returns FAILURE
    ion_stream_set_priority(stream, prio);
    stream->priority = (int)prio;
    RETURN_THIS();
}

METHOD_ARGS_BEGIN(ION_Stream, setPriority, 1)
    METHOD_ARG_LONG(priority, 0)
METHOD_ARGS_END()

/** public function ION\Stream::setInputMaxSize(int $bytes) : self */
CLASS_METHOD(ION_Stream, setInputMaxSize) {
    zend_long    bytes = 0;
    ion_stream * stream = get_this_instance(ion_stream);

    CHECK_STREAM_BUFFER(stream);
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(bytes)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);
    if(bytes < 0) {
        zend_throw_exception(ion_ce_InvalidArgumentException, "The number of bytes cannot be negative", 0);
        return;
    }
    stream->input_size = (size_t)bytes;
    bufferevent_setwatermark(stream->buffer, EV_READ, stream->length, (stream->input_size >= stream->length) ? stream->input_size : stream->length);
    RETURN_THIS();
}

METHOD_ARGS_BEGIN(ION_Stream, setInputMaxSize, 1)
    METHOD_ARG_LONG(bytes, 0)
METHOD_ARGS_END()

/** public function ION\Stream::write(string $data) : self */
CLASS_METHOD(ION_Stream, write) {
    zend_string * data = NULL;
    ion_stream * stream = get_this_instance(ion_stream);

    CHECK_STREAM(stream);

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(data)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(!ZSTR_LEN(data)) {
        RETURN_THIS();
    }

    if(bufferevent_write(stream->buffer, ZSTR_VAL(data), ZSTR_LEN(data)) == FAILURE) {
        zend_throw_exception(ion_ce_ION_StreamException, "Failed to write data", 0);
        return;
    }

    if(ion_stream_output_length(stream) && (stream->state & ION_STREAM_STATE_FLUSHED)) {
        stream->state &= ~ION_STREAM_STATE_FLUSHED;
    }
    RETURN_THIS();


}

METHOD_ARGS_BEGIN(ION_Stream, write, 1)
    METHOD_ARG_STRING(data, 0)
METHOD_ARGS_END()

/** public function ION\Stream::sendFile(string $filename, int $offset = 0, int $length = -1) : bool */
CLASS_METHOD(ION_Stream, sendFile) {
    zend_string  * filename     = NULL;
    zend_long      offset       = 0;
    zend_long      length       = -1;
    int            fd;
    ion_stream   * stream = get_this_instance(ion_stream);
    struct stat    st;
    ion_evbuffer * evbuffer;

    CHECK_STREAM(stream);
    ZEND_PARSE_PARAMETERS_START(1,3)
        Z_PARAM_STR(filename)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(offset)
        Z_PARAM_LONG(length)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    errno = 0;
    fd = open(filename->val, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOATIME);
    if(fd == -1) {
        RETURN_FALSE;
    }

    if (fstat(fd, &st) == FAILURE) {
        close(fd);
        RETURN_FALSE;
    }
    if(length < 0 || length > st.st_size) {
        length = st.st_size;
    }
    evbuffer = bufferevent_get_output(stream->buffer);
    evbuffer_set_flags(evbuffer, EVBUFFER_FLAG_DRAINS_TO_FD);
    if(evbuffer_add_file(evbuffer, fd, (ev_off_t)offset, (ev_off_t)length) == FAILURE) {
        close(fd);
        RETURN_FALSE;
    }

    stream->state &= ~ION_STREAM_STATE_FLUSHED;
    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Stream, sendFile, 1)
    METHOD_ARG_STRING(filename, 0)
    METHOD_ARG_LONG(offset, 0)
    METHOD_ARG_LONG(length, 0)
METHOD_ARGS_END()

void _ion_stream_flush_dtor(ion_promisor * promisor) {
    ion_stream * stream = promisor->object;
    if(stream->flush) {
        zend_object_release(stream->flush);
        stream->flush = NULL;
    }
}

/** public function ION\Stream::flush() : ION\Deferred|true */
CLASS_METHOD(ION_Stream, flush) {
    ion_stream  * stream = get_this_instance(ion_stream);

//    CHECK_STREAM(stream);
    if(stream->flush) {
        zend_object_addref(stream->flush);
        RETURN_OBJ(stream->flush);
    }

    CHECK_STREAM_BUFFER(stream);

    if(stream->state & ION_STREAM_STATE_FLUSHED) {
        RETURN_TRUE;
    } else {
        zend_object * deferred = ion_promisor_deferred_new_ex(NULL);
        ion_promisor_store(deferred, stream);
        ion_promisor_dtor(deferred, _ion_stream_flush_dtor);
        stream->flush = deferred;
        obj_add_ref(deferred);
        RETURN_OBJ(deferred);
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, flush)

/** public function ION\Stream::search(string $token, int $offset = 0, int $length = 0) : int|bool */
CLASS_METHOD(ION_Stream, search) {
    ion_stream * stream = get_this_instance(ion_stream);
    ion_stream_token token = empty_stream_token;
    struct evbuffer * buffer;

    CHECK_STREAM_BUFFER(stream);
    ZEND_PARSE_PARAMETERS_START(1,3)
        Z_PARAM_STR(token.token)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(token.offset)
        Z_PARAM_LONG(token.length)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(!ZSTR_LEN(token.token)) {
        zend_throw_exception(ion_ce_InvalidArgumentException, "Empty token string", 0); \
        return;
    }

    buffer = bufferevent_get_input(stream->buffer);
    if(ion_stream_search_token(buffer, &token) == FAILURE) {
        zend_throw_exception(ion_ce_ION_StreamException, "Failed to get internal buffer pointer for token_length/offset", 0); \
        return;
    }

    RETURN_LONG(token.position);
}

METHOD_ARGS_BEGIN(ION_Stream, search, 1)
    METHOD_ARG_STRING(token, 0)
    METHOD_ARG_LONG(length, 0)
    METHOD_ARG_LONG(offset, 0)
METHOD_ARGS_END()

/** public function ION\Stream::getSize(int $type = self::INPUT) : string|bool */
CLASS_METHOD(ION_Stream, getSize) {
    ion_stream   * stream = get_this_instance(ion_stream);
    zend_long      type = EV_READ;
    ion_evbuffer * buffer;

    CHECK_STREAM_BUFFER(stream);
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(type)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(type == EV_READ) {
        buffer = bufferevent_get_input(stream->buffer);
    } else if(type == EV_WRITE) {
        buffer = bufferevent_get_output(stream->buffer);
    } else {
        zend_throw_exception(ion_ce_InvalidArgumentException, "Invalid buffer identify", 0); \
        return;
    }

    RETURN_LONG(evbuffer_get_length(buffer));
}

METHOD_ARGS_BEGIN(ION_Stream, getSize, 0)
    METHOD_ARG_LONG(type, 0)
METHOD_ARGS_END()


/** public function ION\Stream::get(int $bytes) : string */
CLASS_METHOD(ION_Stream, get) {
    ion_stream * stream = get_this_instance(ion_stream);
    zend_long bytes = 0;
    zend_string * data = NULL;

    CHECK_STREAM_BUFFER(stream);
    ZEND_PARSE_PARAMETERS_START(0,1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(bytes)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(!bytes) {
        RETURN_EMPTY_STRING();
    }

    data = ion_stream_read(stream, (size_t)bytes);
    if(data == NULL) {
        zend_throw_exception(ion_ce_ION_StreamException, "Stream buffer is unreachable", 0);
        return;
    }
    RETURN_STR(data);
}

METHOD_ARGS_BEGIN(ION_Stream, get, 0)
    METHOD_ARG_LONG(bytes, 0)
METHOD_ARGS_END()

/** public function ION\Stream::getAll() : string */
CLASS_METHOD(ION_Stream, getAll) {
    ion_stream  * stream = get_this_instance(ion_stream);
    zend_string * data;

    CHECK_STREAM_BUFFER(stream);
    data = ion_stream_read(stream, ion_stream_input_length(stream));
    if(data == NULL) {
        zend_throw_exception(ion_ce_ION_StreamException, "Stream buffer is unreachable", 0);
        return;
    }
    RETURN_STR(data);
}

METHOD_WITHOUT_ARGS(ION_Stream, getAll);

/** public function ION\Stream::getLine(string $token, $mode = self::MODE_TRIM_TOKEN, $max_length = 0) : string|bool */
CLASS_METHOD(ION_Stream, getLine) {
    ion_stream * stream = get_this_instance(ion_stream);
    ion_stream_token token = empty_stream_token;
    zend_string * data;

    CHECK_STREAM_BUFFER(stream);
    ZEND_PARSE_PARAMETERS_START(1,3)
        Z_PARAM_STR(token.token)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(token.flags)
        Z_PARAM_LONG(token.length)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    token.flags &= ION_STREAM_TOKEN_MODE_MASK;
    if(ZSTR_LEN(token.token) == 0) {
        RETURN_FALSE;
    }

    if(ion_stream_search_token(bufferevent_get_input(stream->buffer), &token) == FAILURE) {
        zend_throw_exception(ion_ce_ION_StreamException, "Failed to get internal buffer pointer for token_length/offset", 0);
        return;
    }

    if(token.position == -1) {
        RETURN_FALSE;
    } else {
        data = ion_stream_read_token(stream, &token);
        if(data == NULL) {
            RETURN_FALSE;
        } else {
            RETURN_STR(data);
        }
    }
}

METHOD_ARGS_BEGIN(ION_Stream, getLine, 1)
    METHOD_ARG_STRING(token, 0)
    METHOD_ARG_LONG(mode, 0)
    METHOD_ARG_LONG(max_length, 0)
METHOD_ARGS_END()

void _ion_stream_read_dtor(ion_promisor * promisor) {
    ion_stream * stream = promisor->object;
    if(stream->read) {
        if(stream->token) {
            zend_string_release(stream->token->token);
            efree(stream->token);
            stream->token = NULL;
        }
        zend_object_release(stream->read);
        stream->read = NULL;
    }
}

/** public function ION\Stream::read(int $bytes) : ION\Deferred|string */
CLASS_METHOD(ION_Stream, read) {
    ion_stream  * stream = get_this_instance(ion_stream);
    zend_long     length = 0;
    size_t current = 0;
    zend_string * data = NULL;

    CHECK_STREAM_BUFFER(stream);
    ZEND_PARSE_PARAMETERS_START(0,1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(length)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(stream->read) {
        if(stream->length == length && !stream->token) { // read same count bytes
            obj_add_ref(stream->read);
            RETURN_OBJ(stream->read);
        } else {
            zend_throw_exception(ion_ce_ION_InvalidUsageException, "Stream locked for reading: already in the process of reading", 0);
            return;
        }
    }

    current = ion_stream_input_length(stream);
    if(current >= length) {
        data = ion_stream_read(stream, (size_t)length);
        if(data == NULL) {
            zend_throw_exception(ion_ce_ION_StreamException, "Stream is unreachable", 0);
            return;
        }
        RETURN_STR(data);
    } else {
        zend_object * deferred = ion_promisor_deferred_new_ex(NULL);
        ion_promisor_store(deferred, stream);
        ion_promisor_dtor(deferred, _ion_stream_read_dtor);
        stream->read = deferred;
        stream->length = (size_t)length;
        bufferevent_setwatermark(stream->buffer, EV_READ, (size_t)length, (stream->input_size >= length) ? stream->input_size : (size_t)length);
        obj_add_ref(deferred);
        RETURN_OBJ(deferred);
    }
}

METHOD_ARGS_BEGIN(ION_Stream, read, 1)
    METHOD_ARG_LONG(bytes, 0)
METHOD_ARGS_END()

/** public function ION\Stream::readLine(string $token, $mode = self::MODE_TRIM_TOKEN, $max_length = 0) : ION\Deferred */
CLASS_METHOD(ION_Stream, readLine) {
    ion_stream       * stream = get_this_instance(ion_stream);
    ion_stream_token   token = empty_stream_token;
    zend_string      * data;

    CHECK_STREAM_BUFFER(stream);
    ZEND_PARSE_PARAMETERS_START(1,3)
        Z_PARAM_STR(token.token)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(token.flags)
        Z_PARAM_LONG(token.length)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    token.flags &= ION_STREAM_TOKEN_MODE_MASK;
    if(ZSTR_LEN(token.token) == 0) {
        zend_throw_exception(ion_ce_ION_StreamException, "Failed to get internal buffer pointer for token_length/offset", 0);
        return;
    }

    if(stream->read) {
        if(stream->token
           && zend_string_equals(stream->token->token, token.token)
           && (stream->token->flags & ION_STREAM_TOKEN_MODE_MASK) == token.flags
           && stream->token->length == token.length) {
            obj_add_ref(stream->read);
            RETURN_OBJ(stream->read);
        } else {
            zend_throw_exception(ion_ce_ION_InvalidUsageException, "Stream locked for reading: already in the process of reading", 0);
            return;
        }
    }

    if(ion_stream_search_token(bufferevent_get_input(stream->buffer), &token) == FAILURE) {
        zend_throw_exception(ion_ce_ION_StreamException, "Failed to get internal buffer pointer for token_length/offset", 0);
        return;
    }

    if(token.position == -1) {
        if(token.flags & ION_STREAM_TOKEN_LIMIT) {
            RETURN_FALSE;
        } else {
            zend_object * deferred = ion_promisor_deferred_new_ex(NULL);
            ion_promisor_store(deferred, stream);
            ion_promisor_dtor(deferred, _ion_stream_read_dtor);
            stream->read = deferred;
            stream->token = emalloc(sizeof(ion_stream_token));
            memcpy(stream->token, &token, sizeof(ion_stream_token));
            stream->token->token = zend_string_copy(token.token);
            obj_add_ref(deferred);
            RETURN_OBJ(deferred);
        }
    } else {
        data = ion_stream_read_token(stream, &token);
        if(data == NULL) {
            RETURN_FALSE;
        } else {
            RETURN_STR(data);
        }
    }

}

METHOD_ARGS_BEGIN(ION_Stream, readLine, 1)
    METHOD_ARG_STRING(token, 0)
    METHOD_ARG_LONG(mode, 0)
    METHOD_ARG_LONG(max_length, 0)
METHOD_ARGS_END()

/** public function ION\Stream::readAll() : ION\Deferred|string */
CLASS_METHOD(ION_Stream, readAll) {
    ion_stream  * stream = get_this_instance(ion_stream);
    size_t        current = 0;
    zend_string * data = NULL;

    CHECK_STREAM_BUFFER(stream);
    if(stream->read) {
        if(!stream->token && !stream->length) {
            obj_add_ref(stream->read);
            RETURN_OBJ(stream->read);
        } else {
            zend_throw_exception(ion_ce_ION_InvalidUsageException, "Stream locked for reading: already in the process of reading", 0);
            return;
        }
    }
    if(stream->state & ION_STREAM_STATE_EOF) {
        current = ion_stream_input_length(stream);
        if(current) {
            data = ion_stream_read(stream, current);
            if(data) {
                RETURN_STR(data);
            } else {
                zend_throw_exception(ion_ce_ION_StreamException, "Stream buffer is unreachable", 0);
                return;
            }
        } else {
            RETURN_EMPTY_STRING();
        }
    } else {
        zend_object * deferred = ion_promisor_deferred_new_ex(NULL);
        ion_promisor_store(deferred, stream);
        ion_promisor_dtor(deferred, _ion_stream_read_dtor);
        stream->read = deferred;
        stream->length = 0;
        obj_add_ref(deferred);
        RETURN_OBJ(deferred);
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, readAll)

/** public function ION\Stream::shutdown(bool $force = false) : self */
CLASS_METHOD(ION_Stream, shutdown) {
    ion_stream * stream = get_this_instance(ion_stream);
    zend_bool force = 0;

    CHECK_STREAM_BUFFER(stream);
    ZEND_PARSE_PARAMETERS_START(0,1)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(force)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(stream->state & ION_STREAM_STATE_CLOSED) {
        RETURN_THIS();
    }
    if((stream->state & ION_STREAM_STATE_FLUSHED) || force) {
        ion_stream_close_fd(stream);
    } else {
        bufferevent_disable(stream->buffer, EV_READ);
        stream->state |= ION_STREAM_STATE_CLOSE_ON_FLUSH;
        RETURN_THIS();
    }
}

METHOD_ARGS_BEGIN(ION_Stream, shutdown, 0)
    METHOD_ARG_BOOL(force, 0)
METHOD_ARGS_END()

/** public function ION\Stream::incoming() : Sequence */
CLASS_METHOD(ION_Stream, incoming) {
    ion_stream * stream = get_this_instance(ion_stream);
    CHECK_STREAM_BUFFER(stream);

    if(!stream->incoming) {
        stream->incoming = ion_promisor_sequence_new(NULL);
    }
    obj_add_ref(stream->incoming);
    RETURN_OBJ(stream->incoming);
}

METHOD_WITHOUT_ARGS(ION_Stream, incoming)

/** public function ION\Stream::suspend() : self */
CLASS_METHOD(ION_Stream, suspend) {
    ion_stream * stream = get_this_instance(ion_stream);
    ion_stream_suspend(stream);
    RETURN_THIS();
}

METHOD_WITHOUT_ARGS(ION_Stream, suspend)

/** public function ION\Stream::resume() : self */
CLASS_METHOD(ION_Stream, resume) {
    ion_stream * stream = get_this_instance(ion_stream);
    ion_stream_resume(stream);
    RETURN_THIS();
}

METHOD_WITHOUT_ARGS(ION_Stream, resume)

void _deferred_stream_closing_dtor(ion_promisor * deferred) {
    ion_stream * stream = deferred->object;
    if(stream->shutdown) {
        zend_object_release(stream->shutdown);
        stream->shutdown = NULL;
    }
}

/** public function ION\Stream::closed() : ION\Deferred|self */
CLASS_METHOD(ION_Stream, closed) {
    ion_stream * stream = get_this_instance(ion_stream);

    CHECK_STREAM_BUFFER(stream);
    if(stream->shutdown) {
        obj_add_ref(stream->shutdown);
        RETURN_OBJ(stream->shutdown);
    }
    if(stream->state & ION_STREAM_STATE_CLOSED) {
        RETURN_THIS();
    } else {
        zend_object * deferred = ion_promisor_deferred_new_ex(NULL);
        ion_promisor_dtor(deferred, _deferred_stream_closing_dtor);
        ion_promisor_store(deferred, stream);
        stream->shutdown = deferred;
        obj_add_ref(deferred);
        RETURN_OBJ(deferred);
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, closed)

/** public function ION\Stream::encrypt(ION\Crypto $encrypt) : self */
CLASS_METHOD(ION_Stream, encrypt) {
    ion_stream * stream = get_this_instance(ion_stream);
    zval       * encrypt = NULL;
    SSL        * ssl_handler = NULL;
    enum bufferevent_ssl_state ssl_state;

    if(stream->state & ION_STREAM_ENCRYPTED) {
        zend_throw_exception(ion_ce_ION_StreamException, "Stream already has encryption", 0);
        return;
    }

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT(encrypt)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(ion_crypto_check_is_client(Z_OBJ_P(encrypt))) {
        ssl_handler = ion_crypto_client_stream_handler(Z_OBJ_P(encrypt));
        ssl_state   = BUFFEREVENT_SSL_CONNECTING;
    } else {
        ssl_handler = ion_crypto_server_stream_handler(Z_OBJ_P(encrypt));
        ssl_state   = BUFFEREVENT_SSL_ACCEPTING;
    }

    if(!ssl_handler) {
        zend_throw_exception(ion_ce_ION_StreamException, "Failed to setup SSL/TLS handler", 0);
        return;
    }
    stream->buffer = bufferevent_openssl_filter_new(GION(base), stream->buffer, ssl_handler, ssl_state, STREAM_BUFFER_DEFAULT_FLAGS | BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(stream->buffer, _ion_stream_input, _ion_stream_output, _ion_stream_notify, (void *) stream);
    if(stream->state & ION_STREAM_STATE_ENABLED) {
        bufferevent_enable(stream->buffer, EV_READ | EV_WRITE);
    }
    if(stream->priority > 0) {
        bufferevent_priority_set(stream->buffer, stream->priority);
    }
    stream->state |= ION_STREAM_ENCRYPTED | ION_STREAM_HAS_UNDERLYING;
    stream->encrypt = Z_OBJ_P(encrypt);
    zval_add_ref(encrypt);
    SSL_set_ex_data(ssl_handler, GION(ssl_index), Z_OBJ_P(encrypt));

    RETURN_THIS();
}

METHOD_ARGS_BEGIN(ION_Stream, encrypt, 1)
    METHOD_ARG_OBJECT(ssl, ION\\Crypto, 0, 0)
METHOD_ARGS_END()

/** public function ION\Stream::isClosed() : int */
CLASS_METHOD(ION_Stream, isClosed) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->state & ION_STREAM_STATE_CLOSED) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, isClosed)

/** public function ION\Stream::isEnabled() : bool */
CLASS_METHOD(ION_Stream, isEnabled) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->state & ION_STREAM_STATE_ENABLED) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, isEnabled)

/** public function ION\Stream::isReading() : bool */
CLASS_METHOD(ION_Stream, isReading) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->read) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, isReading)

/** public function ION\Stream::hasData() : bool */
CLASS_METHOD(ION_Stream, hasData) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->state & ION_STREAM_STATE_HAS_DATA) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, hasData)

/** public function ION\Stream::hasEOF() : bool */
CLASS_METHOD(ION_Stream, hasEOF) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->state & ION_STREAM_STATE_EOF) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, hasEOF)

/** public function ION\Stream::wasShutdown() : bool */
CLASS_METHOD(ION_Stream, wasShutdown) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->state & ION_STREAM_STATE_SHUTDOWN) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, wasShutdown)

/** public function ION\Stream::isFlushed() : bool */
CLASS_METHOD(ION_Stream, isFlushed) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->state & ION_STREAM_STATE_FLUSHED) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, isFlushed)

/** public function ION\Stream::isConnected() : bool */
CLASS_METHOD(ION_Stream, isConnected) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->state & ION_STREAM_STATE_CONNECTED) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, isConnected)



/** public function ION\Stream::__debugInfo() : void */
CLASS_METHOD(ION_Stream, __debugInfo) {
    ion_stream  * stream = get_this_instance(ion_stream);
    zval          read;
    zend_string * address = NULL;

    array_init(return_value);
    add_assoc_long(return_value, "fd", bufferevent_getfd(stream->buffer));
    if(stream->state & ION_STREAM_STATE_PAIR) {
        add_assoc_string(return_value, "type", "pair-socket");
    } else if(stream->state & ION_STREAM_STATE_SOCKET) {
        add_assoc_string(return_value, "type", "socket");
    } else {
        add_assoc_string(return_value, "type", "pipe");
    }
    add_assoc_bool(return_value, "connected",    stream->state & ION_STREAM_STATE_CONNECTED);
    add_assoc_bool(return_value, "ssl",          0);
    add_assoc_long(return_value, "input_bytes",  (zend_long)ion_stream_input_length(stream));
    add_assoc_long(return_value, "output_bytes", (zend_long)ion_stream_output_length(stream));

    address = ion_stream_get_name_self(&stream->std);
    if(address) {
        add_assoc_str(return_value,  "local_name", address);
    } else {
        add_assoc_bool(return_value, "local_name", 0);
    }

    address = ion_stream_get_name_remote(&stream->std);
    if(address) {
        add_assoc_str(return_value,  "remote_peer", address);
    } else {
        add_assoc_bool(return_value, "remote_peer", 0);
    }

    if(stream->state & ION_STREAM_STATE_CLOSED) {
        if(stream->state & ION_STREAM_STATE_EOF) {
            add_assoc_string(return_value, "closed", "eof");
        } else if (stream->state & ION_STREAM_STATE_ERROR) {
            add_assoc_string(return_value, "closed", "error");
        } else {
            add_assoc_string(return_value, "closed", "shutdown");
        }

    } else {
        add_assoc_bool(return_value, "closed", 0);
    }
    if(stream->read) {
        array_init(&read);
        if(stream->token) {
            add_assoc_str(&read, "token", zend_string_copy(stream->token->token));
            add_assoc_long(&read, "max_bytes", stream->token->length);
            add_assoc_long(&read, "scanned_bytes", stream->token->offset);
            if(stream->token->flags & ION_STREAM_MODE_TRIM_TOKEN) {
                add_assoc_string(&read, "mode", "trim_token");
            } else if(stream->token->flags & ION_STREAM_MODE_WITH_TOKEN) {
                add_assoc_string(&read, "mode", "with_token");
            } else {
                add_assoc_string(&read, "mode", "without_token");
            }
        } else if(stream->length) {
            add_assoc_long(&read, "bytes", (zend_long)stream->length);
        } else {
            add_assoc_bool(&read, "all",   1);
        }
        add_assoc_zval(return_value, "read", &read);
    } else {
        add_assoc_bool(return_value, "read", 0);
    }

    add_assoc_bool(return_value, "await_flush",    stream->flush ? 1 : 0);
    add_assoc_bool(return_value, "await_connect",  stream->connect ? 1 : 0);
    add_assoc_bool(return_value, "await_shutdown", stream->shutdown ? 1 : 0);

}

METHOD_WITHOUT_ARGS(ION_Stream, __debugInfo)

/** public function ION\Stream::__destruct() : void */
CLASS_METHOD(ION_Stream, __destruct) {
//    ion_stream * stream = get_this_instance(ion_stream);
//    if(stream->flush) {
//        ion_deferred_cancel(stream->flush, "The stream shutdown by the destructor");
//    }
//    if(stream->read) {
//        ion_deferred_cancel(stream->read, "The stream shutdown by the destructor");
//    }
//    if(stream->connect) {
//        ion_deferred_cancel(stream->connect, "The stream shutdown by the destructor");
//    }
//    if(stream->state & ION_STREAM_STATE_ENABLED) {
//        bufferevent_disable(stream->buffer, EV_READ | EV_WRITE);
//        stream->state &= ~ION_STREAM_STATE_ENABLED;
//    }
}

METHOD_WITHOUT_ARGS(ION_Stream, __destruct)


/** public function ION\Stream::getPeerName() : string */
CLASS_METHOD(ION_Stream, getPeerName) {
    ion_stream  * stream = get_this_instance(ion_stream);
    zend_string * remote_name;
    if(stream->buffer == NULL) {
        RETURN_FALSE;
    }
    if(stream->state & ION_STREAM_STATE_SOCKET) {
        if(ion_stream_is_valid_fd(stream)) {
            RETURN_FALSE;
        }
        if(stream->state & ION_STREAM_STATE_PAIR) {
            RETURN_STRING("twin");
        }
        remote_name = ion_stream_get_name_remote(Z_OBJ_P(getThis()));
        if(remote_name) {
            RETURN_STR(remote_name);
        } else {
            RETURN_FALSE;
        }
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, getPeerName)

/** public function ION\Stream::getLocalName() : string */
CLASS_METHOD(ION_Stream, getLocalName) {
    ion_stream * stream = get_this_instance(ion_stream);
    zend_string * local_name;
    if(stream->buffer == NULL) {
        RETURN_FALSE;
    }
    if(stream->state & ION_STREAM_STATE_SOCKET) {
        if(ion_stream_is_valid_fd(stream)) {
            RETURN_FALSE;
        }
        if(stream->state & ION_STREAM_STATE_PAIR) {
            RETURN_STRING("twin")
        }
        local_name = ion_stream_get_name_self(Z_OBJ_P(getThis()));
        if(local_name) {
            RETURN_STR(local_name);
        } else {
            RETURN_FALSE;
        }
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, getLocalName)

/** public function ION\Stream::getID() : int */
CLASS_METHOD(ION_Stream, getID) {
    ion_stream * stream = get_this_instance(ion_stream);
    RETURN_LONG(bufferevent_getfd(stream->buffer));
}

METHOD_WITHOUT_ARGS(ION_Stream, getID)

/** public function ION\Stream::hasError() : string */
CLASS_METHOD(ION_Stream, hasError) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->error) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, hasError)

/** public function ION\Stream::getError() : string */
CLASS_METHOD(ION_Stream, getError) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->error) {
        zend_object_addref(stream->error);
        RETURN_OBJ(stream->error);
    } else {
        RETURN_NULL();
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, getError)

/** public function ION\Stream::getType() : string */
CLASS_METHOD(ION_Stream, getType) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->state & ION_STREAM_STATE_PAIR) {
        RETURN_STRING("pair_socket");
    } else if(stream->state & ION_STREAM_STATE_SOCKET) {
        RETURN_STRING("socket");
    } else {
        RETURN_STRING("stream");
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, getType)

/** public function ION\Stream::__toString() : string */
CLASS_METHOD(ION_Stream, __toString) {
    RETURN_STR(ion_stream_describe(Z_OBJ_P(getThis())));
}

METHOD_WITHOUT_ARGS(ION_Stream, __toString)

/** public function ION\Stream::appendToInput(string $data) : self */
CLASS_METHOD(ION_Stream, appendToInput) {
    zend_string * data = NULL;
    ion_stream * stream = get_this_instance(ion_stream);
    struct evbuffer * input;

    CHECK_STREAM(stream);
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(data)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(!data->len) {
        RETURN_THIS();
    }

    input = bufferevent_get_input(stream->buffer);

    evbuffer_unfreeze(input, 0);
    if(evbuffer_add(input, (const void *)data->val, (size_t)data->len)) {
        zend_throw_exception(ion_ce_ION_StreamException, "Failed to append data to input", 0);
        return;
    }
    stream->state &= ~ION_STREAM_STATE_FLUSHED;
    RETURN_THIS();
}

METHOD_ARGS_BEGIN(ION_Stream, appendToInput, 1)
    METHOD_ARG_STRING(data, 0)
METHOD_ARGS_END()

/** public function ION\Stream::hasStorage() : string */
CLASS_METHOD(ION_Stream, hasStorage) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->storage) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, hasStorage)

/** public function ION\Stream::getStorage() : string */
CLASS_METHOD(ION_Stream, getStorage) {
    ion_stream * stream = get_this_instance(ion_stream);
    if(stream->storage) {
        zend_object_addref(stream->storage);
        RETURN_OBJ(stream->storage);
    } else {
        RETURN_NULL();
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, getStorage)


/** public function ION\Stream::release() : string */
CLASS_METHOD(ION_Stream, release) {
    ion_stream * stream = get_this_instance(ion_stream);
    zend_bool    force = 0;

    if(stream->storage) {
        ion_storage_handler_release(stream->storage, &stream->std);
    } else {
        if((stream->state & ION_STREAM_STATE_FLUSHED) || force) {
            ion_stream_close_fd(stream);
        } else {
            bufferevent_disable(stream->buffer, EV_READ);
            stream->state |= ION_STREAM_STATE_CLOSE_ON_FLUSH;
            RETURN_THIS();
        }
    }
}

METHOD_WITHOUT_ARGS(ION_Stream, release)

CLASS_METHODS_START(ION_Stream)
    // Factories
    METHOD(ION_Stream, resource,           ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Stream, pair,               ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Stream, socket,             ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)

    // Events
    METHOD(ION_Stream, _input,             ZEND_ACC_PRIVATE)
    METHOD(ION_Stream, _output,            ZEND_ACC_PRIVATE)
    METHOD(ION_Stream, _eof,               ZEND_ACC_PRIVATE)
    METHOD(ION_Stream, _connect,           ZEND_ACC_PRIVATE)
    METHOD(ION_Stream, _error,             ZEND_ACC_PRIVATE)

    METHOD(ION_Stream, connect,            ZEND_ACC_PUBLIC)

    // Configuration
    METHOD(ION_Stream, setPriority,        ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, setInputMaxSize,    ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, enable,             ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, disable,            ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, encrypt,            ZEND_ACC_PUBLIC)

    // Writing
    METHOD(ION_Stream, write,              ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, appendToInput,      ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, sendFile,           ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, flush,              ZEND_ACC_PUBLIC)

    // Get info
    METHOD(ION_Stream, search,             ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, getSize,            ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, getPeerName,        ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, getLocalName,       ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, getID,              ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, getError,           ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, __debugInfo,        ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, getType,            ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, __toString,         ZEND_ACC_PUBLIC)

    // Reading
    METHOD(ION_Stream, get,                ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, getAll,             ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, getLine,            ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, read,               ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, readAll,            ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, readLine,           ZEND_ACC_PUBLIC)

    // Close stream
    METHOD(ION_Stream, closed,             ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, shutdown,           ZEND_ACC_PUBLIC)

    // Handle incoming data
    METHOD(ION_Stream, incoming,           ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, suspend,            ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, resume,             ZEND_ACC_PUBLIC)

    // Check states
    METHOD(ION_Stream, isEnabled,          ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, isConnected,        ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, isReading,          ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, hasData,            ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, isFlushed,          ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, hasError,           ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, hasEOF,             ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, isClosed,           ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, wasShutdown,        ZEND_ACC_PUBLIC)

    // Misc
    METHOD(ION_Stream, __destruct,         ZEND_ACC_PUBLIC)

    // Storage
    METHOD(ION_Stream, hasStorage,         ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, getStorage,         ZEND_ACC_PUBLIC)
    METHOD(ION_Stream, release,            ZEND_ACC_PUBLIC)

CLASS_METHODS_END;

PHP_MINIT_FUNCTION(ION_Stream) {
    pion_register_class(ION_Stream, "ION\\Stream", ion_stream_init, CLASS_METHODS(ION_Stream));
    pion_init_std_object_handlers(ION_Stream);
    pion_set_object_handler(ION_Stream, free_obj, ion_stream_free);
    pion_set_object_handler(ION_Stream, clone_obj, NULL);

    PION_CLASS_CONST_LONG(ION_Stream, "MODE_TRIM_TOKEN",    ION_STREAM_MODE_TRIM_TOKEN);
    PION_CLASS_CONST_LONG(ION_Stream, "MODE_WITH_TOKEN",    ION_STREAM_MODE_WITH_TOKEN);
    PION_CLASS_CONST_LONG(ION_Stream, "MODE_WITHOUT_TOKEN", ION_STREAM_MODE_WITHOUT_TOKEN);

//    PION_CLASS_CONST_LONG(ION_Stream, "FROM_PEER",   ION_STREAM_FROM_PEER);
//    PION_CLASS_CONST_LONG(ION_Stream, "FROM_ME",     ION_STREAM_FROM_ME);

    PION_CLASS_CONST_LONG(ION_Stream, "INPUT",  EV_READ);
    PION_CLASS_CONST_LONG(ION_Stream, "OUTPUT", EV_WRITE);
    PION_CLASS_CONST_LONG(ION_Stream, "BOTH",   EV_WRITE | EV_READ);

    PION_REGISTER_VOID_EXTENDED_CLASS(ION_StreamException, ion_ce_ION_RuntimeException, "ION\\StreamException");

    return SUCCESS;
}

PHP_RINIT_FUNCTION(ION_Stream) {
    ion_buffer * buffer = NULL;

    buffer = bufferevent_socket_new(GION(base), STDIN_FILENO, STREAM_BUFFER_DEFAULT_FLAGS);
    GION(input) = ion_stream_new_ex(buffer, ION_STREAM_STATE_CONNECTED | ION_STREAM_RESERVED, ion_ce_ION_Stream);

    buffer = bufferevent_socket_new(GION(base), STDOUT_FILENO, STREAM_BUFFER_DEFAULT_FLAGS);
    GION(output) = ion_stream_new_ex(buffer, ION_STREAM_STATE_CONNECTED | ION_STREAM_RESERVED, ion_ce_ION_Stream);

    buffer = bufferevent_socket_new(GION(base), STDERR_FILENO, STREAM_BUFFER_DEFAULT_FLAGS);
    GION(error) = ion_stream_new_ex(buffer, ION_STREAM_STATE_CONNECTED | ION_STREAM_RESERVED, ion_ce_ION_Stream);

    stream_input   = pion_cb_fetch_method("ION\\Stream", "_input");
    stream_output  = pion_cb_fetch_method("ION\\Stream", "_output");
    stream_eof     = pion_cb_fetch_method("ION\\Stream", "_eof");
    stream_error   = pion_cb_fetch_method("ION\\Stream", "_error");
    stream_connect = pion_cb_fetch_method("ION\\Stream", "_connect");

    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(ION_Stream) {
    zend_object_release(GION(input));
    zend_object_release(GION(output));
    zend_object_release(GION(error));

    pion_cb_free(stream_input);
    pion_cb_free(stream_output);
    pion_cb_free(stream_eof);
    pion_cb_free(stream_error);
    pion_cb_free(stream_connect);

    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(ION_Stream) {
    return SUCCESS;
}