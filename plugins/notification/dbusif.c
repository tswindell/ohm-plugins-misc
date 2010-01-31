/*! \defgroup pubif Public Interfaces */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <dbus/dbus.h>

#include "plugin.h"
#include "dbusif.h"
#include "proxy.h"


typedef struct {
    char       *member;
    uint32_t  (*function)(DBusMessage *, char *, char *);
} handler_t;

static DBusConnection   *conn;      /* connection to D-Bus session bus */
static int               timeout;   /* message timeoutin msec */
static int               backend = TRUE; /* FIXME: remove the initialization */

static void get_parameters(OhmPlugin *);
static void session_bus_init(const char *);

static void reply_with_error(DBusMessage *, const char *, const char *);
static void reply_with_id(DBusMessage *, uint32_t);

static int copy_string(DBusMessageIter *, DBusMessageIter *);
static int append_string(DBusMessageIter *, char *);
static int copy_dict_entry(DBusMessageIter *, DBusMessageIter *);
static int copy_variant(DBusMessageIter *, DBusMessageIter *);
static int append_variant(DBusMessageIter *, int, void *);
static int append_dict_entry(DBusMessageIter *, DBusMessageIter *);
static int close_dict_entry(DBusMessageIter *, DBusMessageIter *);
static int copy_array(DBusMessageIter *, DBusMessageIter *, DBusMessageIter *);
static int extend_array(DBusMessageIter *, va_list);
static int close_array(DBusMessageIter *, DBusMessageIter *);



static DBusHandlerResult name_changed(DBusConnection *, DBusMessage *, void *);

static DBusHandlerResult ngf_method(DBusConnection *, DBusMessage *, void *);
static uint32_t play_handler(DBusMessage *, char *, char *);
static uint32_t stop_handler(DBusMessage *, char *, char *);


/*! \addtogroup pubif
 *  Functions
 *  @{
 */

void dbusif_init(OhmPlugin *plugin)
{
    get_parameters(plugin);
}


