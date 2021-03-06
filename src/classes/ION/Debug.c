#include "ion.h"

pion_cb * global_cb;
zend_class_entry * ion_ce_ION_Debug;

CLASS_METHOD(ION_Debug, fcallVoid) {

    zval * arg1 = NULL;
    zval * arg2 = NULL;
    zval * arg3 = NULL;
    int r = 0;
    zend_fcall_info        fci = empty_fcall_info;
    zend_fcall_info_cache  fcc = empty_fcall_info_cache;
//#ifndef FAST_ZPP
//    PARSE_ARGS("f|zzz", &fci, &fcc, &arg1, &arg2, &arg3);
//#else
    ZEND_PARSE_PARAMETERS_START(1, 4)
        Z_PARAM_FUNC(fci, fcc)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(arg1)
        Z_PARAM_ZVAL(arg2)
        Z_PARAM_ZVAL(arg3)
    ZEND_PARSE_PARAMETERS_END();
//#endif
    if(ZEND_NUM_ARGS() == 1) {
        r = pion_fcall_void_no_args(&fci, &fcc);
    } else if (ZEND_NUM_ARGS() == 2) {
        r = pion_fcall_void_1_arg(&fci, &fcc, arg1);
    } else if (ZEND_NUM_ARGS() == 3) {
        r = pion_fcall_void_2_args(&fci, &fcc, arg1, arg2);
    } else if (ZEND_NUM_ARGS() == 4) {
        r = pion_fcall_void_3_args(&fci, &fcc, arg1, arg2, arg3);
    }
    RETURN_LONG(r);
}

METHOD_ARGS_BEGIN_RETURN_INT(ION_Debug, fcallVoid, 1)
    ARGUMENT(callback, IS_CALLABLE)
    ARGUMENT(arg1, IS_MIXED)
    ARGUMENT(arg2, IS_MIXED)
    ARGUMENT(arg3, IS_MIXED)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, cbCallVoid) {
    zval * arg1 = NULL;
    zval * arg2 = NULL;
    zval * arg3 = NULL;
    pion_cb * cb = NULL;
    int r = 0;
    zend_fcall_info        fci = empty_fcall_info;
    zend_fcall_info_cache  fcc = empty_fcall_info_cache;
//#ifndef FAST_ZPP
//    PARSE_ARGS("f|zzz", &fci, &fcc, &arg1, &arg2, &arg3);

//#else
    ZEND_PARSE_PARAMETERS_START(1, 4)
        Z_PARAM_FUNC(fci, fcc)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(arg1)
        Z_PARAM_ZVAL(arg2)
        Z_PARAM_ZVAL(arg3)
    ZEND_PARSE_PARAMETERS_END();
//#endif
    cb = pion_cb_create(&fci, &fcc);

    if(ZEND_NUM_ARGS() == 1) {
        r = pion_cb_void_without_args(cb);
    } else if (ZEND_NUM_ARGS() == 2) {
        r = pion_cb_void_with_1_arg(cb, arg1);
    } else if (ZEND_NUM_ARGS() == 3) {
        r = pion_cb_void_with_2_args(cb, arg1, arg2);
    } else if (ZEND_NUM_ARGS() == 4) {
        r = pion_cb_void_with_3_args(cb, arg1, arg2, arg3);
    }

    pion_cb_free(cb);
    RETURN_LONG(r);
}

METHOD_ARGS_BEGIN_RETURN_INT(ION_Debug, cbCallVoid, 1)
    ARGUMENT(callback, IS_CALLABLE)
    ARGUMENT(arg1, IS_MIXED)
    ARGUMENT(arg2, IS_MIXED)
    ARGUMENT(arg3, IS_MIXED)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, globalCbCall) {
    zval *zarg = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(zarg)
    ZEND_PARSE_PARAMETERS_END();
    zval result = pion_cb_call_with_1_arg(global_cb, zarg);
    pion_cb_free(global_cb);
    if(Z_ISUNDEF(result)) {
        RETURN_FALSE;
    }
    global_cb = NULL;
    RETURN_ZVAL(&result, 0, 1);
}

METHOD_ARGS_BEGIN(ION_Debug, globalCbCall, 1)
    ARGUMENT(arg, IS_MIXED)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, globalCbObjCall) {
    zval * obj = NULL;
    zval * zarg = NULL;
//    PARSE_ARGS("zz", &obj, &zarg);
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_ZVAL(obj)
        Z_PARAM_ZVAL(zarg)
    ZEND_PARSE_PARAMETERS_END();
    zval result = pion_cb_obj_call_with_1_arg(global_cb, Z_OBJ_P(obj), zarg);
    pion_cb_free(global_cb);
    global_cb = NULL;
    if(Z_ISUNDEF(result)) {
        RETURN_FALSE;
    }
    RETURN_ZVAL(&result, 0, 1);
}

