#include <php.h>
#include <event.h>

#include "ION.h"

//#define event struct event

typedef struct event ev;

DEFINE_CLASS(ION);
IONBase *ionBase;
void ion_reinit(long flags) {
    IONF("Reinit event loop: %d. Cleanup exec events...", (int)flags);
    //zend_hash_clean(ION(execs));

    if(!(flags & PRESERVE_TIMERS)) {
        IONF("Cleanup timer events...");
        zend_hash_clean(ION(timers));
    }

    if(!(flags & PRESERVE_SIGNALS)) {
        IONF("Cleanup signal events...");
        zend_hash_clean(ION(signals));
    }

    if(event_reinit(ION(base)) == FAILURE) {
        php_error(E_NOTICE, "Some events could not be re-added");
    }
}

/** public function ION::reinit(int $flags = 0) : self */
CLASS_METHOD(ION, reinit) {
    long flags = 0;
    PARSE_ARGS("|l", &flags);

    ion_reinit(flags);
}
METHOD_ARGS_BEGIN(ION, reinit, 1)
    METHOD_ARG_TYPE(flags, IS_LONG, 0, 0)
METHOD_ARGS_END()

/** public function ION::dispatch(int $flags = 0) : self */
CLASS_METHOD(ION, dispatch) {
    long flags = 0;
    int ret;

    PARSE_ARGS("|ld", &flags);

    ret = event_base_loop(ION(base), (int)flags);

    if(ret == -1) {
        ThrowRuntime("Dispatching runtime error", 1);
    }

    if(ret) {
        RETURN_FALSE;
    } else {
        RETURN_TRUE;
    }
}

METHOD_ARGS_BEGIN(ION, dispatch, 1)
    METHOD_ARG_TYPE(flags, IS_LONG, 0, 0)
METHOD_ARGS_END()

/** public function ION::stop(double $timeout = -1) : self */
CLASS_METHOD(ION, stop) {
    double timeout = 0.0;
    struct timeval time;
    PARSE_ARGS("|d", &timeout);

    if(timeout > 0) {
        time.tv_usec = (int)((int)(timeout*1000000) % 1000000);
        time.tv_sec = (int)timeout;
        event_base_loopexit(ION(base), &time);
    } else {
        event_base_loopbreak(ION(base));
    }
}

METHOD_ARGS_BEGIN(ION, stop, 0)
    METHOD_ARG(timeout, 0)
METHOD_ARGS_END()

static void _timer_done(evutil_socket_t fd, short flags, void * arg) {
    zval * zdeferred = (zval * )arg;
    zval * zresult = NULL;
    TSRMLS_FETCH();
    MAKE_STD_ZVAL(zresult);
    deferredResolve(zdeferred, zresult);
    zval_ptr_dtor(&zresult);
//    zval_ptr_dtor(&zdeferred);
}

static void _timer_dtor(void * object, zval * zdeferred TSRMLS_DC) {
    ev * timer = (ev *) object;
    event_del(timer);
    event_free(timer);
    zval_ptr_dtor(&zdeferred);
}

/** public function ION::await(double $time) : ION\Deferred */
CLASS_METHOD(ION, await) {
    zval *zDeferred = NULL;
    double timeout = 0.0;
    struct timeval tv = { 0, 0 };
    PARSE_ARGS("d", &timeout);
    if(timeout < 0) {
        ThrowRuntime("timeout sould be unsigned", 1);
        return;
    }
    tv.tv_usec = (int)((int)(timeout*1000000) % 1000000);
    tv.tv_sec = (int)timeout;
    zDeferred = deferredNewInternal(NULL);
    ev * timer = event_new(ION(base), -1, EV_TIMEOUT, _timer_done, zDeferred);
    if(event_add(timer, &tv) == FAILURE) {
        event_del(timer);
        event_free(timer);
        deferredFree(zDeferred);
    } else {
        deferredStore(zDeferred, timer, _timer_dtor);
        RETURN_ZVAL(zDeferred, 1, 0);
    }

}

METHOD_ARGS_BEGIN(ION, await, 1)
    METHOD_ARG(time, 0)
METHOD_ARGS_END()

CLASS_METHODS_START(ION)
    METHOD(ION, reinit,   ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION, dispatch, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION, stop,     ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION, await,    ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
//    METHOD(ION_Deferred, then, ZEND_ACC_PUBLIC)
//    METHOD(ION_Deferred, reject, ZEND_ACC_PUBLIC)
//    METHOD(ION_Deferred, resolve, ZEND_ACC_PUBLIC)
//    METHOD(ION_Deferred, error, ZEND_ACC_PUBLIC)
//    METHOD(ION_Deferred, timeout, ZEND_ACC_PUBLIC)
//    METHOD(ION_Deferred, getFlags, ZEND_ACC_PUBLIC)
//    METHOD(ION_Deferred, __destruct, ZEND_ACC_PUBLIC)
CLASS_METHODS_END;

PHP_MINIT_FUNCTION(ION) {
    PION_REGISTER_PLAIN_CLASS(ION, "ION");
    PION_CLASS_CONST_STRING(ION, "VERSION", ION_VERSION);
    PION_CLASS_CONST_LONG(ION, "LIBEVENT_VERSION_NUMBER",LIBEVENT_VERSION_NUMBER);
    PION_CLASS_CONST_LONG(ION, "ONCE",            EVLOOP_ONCE);
    PION_CLASS_CONST_LONG(ION, "NONBLOCK",        EVLOOP_NONBLOCK);
//    PION_CLASS_CONST_LONG(ION_Deferred, "FAILED",    DEFERRED_FAILED);
//    PION_CLASS_CONST_LONG(ION_Deferred, "FINISHED",  DEFERRED_FINISHED);
//    PION_CLASS_CONST_LONG(ION_Deferred, "INTERNAL",  DEFERRED_INTERNAL);
//    PION_CLASS_CONST_LONG(ION_Deferred, "TIMED_OUT", DEFERRED_TIMED_OUT);
//    PION_CLASS_CONST_LONG(ION_Deferred, "REJECTED",  DEFERRED_REJECTED);
//
//    REGISTER_VOID_EXTENDED_CLASS(ION_Deferred_RejectException, Exception, "ION\\Deferred\\RejectException", NULL);
//    REGISTER_VOID_EXTENDED_CLASS(ION_Deferred_TimeoutException, ION_Deferred_RejectException, "ION\\Deferred\\TimeoutException", NULL);
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(ION) {
    return SUCCESS;
}

PHP_RINIT_FUNCTION(ION) {

    return SUCCESS;
}