DBusHandlerResult dbusif_session_notification(DBusConnection *syscon,
                                              DBusMessage    *msg,
                                              void           *ud)
{
    char      *address;
    DBusError  error;
    int        success;

    (void)syscon;               /* supposed to be sys_conn */
    (void)ud;                   /* not used */

    do { /* not a loop */
        dbus_error_init(&error);
    
        success = dbus_message_get_args(msg, &error,
                                        DBUS_TYPE_STRING, &address,
                                        DBUS_TYPE_INVALID);

        if (!success) {
            if (!dbus_error_is_set(&error)) {
                OHM_ERROR("notification: failed to parse session bus "
                          "notification.");
            }
            else {
                OHM_ERROR("notification: failed to parse session bus "
                          "notification: %s.", error.message);
                dbus_error_free(&error);
            }
            break;
        }
                         
        if (!strcmp(address, "<failure>")) {
            OHM_INFO("notification: got session bus failure notification, "
                     "exiting");
            ohm_restart(10);
        }

        if (conn != NULL) {
            OHM_ERROR("notification: got session bus notification but "
                      "already has a bus.");
            OHM_ERROR("notification: ignoring session bus notification.");
            break;
        }

        OHM_INFO("notification: got session bus notification with address "
                 "'%s'", address);

        session_bus_init(address);

    } while(0);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void *dbusif_append_to_data(void *data, ...)
{
    DBusMessage    *src = data;
    DBusMessage    *dst = NULL;
    int             success = FALSE;
    DBusMessageIter sit;
    DBusMessageIter dit;
    DBusMessageIter darr;
    va_list         ap;

#if 0
    dst = dbus_message_new_method_call(DBUS_NGF_BACKEND_SERVICE, DBUS_NGF_PATH,
                                       DBUS_NGF_INTERFACE, DBUS_PLAY_METHOD);
#else
    dst = dbus_message_new_signal(DBUS_NGF_PATH, DBUS_NGF_INTERFACE,
                                  DBUS_PLAY_METHOD);
#endif

    if (src != NULL && dst != NULL) {

        va_start(ap, data);

        dbus_message_iter_init(src, &sit);
        dbus_message_iter_init_append(dst, &dit);

        if (copy_string(&sit, &dit)       &&
            copy_array(&sit, &dit, &darr) &&
            extend_array(&darr, ap)       &&
            close_array(&dit, &darr)        )
        {
            success = TRUE;
        }

        va_end(ap);
    }

    if (success)
        OHM_DEBUG(DBG_DBUS, "append to data succeeded");
    else {
        OHM_DEBUG(DBG_DBUS, "append to data failed");

        dbus_message_unref(dst);
        dst = NULL;
    }

    return (void *)dst;
}

void dbusif_forward_data(void *data)
{
    DBusMessage *msg = data;

    if (msg != NULL) {
        OHM_DEBUG(DBG_DBUS, "forwarding message");

        dbus_connection_send(conn, msg, NULL);
        dbus_message_unref(msg);
    }
}

void dbusif_free_data(void *data)
{
    DBusMessage *msg = data;

    if (msg != NULL)
        dbus_message_unref(msg);
}


/*!
 * @}
 */

static void get_parameters(OhmPlugin *plugin)
{
    const char *timeout_str;
    char       *e;
    
    if ((timeout_str = ohm_plugin_get_param(plugin, "dbus-timeout")) == NULL)
        timeout = -1;           /* 'a sane default timeout' will be used */
    else {
        timeout = strtol(timeout_str, &e, 10);
        
        if (*e != '\0') {
            OHM_ERROR("notification: Invalid value '%s' for 'dbus-timeout'",
                      timeout_str);
            timeout = -1;
        }
        
        if (timeout < 0)
            timeout = -1;
    }

    OHM_INFO("notification: D-Bus message timeout is %dmsec", timeout);
}


static void session_bus_init(const char *addr)
{
    static char *filter =
        "type='signal',"
        "sender='"    DBUS_ADMIN_SERVICE             "',"
        "interface='" DBUS_ADMIN_INTERFACE           "',"
        "member='"    DBUS_NAME_OWNER_CHANGED_SIGNAL "',"
        "path='"      DBUS_ADMIN_PATH                "',"
        "arg0='"      DBUS_NGF_BACKEND_SERVICE       "'";

    static struct DBusObjectPathVTable method = {
        .message_function = ngf_method
    };

    DBusError err;
    int       retval;
    int       success;

    dbus_error_init(&err);

    if (!addr) {
        if ((conn = dbus_bus_get(DBUS_BUS_SESSION, &err)) != NULL)
            success = TRUE;
        else {
            success = FALSE;

            if (!dbus_error_is_set(&err))
                OHM_ERROR("notification: can't get D-Bus connection");
            else {
                OHM_ERROR("notification: can't get D-Bus connection: %s",
                          err.message);
                dbus_error_free(&err);
            }
        }
    }
    else {
        if ((conn = dbus_connection_open(addr, &err)) != NULL &&
            dbus_bus_register(conn, &err)                        )
            success = TRUE;
        else {
            success = FALSE;
            conn = NULL;

            if (!dbus_error_is_set(&err))
                OHM_ERROR("notification: can't connect to D-Bus %s", addr);
            else {
                OHM_ERROR("notification: can't connect to D-Bus %s (%s)",
                          addr, err.message);
                dbus_error_free(&err);
            }
        }
    }

    if (!success) {
        OHM_ERROR("delayed connection to D-Bus session bus failed");
        return;
    }

    /*
     * signal filtering to track the backand
     */
    if (!dbus_connection_add_filter(conn, name_changed,NULL, NULL)) {
        OHM_ERROR("Can't add filter 'name_changed'");
        exit(1);
    }

    dbus_bus_add_match(conn, filter, &err);

    if (dbus_error_is_set(&err)) {
        OHM_ERROR("notification: can't watch backend '%s': %s",
                  DBUS_NGF_BACKEND_SERVICE, err.message);
        dbus_error_free(&err);
        exit(1);
    }

    /*
     * proxy methods
     */
    if(!dbus_connection_register_object_path(conn,DBUS_NGF_PATH,&method,NULL)){
        OHM_ERROR("notification: can't register object path '%s'",
                  DBUS_NGF_PATH);
        exit(1);
    }

    retval = dbus_bus_request_name(conn, DBUS_NGF_PROXY_SERVICE,
                                   DBUS_NAME_FLAG_REPLACE_EXISTING, &err);

    if (retval != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (dbus_error_is_set(&err)) {
            OHM_ERROR("notification: can't be the primary owner for name %s: "
                      "%s", DBUS_NGF_PROXY_SERVICE, err.message);
            dbus_error_free(&err);
        }
        else {
            OHM_ERROR("notification: can't be the primary owner for name %s",
                      DBUS_NGF_PROXY_SERVICE);
        }
        exit(1);
    }
    
    OHM_INFO("notification: got name '%s' on session D-BUS",
             DBUS_NGF_PROXY_SERVICE);

    OHM_INFO("notification: successfully connected to D-Bus session bus");
}


static void reply_with_error(DBusMessage *msg,const char *err,const char *desc)
{
    static uint32_t  id = 0;

    DBusMessage     *reply;
    dbus_uint32_t    serial;
    int              success;

    if (err == NULL || err[0] == '\0')
        err = DBUS_NGF_ERROR_INTERNAL;

    if (desc == NULL || desc[0] == '\0')
        desc = "general error";

    serial  = dbus_message_get_serial(msg);
    reply   = dbus_message_new_error(msg, err, desc);
    success = dbus_message_append_args(reply,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_INVALID);
                                       
    if (!success)
        OHM_ERROR("notification: failed to build D-Bus error message");
    else {
        OHM_DEBUG(DBG_DBUS, "replying to %s request with error '%s'",
                  dbus_message_get_member(msg), desc);

        dbus_connection_send(conn, reply, &serial);
    }

    dbus_message_unref(reply);
}

static void reply_with_id(DBusMessage *msg, uint32_t id)
{
    dbus_uint32_t   serial;
    const char     *member;
    DBusMessage    *reply;
    int             success;

    serial  = dbus_message_get_serial(msg);
    member  = dbus_message_get_member(msg);
    reply   = dbus_message_new_method_return(msg);
    success = dbus_message_append_args(reply,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_INVALID);

    if (!success)
        OHM_ERROR("notification: failed to build D-Bus reply message");
    else {
        OHM_DEBUG(DBG_DBUS, "replying to %s request with id %u", member, id);

        dbus_connection_send(conn, reply, &serial);
    }

    dbus_message_unref(reply); 
}

static int copy_string(DBusMessageIter *sit, DBusMessageIter *dit)
{
    char *string;

    if (dbus_message_iter_get_arg_type(sit) != DBUS_TYPE_STRING)
        return FALSE;
    
    dbus_message_iter_get_basic(sit, &string);
    append_string(dit, string);

    return dbus_message_iter_next(sit);
}

static int append_string(DBusMessageIter *it, char *string)
{
    dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &string);

    return TRUE;
}

