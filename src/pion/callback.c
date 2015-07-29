#include "callback.h"
#include "zend.h"
#include "exceptions.h"


/**
 * Create pionCb callback from FCI
 **/
pionCb * pionCbCreate(zend_fcall_info *fci_ptr, zend_fcall_info_cache *fcc_ptr TSRMLS_DC) {
    zval* retval_ptr;
    pionCb *cb = safe_emalloc(1, sizeof(pionCb), 0);
    cb->fci = safe_emalloc(1, sizeof(zend_fcall_info), 0);
    cb->fcc = safe_emalloc(1, sizeof(zend_fcall_info_cache), 0);

    memcpy(cb->fci, fci_ptr, sizeof(zend_fcall_info));
    memcpy(cb->fcc, fcc_ptr, sizeof(zend_fcall_info_cache));

    if (ZEND_FCI_INITIALIZED(*fci_ptr)) {
        Z_ADDREF_P(cb->fci->function_name);
        if (fci_ptr->object_ptr) {
            Z_ADDREF_P(fci_ptr->object_ptr);
        }
    }

    cb->fci->param_count = 0;
    cb->fci->no_separation = 0;
    cb->fci->retval_ptr_ptr = &retval_ptr;

    TSRMLS_SET_CTX(cb->thread_ctx);

    return cb;
}

/**
 * Create pionCb callback from variable zval
 * @param zval* zCb
 * @return pionCb*
 */
pionCb * pionCbCreateFromZval(zval* zCb TSRMLS_DC) {
    return NULL;
}

/**
 * Destroy pionCb callback
 * */
void pionCbFree(pionCb *cb) {
    efree(cb->fcc);
    if (ZEND_FCI_INITIALIZED(*cb->fci)) {
        zval_ptr_dtor(&cb->fci->function_name);
        if (cb->fci->object_ptr) {
            zval_ptr_dtor(&cb->fci->object_ptr);
        }
    }
    efree(cb->fci);
    efree(cb);
    cb = NULL;
}



/**
 * Invoke pionCb and destroy result
 * @param pionCb* cb
 * @param int num count of the arguments
 * @param zval*** args array of the arguments
 * @return
 */
int pionCbVoidCall(pionCb *cb, int num, zval ***args TSRMLS_DC) {
    zval *pretval = NULL;


    if (ZEND_FCI_INITIALIZED(*cb->fci)) {
        cb->fci->retval_ptr_ptr = &pretval;
        cb->fci->params = args;
        cb->fci->param_count = (zend_uint)num;
//        zend_try {
        zend_call_function(cb->fci, cb->fcc TSRMLS_CC);
//        } zend_catch {
//        } zend_end_try();
        if(EG(exception)) {
//            PHPDBG("catch exception");
//            event_base_loopbreak(ION(base));
        }
        if(pretval) {
//            ion_result_dispatch(pretval TSRMLS_CC);
        }
        return SUCCESS;
    } else {
//        PHPDBG("FCI not inited");
        return FAILURE;
        //ThrowRuntimeError("Callback corrupted");
    }
}

int pionCbVoidWithoutArgs(pionCb * cb) {
    return pionCbVoidCall(cb, 0, NULL);
}

int pionCbVoidWith1Arg(pionCb * cb, zval* arg1) {
    zval **args[1];
    args[0] = &arg1;
    return pionCbVoidCall(cb, 1, args);
}

int pionCbVoidWith2Args(pionCb *cb, zval *arg1, zval *arg2) {
    zval **args[2];
    args[0] = &arg1;
    args[1] = &arg2;
    return pionCbVoidCall(cb, 2, args);
}

int pionCbVoidWith3Args(pionCb *cb, zval *arg1, zval *arg2, zval *arg3) {
    zval **args[3];
    args[0] = &arg1;
    args[1] = &arg2;
    args[2] = &arg3;
    return pionCbVoidCall(cb, 3, args);
}

int pionCbVoidWith4Args(pionCb *cb, zval *arg1, zval *arg2, zval *arg3, zval *arg4) {
    zval **args[4];
    args[0] = &arg1;
    args[1] = &arg2;
    args[2] = &arg3;
    args[3] = &arg4;
    return pionCbVoidCall(cb, 4, args);
}

/**
 * Invoke class constructor
 * @param zend_class_entry* cls class entry
 * @param zval* this_ptr $this
 * @param int args_num
 * @param zval*** args
 * @return
 */
