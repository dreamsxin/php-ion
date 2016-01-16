#ifndef PION_ERRORS_H
#define PION_ERRORS_H

// class ION
#define ERR_ION_DISPATCH_IN_LOOP    "Dispatching in progress"
#define ERR_ION_DISPATCH_FAILED     "Dispatching runtime error"
#define ERR_ION_AWAIT_INVALID_TIME  "Timeout should be unsigned"
#define ERR_ION_AWAIT_FAILED        "Unable to add event to queue"

// class ION\HTTP\Request
#define ERR_ION_HTTP_REQUEST_FACTORY_URI    "URI should be instance of ION\\URI"
#define ERR_ION_HTTP_REQUEST_FACTORY_METHOD "Method should be a string"
#define ERR_ION_HTTP_REQUEST_FACTORY_HEADERS "Headers should be an array"
#define ERR_ION_HTTP_REQUEST_FACTORY_UNKNOWN "Unknown option %d"


#endif //PION_ERRORS_H