static int copy_dict_entry(DBusMessageIter *sdict, DBusMessageIter *ddict)
{
    int success = FALSE;

    if (copy_string(sdict, ddict) &&
        copy_variant(sdict, ddict)  )
        success = TRUE;

    return success;
}


static int copy_variant(DBusMessageIter *sit, DBusMessageIter *dit)
{
    DBusMessageIter  var;
    int              type;
    char             value[16];

    if (dbus_message_iter_get_arg_type(sit) != DBUS_TYPE_VARIANT)
        return FALSE;

    dbus_message_iter_recurse(sit, &var);

    type = dbus_message_iter_get_arg_type(&var);
    dbus_message_iter_get_basic(&var, (void *)value);

    append_variant(dit, type, (void *)value);

    dbus_message_iter_next(sit);

    return TRUE;
}

static int append_variant(DBusMessageIter *it, int type, void *value)
{
    DBusMessageIter var;
    char signiture[2];

    signiture[0] = (char)type;
    signiture[1] = '\0';

    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, signiture, &var);
    dbus_message_iter_append_basic(&var, type, value);
    dbus_message_iter_close_container(it, &var);

    return TRUE;
}


static int append_dict_entry(DBusMessageIter *it, DBusMessageIter *dict)
{
    dbus_message_iter_open_container(it, DBUS_TYPE_DICT_ENTRY, 0, dict);

    return TRUE;
}