int pionCallConstructor(zend_class_entry *cls, zval *this_ptr, int args_num, zval ***args) {
    zval *retval_ptr = NULL;
    zend_fcall_info fci;
    zend_fcall_info_cache fcc;

    if (!(cls->constructor->common.fn_flags & ZEND_ACC_PUBLIC)) {
        zend_throw_exception_ex(spl_ce_RuntimeException, 1, "Invocation of %s's constructor failed",  cls->name);
        return FAILURE;
    }
    fci.size = sizeof(fci);
    fci.function_table = EG(function_table);
    fci.function_name = NULL;
    fci.symbol_table = NULL;
    fci.object_ptr = this_ptr;
    fci.retval_ptr_ptr = &retval_ptr;
    fci.param_count = (zend_uint)args_num;
    fci.params = args;
    fci.no_separation = 1;

    fcc.initialized = 1;
    fcc.function_handler = cls->constructor;
    fcc.calling_scope = EG(scope);
    fcc.called_scope = Z_OBJCE_P(this_ptr);
    fcc.object_ptr = this_ptr;

    if (zend_call_function(&fci, &fcc TSRMLS_CC) == FAILURE) {
        if (retval_ptr) {
            zval_ptr_dtor(&retval_ptr);
        }
        zend_throw_exception_ex(spl_ce_RuntimeException, 1, "Invocation of %s's constructor failed",  cls->name);
        return FAILURE;
    }
    if (retval_ptr) {
        zval_ptr_dtor(&retval_ptr);
    }

    return SUCCESS;
}

int pionCallConstructorWith1Arg(zend_class_entry *cls, zval *this_ptr, zval *arg1) {
    zval **args[1];
    args[0] = &arg1;
    return pionCallConstructor(cls, this_ptr, 1, args);
}

int pionCallConstructorWith2Args(zend_class_entry *cls, zval *this_ptr, zval *arg1, zval *arg2) {
    zval **args[2];
    args[0] = &arg1;
    args[1] = &arg2;
    return pionCallConstructor(cls, this_ptr, 2, args);
}

int pionCallConstructorWith3Args(zend_class_entry *cls, zval *this_ptr, zval *arg1, zval *arg2, zval *arg3) {
    zval **args[3];
    args[0] = &arg1;
    args[1] = &arg2;
    args[2] = &arg3;
    return pionCallConstructor(cls, this_ptr, 3, args);
}

zval* pionNewObject(zend_class_entry *ce, int args_num, zval ***args) {
    zval *object = NULL;
    ALLOC_INIT_ZVAL(object);
    object_init_ex(object, ce);
    if(ce->constructor) {
        if(pionCallConstructor(ce, object, args_num, args) == FAILURE) {
            zval_ptr_dtor(&object);
            return NULL;
        }
    }

    return object;
}

zval* pionNewObjectWithoutArgs(zend_class_entry *ce) {
    return pionNewObject(ce, 0, NULL);
}

zval* pionNewObjectWith1Arg(zend_class_entry *ce, zval *arg1) {
    zval **args[1];
    args[0] = &arg1;
    return pionNewObject(ce, 1, args);
}

zval* pionNewObjectWith2Args(zend_class_entry *ce, zval *arg1, zval *arg2) {
    zval **args[2];
    args[0] = &arg1;
    args[1] = &arg2;
    return pionNewObject(ce, 2, args);
}

zval* pionNewObjectWith3Args(zend_class_entry *ce, zval *arg1, zval *arg2, zval *arg3) {
    zval **args[3];
    args[0] = &arg1;
    args[1] = &arg2;
    args[2] = &arg3;
    return pionNewObject(ce, 3, args);
}

zval* pionInitException(zend_class_entry *cls, char *message, int code) {
    zval *msg, *c, *exception;
    ALLOC_STRING_ZVAL(msg, message, 1);
    ALLOC_LONG_ZVAL(c, code);
    exception = pionNewObjectWith2Args(cls, msg, c);
    zval_ptr_dtor(&msg);
    zval_ptr_dtor(&c);
    return exception;
}

zval*pionCallFunction(const char *function_name, int num_args, zval **args) {
    zval *zfunc, *retval = NULL;
    ALLOC_INIT_ZVAL(retval);
    ALLOC_INIT_ZVAL(zfunc);
    ZVAL_STRING(zfunc, function_name, 1);
    if(call_user_function(EG(function_table), NULL, zfunc, retval, num_args, args) == FAILURE) {
        zval_ptr_dtor(&zfunc);
        zval_ptr_dtor(&retval);
        return NULL;
    } else {
        zval_ptr_dtor(&zfunc);
        return retval;
    }
}

zval*pionCallFunctionWith1Arg(const char *function_name, zval *arg1) {
    zval *args[1];
    args[0] = arg1;
    return pionCallFunction(function_name, 1, args);
}

zval*pionCallFunctionWith2Args(const char *function_name, zval *arg1, zval *arg2) {
    zval *args[2];
    args[0] = arg1;
    args[1] = arg2;
    return pionCallFunction(function_name, 2, args);
}

zval*pionCallFunctionWith3Args(const char *function_name, zval *arg1, zval *arg2, zval *arg3) {
    zval *args[3];
    args[0] = arg1;
    args[1] = arg2;
    args[2] = arg3;
    return pionCallFunction(function_name, 3, args);
}