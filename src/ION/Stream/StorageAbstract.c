#include "../../pion.h"


zend_object_handlers ion_oh_ION_Stream_StorageAbstract;
zend_class_entry * ion_ce_ION_Stream_StorageAbstract;

zend_object_handlers ion_oh_ION_Net_Socket_StorageException;
zend_class_entry * ion_ce_ION_Net_Socket_StorageException;

static void ion_storage_incoming_handler(zend_object * connect) {
    ion_stream   * stream = get_object_instance(connect, ion_stream);
    ion_storage  * server = get_object_instance(stream->storage, ion_storage);

    server->total_resumed++;

    ion_stream_suspend(stream);

    if(server->incoming) {
        zval connect_container;
        ZVAL_OBJ(&connect_container, connect);
        ion_promisor_sequence_invoke(server->incoming, &connect_container);
    } else {
        // todo: autofree
    }
}

static void ion_storage_close_handler(zend_object * connect) {
    ion_stream   * stream = get_object_instance(connect, ion_stream);
    ion_storage  * server = get_object_instance(stream->storage, ion_storage);
    zend_string  * name = ion_stream_get_name_remote(connect);
    if(!name) {
        return;
    }
    zend_hash_str_del(server->conns, name->val, name->len);
    zend_string_release(name);
}

static void ion_storage_timeout_handler(zend_object * connect) {
    ion_stream   * stream = get_object_instance(connect, ion_stream);
    ion_storage  * server = get_object_instance(stream->storage, ion_storage);
    if(server->close) {
        zval connect_container;
        ZVAL_OBJ(&connect_container, connect);
        ion_promisor_sequence_invoke(server->close, &connect_container);
    } else {
        ion_stream_close_fd(stream);
    }
}

static void ion_storage_ping_handler(zend_object * connect) {
    ion_stream   * stream = get_object_instance(connect, ion_stream);
    ion_storage  * server = get_object_instance(stream->storage, ion_storage);
    if(server->ping) {
        zval connect_container;
        ZVAL_OBJ(&connect_container, connect);
        ion_promisor_sequence_invoke(server->ping, &connect_container);
    }
}

zend_object * ion_storage_init(zend_class_entry * ce) {
    ion_storage * server = ecalloc(1, sizeof(ion_storage));

    ALLOC_HASHTABLE(server->listeners);
    zend_hash_init(server->listeners, 8, NULL, _zval_dtor_wrapper, 0);
    ALLOC_HASHTABLE(server->conns);
    zend_hash_init(server->conns, 1024, NULL, _zval_dtor_wrapper, 0);

//    server->handshake_handler = ion_storage_connect_handler;
    server->incoming_handler  = ion_storage_incoming_handler;
    server->release_handler   = NULL;
    server->close_handler     = ion_storage_close_handler;
    server->timeout_handler   = ion_storage_timeout_handler;
    server->ping_handler      = ion_storage_ping_handler;
    RETURN_INSTANCE(ION_Stream_StorageAbstract, server);
}

void ion_storage_free(zend_object * object) {
    ion_storage * server = get_object_instance(object, ion_storage);
    zend_object_std_dtor(object);
    zend_hash_clean(server->listeners);
    zend_hash_destroy(server->listeners);
    FREE_HASHTABLE(server->listeners);
    zend_hash_clean(server->conns);
    zend_hash_destroy(server->conns);
    FREE_HASHTABLE(server->conns);
    if(server->handshake) {
        zend_object_release(server->handshake);
    }
    if(server->incoming) {
        zend_object_release(server->incoming);
    }
    if(server->timeout) {
        zend_object_release(server->timeout);
    }
    if(server->close) {
        zend_object_release(server->close);
    }
    if(server->group) {
        bufferevent_rate_limit_group_free(server->group);
    }
}


/** public function ION\Server::setMaxPoolSize(int $max) : self  */
CLASS_METHOD(ION_Stream_StorageAbstract, setMaxPoolSize) {
    zend_long max = -1;
    ion_storage * server = get_this_instance(ion_storage);

    ZEND_PARSE_PARAMETERS_START(1, 1)
            Z_PARAM_LONG(max)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    zend_hash_extend(server->conns, (uint32_t)max, 0);

    server->max_conns = max;

    RETURN_THIS();
}

METHOD_ARGS_BEGIN(ION_Stream_StorageAbstract, setMaxPoolSize, 1)
    METHOD_ARG_LONG(max, 0)
METHOD_ARGS_END();