static int close_dict_entry(DBusMessageIter *it, DBusMessageIter *dict)
{
    dbus_message_iter_close_container(it, dict);
}


static int copy_array(DBusMessageIter *sit,
                      DBusMessageIter *dit,
                      DBusMessageIter *darr)
{
    DBusMessageIter  iter;
    DBusMessageIter  sdict;
    DBusMessageIter  ddict;
    DBusMessageIter *sarr = &iter;
    int              success = FALSE;

    if (dbus_message_iter_get_arg_type(sit) == DBUS_TYPE_ARRAY) {

        dbus_message_iter_recurse(sit, sarr);
        dbus_message_iter_open_container(dit, DBUS_TYPE_ARRAY, "{sv}", darr);

        success = TRUE;

        do {
            if (dbus_message_iter_get_arg_type(sarr) != DBUS_TYPE_DICT_ENTRY) {
                success = FALSE;
                break;
            }

            dbus_message_iter_recurse(sarr, &sdict);

            append_dict_entry(darr, &ddict);
            copy_dict_entry(&sdict, &ddict);
            close_dict_entry(darr, &ddict);

        } while (dbus_message_iter_next(sarr) && success);
    }

    return success;
}

static int extend_array(DBusMessageIter *arr, va_list ap)
{
    DBusMessageIter dict;
    char           *name;
    int             type;
    void           *value;
    char           *string;
    int32_t         integer;
    uint32_t        unsignd;
    double          floating;
    uint32_t        boolean;
    int             success = TRUE;


    while ((name = va_arg(ap, char *)) != NULL) {
        type  = va_arg(ap, int);

        string   = "";
        integer  = 0;
        unsignd  = 0;
        floating = 0.0;
        boolean  = FALSE;

        switch (type) {
        case 's':  string   = va_arg(ap, char *);   value = &string;   break;
        case 'i':  integer  = va_arg(ap, int32_t);  value = &integer;  break;
        case 'u':  unsignd  = va_arg(ap, uint32_t); value = &unsignd;  break;
        case 'd':  floating = va_arg(ap, double);   value = &floating; break;
        case 'b':  boolean  = va_arg(ap, uint32_t); value = &boolean;  break;
        default:   /* skip the unsupported formats */                 continue;
        }

        OHM_DEBUG(DBG_DBUS,"appending argument %s '%c' {'%s',%d,%u,%lf,%u/%s}",
                  name, (char)type, string, integer, unsignd, floating,
                  boolean, boolean ? "TRUE":"FALSE");


        append_dict_entry(arr, &dict);
        append_string(&dict, name);
        append_variant(&dict, type, value);
        close_dict_entry(arr, &dict);
    }

    return success;
}

static int close_array(DBusMessageIter *dit, DBusMessageIter *darr)
{
    return dbus_message_iter_close_container(dit, darr);
}