METHOD_ARGS_BEGIN(ION_Debug, globalCbObjCall, 2)
    ARGUMENT(obj, IS_MIXED)
    ARGUMENT(arg, IS_MIXED)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, globalCbCallVoid) {
    zval *arg = NULL;
//    PARSE_ARGS("z", &arg);
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(arg)
    ZEND_PARSE_PARAMETERS_END();

    int result = pion_cb_void_with_1_arg(global_cb, arg);
    pion_cb_free(global_cb);
    global_cb = NULL;
    RETURN_LONG((long)result);
}

METHOD_ARGS_BEGIN_RETURN_INT(ION_Debug, globalCbCallVoid, 1)
    ARGUMENT(arg, IS_MIXED)
METHOD_ARGS_END();


CLASS_METHOD(ION_Debug, globalCbCreate) {
    zend_fcall_info        fci = empty_fcall_info;
    zend_fcall_info_cache  fcc = empty_fcall_info_cache;
//    PARSE_ARGS("f", &fci, &fcc);
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_FUNC(fci, fcc)
    ZEND_PARSE_PARAMETERS_END();
    global_cb = pion_cb_create(&fci, &fcc);
}

METHOD_ARGS_BEGIN(ION_Debug, globalCbCreate, 1)
    ARGUMENT(callback, IS_CALLABLE)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, globalCbCreateFromZval) {
    zval *zcb = NULL;
//    PARSE_ARGS("z", &zcb);
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(zcb)
    ZEND_PARSE_PARAMETERS_END();
    global_cb = pion_cb_create_from_zval(zcb);
}

METHOD_ARGS_BEGIN(ION_Debug, globalCbCreateFromZval, 1)
    ARGUMENT(callback, IS_MIXED)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, globalCbFetchMethod) {
    zend_string * class_name = NULL;
    zend_string * method_name = NULL;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STR(class_name)
        Z_PARAM_STR(method_name)
    ZEND_PARSE_PARAMETERS_END();
    global_cb = pion_cb_fetch_method(class_name->val, method_name->val);
}

METHOD_ARGS_BEGIN(ION_Debug, globalCbFetchMethod, 2)
    ARGUMENT(class_name, IS_STRING)
    ARGUMENT(method_name, IS_STRING)
METHOD_ARGS_END();


CLASS_METHOD(ION_Debug, noHint) {
    zval * val = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 1)
            Z_PARAM_ZVAL(val);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, noHint, 1)
                ARGUMENT(val, IS_MIXED)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, intHint) {
    zend_long lval = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(lval);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, intHint, 1)
                ARGUMENT(val, IS_LONG)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, doubleHint) {
    double val = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_DOUBLE(val);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, doubleHint, 1)
                ARGUMENT(val, IS_DOUBLE)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, boolHint) {
    zend_bool val = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_BOOL(val);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, boolHint, 1)
                ARGUMENT(val, IS_BOOLEAN)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, stringHint) {
    zend_string * val = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(val);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, stringHint, 1)
                ARGUMENT(val, IS_STRING)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, arrayHint) {
    zval * val = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(val);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, arrayHint, 1)
                ARGUMENT(val, IS_ARRAY)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, ArrayObjectHint) {
    zval * val = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 1)
            Z_PARAM_ARRAY(val);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, ArrayObjectHint, 1)
                ARGUMENT_OBJECT(val, ArrayObject, 0)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, SplDoublyLinkedListHint) {
    zval * val = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT(val);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, SplDoublyLinkedListHint, 1)
                ARGUMENT_OBJECT(val, SplDoublyLinkedList, 0)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, ArrayAccessHint) {
    zval * val = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 1)
            Z_PARAM_OBJECT(val);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, ArrayAccessHint, 1)
                ARGUMENT_OBJECT(val, ArrayAccess, 0)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, SplQueueHint) {
    zval * val = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 1)
            Z_PARAM_OBJECT(val);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, SplQueueHint, 1)
    ARGUMENT_OBJECT(val, SplQueue, 0)
METHOD_ARGS_END();

CLASS_METHOD(ION_Debug, CallableHint) {
    zval * val = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(val);
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

METHOD_ARGS_BEGIN(ION_Debug, CallableHint, 1)
                ARGUMENT(val, IS_CALLABLE)
METHOD_ARGS_END();


CLASS_METHOD(ION_Debug, sandbox) {

}

METHOD_WITHOUT_ARGS(ION_Debug, sandbox)

METHODS_START(methods_ION_Debug)
    METHOD(ION_Debug, fcallVoid,              ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, cbCallVoid,             ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, globalCbCall,           ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, globalCbObjCall,        ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, globalCbCallVoid,       ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, globalCbCreate,         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, globalCbCreateFromZval, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, globalCbFetchMethod,    ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, sandbox,                ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)

    METHOD(ION_Debug, noHint,                 ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, intHint,                ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, doubleHint,             ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, boolHint,               ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, stringHint,             ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, arrayHint,              ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, ArrayObjectHint,        ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, SplDoublyLinkedListHint,ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, ArrayAccessHint,        ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, SplQueueHint,           ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    METHOD(ION_Debug, CallableHint,           ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
METHODS_END;

PHP_MINIT_FUNCTION(ION_Debug) {

    ion_register_static_class(ion_ce_ION_Debug, "ION\\Debug", methods_ION_Debug);
    return SUCCESS;
}