/** public function ION\Server::setIdleTimeout(int $sec) : self  */
CLASS_METHOD(ION_Stream_StorageAbstract, setIdleTimeout) {
    zend_long     sec = -1;
    ion_storage * storage = get_this_instance(ion_storage);

    ZEND_PARSE_PARAMETERS_START(1, 1)
         Z_PARAM_LONG(sec)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    storage->idle_timeout = (zend_uint)sec;

    RETURN_THIS();
}

METHOD_ARGS_BEGIN(ION_Stream_StorageAbstract, setIdleTimeout, 1)
        METHOD_ARG_LONG(sec, 0)
METHOD_ARGS_END();


/** public function ION\Server::setPriority(int $priority) : self  */
CLASS_METHOD(ION_Stream_StorageAbstract, setPriority) {
    zend_long     prio = ION_PRIORITY_DEFAULT;
    ion_storage * storage = get_this_instance(ion_storage);

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(prio)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(prio < 0 || prio > 6) {
        zend_throw_exception(ion_ce_InvalidArgumentException, "Invalid priority value", 0);
        return;
    }

    storage->priority = (int)prio;

    RETURN_THIS();
}

METHOD_ARGS_BEGIN(ION_Stream_StorageAbstract, setPriority, 1)
    METHOD_ARG_LONG(priority, 0)
METHOD_ARGS_END();


/** public function ION\Server::setInputSize(int $size) : self  */
CLASS_METHOD(ION_Stream_StorageAbstract, setInputSize) {
    zend_long     size = ION_PRIORITY_DEFAULT;
    ion_storage * storage = get_this_instance(ion_storage);

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(size)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(size < 0 || size > EV_SIZE_MAX) {
        zend_throw_exception(ion_ce_InvalidArgumentException, "Invalid size value", 0);
        return;
    }

    storage->input_buffer_size = (size_t)size;

    RETURN_THIS();
}

METHOD_ARGS_BEGIN(ION_Stream_StorageAbstract, setInputSize, 1)
    METHOD_ARG_LONG(size, 0)
METHOD_ARGS_END();



/** public function ION\Server::setPingInterval(int $ping_interval, int $ping_timeout) : self  */
CLASS_METHOD(ION_Stream_StorageAbstract, setPingInterval) {
    zend_long     ping_interval = 0;
    zend_long     ping_timeout = 0;
    ion_storage * storage = get_this_instance(ion_storage);

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_LONG(ping_interval)
        Z_PARAM_LONG(ping_timeout)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    storage->ping_interval = (zend_uint)ping_interval;
    storage->ping_timeout  = (zend_uint)ping_timeout;

    RETURN_THIS();
}

METHOD_ARGS_BEGIN(ION_Stream_StorageAbstract, setPingInterval, 2)
    METHOD_ARG_LONG(ping_interval, 0)
    METHOD_ARG_LONG(ping_timeout, 0)
METHOD_ARGS_END();

/** public function ION\Server::handshake() : Sequence  */
CLASS_METHOD(ION_Stream_StorageAbstract, handshake) {
    ion_storage * storage = get_this_instance(ion_storage);
    if(!storage->handshake) {
        storage->handshake = ion_promisor_sequence_new(NULL);
    }
    zend_object_addref(storage->handshake);
    RETURN_OBJ(storage->handshake);
}

METHOD_WITHOUT_ARGS(ION_Stream_StorageAbstract, handshake)

/** public function ION\Server::incoming() : Sequence  */
CLASS_METHOD(ION_Stream_StorageAbstract, incoming) {
    ion_storage * storage = get_this_instance(ion_storage);
    if(!storage->incoming) {
        storage->incoming = ion_promisor_sequence_new(NULL);
    }
    zend_object_addref(storage->incoming);
    RETURN_OBJ(storage->incoming);
}

METHOD_WITHOUT_ARGS(ION_Stream_StorageAbstract, incoming)


/** public function ION\Server::timeout() : Sequence  */
CLASS_METHOD(ION_Stream_StorageAbstract, timeout) {
    ion_storage * storage = get_this_instance(ion_storage);
    if(!storage->timeout) {
        storage->timeout = ion_promisor_sequence_new(NULL);
    }
    zend_object_addref(storage->timeout);
    RETURN_OBJ(storage->timeout);
}

METHOD_WITHOUT_ARGS(ION_Stream_StorageAbstract, timeout)