static DBusHandlerResult name_changed(DBusConnection *conn,
                                      DBusMessage    *msg,
                                      void           *ud)
{
    char              *sender;
    char              *before;
    char              *after;
    gboolean           success;
    DBusHandlerResult  result;

    (void)conn;
    (void)ud;


    result  = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    success = dbus_message_is_signal(msg, DBUS_ADMIN_INTERFACE,
                                     DBUS_NAME_OWNER_CHANGED_SIGNAL);

    if (success) {
        success = dbus_message_get_args(msg, NULL,
                                        DBUS_TYPE_STRING, &sender,
                                        DBUS_TYPE_STRING, &before,
                                        DBUS_TYPE_STRING, &after,
                                        DBUS_TYPE_INVALID);

        if (success && sender && !strcmp(sender, DBUS_NGF_BACKEND_SERVICE)) {

            if (after && strcmp(after, "")) {
                OHM_DEBUG(DBG_DBUS, "backend is up");
                backend = TRUE;
            }
            else if (before != NULL && (!after || !strcmp(after, ""))) {
                OHM_DEBUG(DBG_DBUS, "backend is gone");
                backend = FALSE;
                /* proxy_backend_is_down(); */
            }
        }
    }

    return result;
}


static DBusHandlerResult ngf_method(DBusConnection *conn,
                                    DBusMessage    *msg,
                                    void           *ud)
{
    static handler_t  handlers[] = {
        { DBUS_PLAY_METHOD, play_handler },
        { DBUS_STOP_METHOD, stop_handler },
        {       NULL      ,     NULL     }
    };

    int                type;
    const char        *interface;
    const char        *member;
    DBusHandlerResult  result;
    handler_t         *hlr;
    uint32_t           id;
    char               err[DBUS_ERRBUF_LEN];
    char               desc[DBUS_DESCBUF_LEN];

    (void)conn;
    (void)ud;

    type      = dbus_message_get_type(msg);
    interface = dbus_message_get_interface(msg);
    member    = dbus_message_get_member(msg);
    result    = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    OHM_DEBUG(DBG_DBUS, "got '%s' request on interface '%s'",member,interface);

    if (type == DBUS_MESSAGE_TYPE_METHOD_CALL &&
        !strcmp(interface,DBUS_NGF_INTERFACE)   )
    {
        for (hlr = handlers;    hlr->member != NULL;    hlr++) {
            if (!strcmp(member, hlr->member)) {

                if (!backend) {
                    reply_with_error(msg, DBUS_NGF_ERROR_NO_BACKEND,
                                     "backend is not running");
                }
                else {
                    err[0] = desc[0] = '\0';

                    if (!(id = hlr->function(msg, err, desc)))
                        reply_with_error(msg, err, desc);
                    else
                        reply_with_id(msg, id);
                }
                    

                result = DBUS_HANDLER_RESULT_HANDLED;
                
                break;
            }
        }
    }

    return result;
}

static uint32_t play_handler(DBusMessage *msg, char *err, char *desc)
{
    int       success;
    char     *what;
    uint32_t  id;

    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_STRING, &what,
                                    DBUS_TYPE_INVALID);

    if (!success) {
        OHM_DEBUG(DBG_DBUS, "malformed play request for '%s'", what);
        
        snprintf(err , DBUS_ERRBUF_LEN , "%s", DBUS_NGF_ERROR_FORMAT);
        snprintf(desc, DBUS_DESCBUF_LEN, "can't obtain event from request");

        id = 0;
    }
    else {
        OHM_DEBUG(DBG_DBUS, "requested to play '%s'", what); 

        id = proxy_playback_request(what, msg, desc);

        if (id == 0) {
            strncpy(err, DBUS_NGF_ERROR_DENIED, DBUS_ERRBUF_LEN);
            err[DBUS_ERRBUF_LEN-1] = '\0';
        }
    }

    return id;
}

static uint32_t stop_handler(DBusMessage *msg, char *err, char *desc)
{
    return 0;
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