/** public function ION\Server::close() : Sequence  */
CLASS_METHOD(ION_Stream_StorageAbstract, close) {
    ion_storage * storage = get_this_instance(ion_storage);
    if(!storage->close) {
        storage->close = ion_promisor_sequence_new(NULL);
    }
    zend_object_addref(storage->close);
    RETURN_OBJ(storage->close);
}

METHOD_WITHOUT_ARGS(ION_Stream_StorageAbstract, close)


/** public function ION\Server::ping() : Sequence  */
CLASS_METHOD(ION_Stream_StorageAbstract, ping) {
    ion_storage * storage = get_this_instance(ion_storage);
    if(!storage->ping) {
        storage->ping = ion_promisor_sequence_new(NULL);
    }
    zend_object_addref(storage->ping);
    RETURN_OBJ(storage->ping);
}

METHOD_WITHOUT_ARGS(ION_Stream_StorageAbstract, ping)

/** public function ION\Server::hasStream($peer_name) : Sequence  */
CLASS_METHOD(ION_Stream_StorageAbstract, hasStream) {
    ion_storage * storage = get_this_instance(ion_storage);
    zend_string * peer_name = NULL;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(peer_name)
    ZEND_PARSE_PARAMETERS_END_EX(PION_ZPP_THROW);

    if(zend_hash_exists(storage->conns, peer_name)) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE;
    }
}

METHOD_ARGS_BEGIN(ION_Stream_StorageAbstract, hasStream, 1)
    METHOD_ARG_STRING(peer_name, 0)
METHOD_ARGS_END();


/** public function ION\Server::getStats() : array  */
CLASS_METHOD(ION_Stream_StorageAbstract, getStats) {
    ion_storage * storage = get_this_instance(ion_storage);

    array_init(return_value);
    add_assoc_long(return_value, "streams_count", zend_hash_num_elements(storage->conns));
    add_assoc_long(return_value, "idle_stream_count", 0);
    add_assoc_long(return_value, "handled_streams", storage->total_conns);
    add_assoc_long(return_value, "read",    storage->total_read);
    add_assoc_long(return_value, "written", storage->total_written);
    add_assoc_long(return_value, "resumed_streams", storage->total_resumed);
}

METHOD_WITHOUT_ARGS(ION_Stream_StorageAbstract, getStats)


CLASS_METHODS_START(ION_Stream_StorageAbstract)
    METHOD(ION_Stream_StorageAbstract, setMaxPoolSize,   ZEND_ACC_PUBLIC)
    METHOD(ION_Stream_StorageAbstract, setIdleTimeout,   ZEND_ACC_PUBLIC)
    METHOD(ION_Stream_StorageAbstract, setPriority,      ZEND_ACC_PUBLIC)
    METHOD(ION_Stream_StorageAbstract, setInputSize,     ZEND_ACC_PUBLIC)
    METHOD(ION_Stream_StorageAbstract, setPingInterval,  ZEND_ACC_PUBLIC)

    METHOD(ION_Stream_StorageAbstract, handshake,        ZEND_ACC_PUBLIC)
    METHOD(ION_Stream_StorageAbstract, incoming,         ZEND_ACC_PUBLIC)
    METHOD(ION_Stream_StorageAbstract, timeout,          ZEND_ACC_PUBLIC)
    METHOD(ION_Stream_StorageAbstract, close,            ZEND_ACC_PUBLIC)
    METHOD(ION_Stream_StorageAbstract, ping,             ZEND_ACC_PUBLIC)

    METHOD(ION_Stream_StorageAbstract, hasStream,        ZEND_ACC_PUBLIC)
    METHOD(ION_Stream_StorageAbstract, getStats,         ZEND_ACC_PUBLIC)
CLASS_METHODS_END;


PHP_MINIT_FUNCTION(ION_Stream_StorageAbstract) {
    pion_register_class(ION_Stream_StorageAbstract, "ION\\Stream\\StorageAbstract", ion_storage_init, CLASS_METHODS(ION_Stream_StorageAbstract));
    ion_ce_ION_Stream_StorageAbstract->ce_flags |= ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;
    pion_init_std_object_handlers(ION_Stream_StorageAbstract);
    pion_set_object_handler(ION_Stream_StorageAbstract, free_obj, ion_storage_free);
    pion_set_object_handler(ION_Stream_StorageAbstract, clone_obj, NULL);

    PION_REGISTER_VOID_EXTENDED_CLASS(ION_Net_Socket_StorageException, ion_ce_ION_RuntimeException, "ION\\Stream\\StorageException");

    return SUCCESS;
}
