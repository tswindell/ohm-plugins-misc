/**
 * @file hal-internal.c
 * @brief OHM HAL plugin private functions
 * @author ismo.h.puustinen@nokia.com
 *
 * Copyright (C) 2008, Nokia. All rights reserved.
 */

#include "hal.h"

static int DBG_HAL, DBG_FACTS;

/* FIXME:
 *
 * - Add support to adding/removing device capabilities (new fields in
 *   OhmFacts
 * - How do the 64-bit uints map to any allowed OhmFact types?
 */

/* this uses libhal (for now) */

typedef struct _hal_modified_property {
    char *udi;
    char *key;
    dbus_bool_t is_removed;
    dbus_bool_t is_added;
} hal_modified_property;

typedef struct _decorator {
    gchar *capability;
    GSList *devices;
    hal_cb cb;
    void *user_data;
} decorator;

static gchar * escape_udi(const char *hal_udi)
{
    /* returns an escaped copy of the udi:
     *
     * /org/freedesktop/key becomes
     * org.freedesktop.key
     *
     * */
    gchar *escaped_udi;

    /* escape the udi  */
    int i, len;
    
    if (strlen(hal_udi) < 2)
        return NULL;

    escaped_udi = g_strdup(hal_udi + 1);
    
    if (escaped_udi == NULL)
        return NULL;

    len = strlen(escaped_udi);
    
    if (len < 2) {
        g_free(escaped_udi);
        return NULL;
    }

    for (i = 1; i < len; i++) {
        if (escaped_udi[i] == '/')
            escaped_udi[i] = '.';
    }

    return escaped_udi;

}

#if OPTIMIZED
static gboolean property_has_capability(LibHalPropertySet *properties, gchar *capability)
{

    LibHalPropertySetIterator iter;
    int len, i;

    libhal_psi_init(&iter, properties);

    len = libhal_property_set_get_num_elems(properties);

    for (i = 0; i < len; i++, libhal_psi_next(&iter)) {
        char *key = libhal_psi_get_key(&iter);
        if (strdup(key, "info.capabilities") == 0) {
            /* TODO: check if the capability is there */
        
        }
    }

    return TRUE;
}
#endif

static gboolean has_udi(decorator *dec, const gchar *udi)
{
    GSList *e = NULL;
    for (e = dec->devices; e != NULL; e = g_slist_next(e)) {
        gchar *device_udi = e->data;
        if (strcmp(device_udi, udi) == 0)
            return TRUE;
    }
    return FALSE;
}

static OhmFact * create_fact(hal_plugin *plugin, const char *udi, LibHalPropertySet *properties)
{
    /* Create an OhmFact based on the properties of a HAL object */

    LibHalPropertySetIterator iter;
    OhmFact *fact = NULL;
    int i, len;
    gchar *escaped_udi = escape_udi(udi);
    GValue *val = NULL;

    if (escaped_udi == NULL)
        return NULL;

    fact = ohm_fact_new(escaped_udi);
    OHM_DEBUG(DBG_FACTS, "created fact '%s' at '%p'", escaped_udi, fact);
    g_free(escaped_udi);

    if (!fact)
        return NULL;

    /* set the identity field with the original UDI value */
    val = ohm_value_from_string(udi);
    ohm_fact_set(fact, "udi", val);

    libhal_psi_init(&iter, properties);
    
    /* TODO error handling */

    len = libhal_property_set_get_num_elems(properties);

    for (i = 0; i < len; i++, libhal_psi_next(&iter)) {
        char *key = libhal_psi_get_key(&iter);
        LibHalPropertyType type = libhal_psi_get_type(&iter);

        /* Not good to duplicate the switch, consider strategy pattern. Still,
         * it is a good idea to fetch the properties only once. */
        switch (type) {
            case LIBHAL_PROPERTY_TYPE_INT32:
                {
                    dbus_int32_t hal_value = libhal_psi_get_int(&iter);
                    val = ohm_value_from_int(hal_value);
                    break;
                }
            case LIBHAL_PROPERTY_TYPE_STRING:
                {
                    /* freed with propertyset*/
                    char *hal_value = libhal_psi_get_string(&iter);
                    val = ohm_value_from_string(hal_value);
                    break;
                }
            case LIBHAL_PROPERTY_TYPE_STRLIST:
                {
#define STRING_DELIMITER "\\"
                    /* freed with propertyset*/
                    char **strlist = libhal_psi_get_strlist(&iter);
                    gchar *escaped_string = g_strjoinv(STRING_DELIMITER, strlist);
                    val = ohm_value_from_string(escaped_string);
                    g_free(escaped_string);
                    break;
#undef STRING_DELIMITER
                }
            default:
                /* error case, currently means that FactStore doesn't
                 * support the type yet */
                break;
        }

        if (val) {
            ohm_fact_set(fact, key, val);
        }
    }

    return fact;
}


static gboolean process_decoration(hal_plugin *plugin, decorator *dec, gboolean added, gboolean removed, const gchar *udi)
{
    gboolean match = FALSE;
    printf("> process_decoration\n");

    if (has_udi(dec, udi)) {
        DBusError error;
        LibHalPropertySet *properties = NULL;
        OhmFact *fact = NULL;

        match = TRUE;
        dbus_error_init(&error);
        
        if (!removed) {
            /* get the fact from the HAL */
            properties = libhal_device_get_all_properties(plugin->hal_ctx, udi, &error);

            if (dbus_error_is_set(&error)) {
                g_print("Error getting data for HAL object %s. '%s': '%s'\n", udi, error.name, error.message);
                return FALSE;
            }

        }

        fact = create_fact(plugin, udi, properties);
        dec->cb(fact, dec->capability, added, removed, dec->user_data);

        libhal_free_property_set(properties);
    }

    return match;
}

static gboolean process_udi(hal_plugin *plugin, gboolean added, gboolean removed, const gchar *udi)
{
    GSList *e = NULL;
    gboolean match = FALSE;

    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;
        if (process_decoration(plugin, dec, added, removed, udi))
            match = TRUE;
    }
    return match;
}

static void
hal_capability_added_cb (LibHalContext *ctx,
        const char *udi, const char *capability)
{
    printf( "> hal_capability_added_cb: udi '%s', capability: '%s'\n", udi, capability);
    /* TODO */
}

static void
hal_capability_lost_cb (LibHalContext *ctx,
        const char *udi, const char *capability)
{
    printf( "> hal_capability_lost_cb: udi '%s', capability: '%s'\n", udi, capability);
    /* TODO */
}

static void
hal_device_added_cb (LibHalContext *ctx,
        const char *udi)
{
    hal_plugin *plugin = (hal_plugin *) libhal_ctx_get_user_data(ctx);
    DBusError error;
#if OPTIMIZED
    LibHalPropertySet *properties = NULL;
#endif
    GSList *e;
    gboolean match = FALSE;

    printf( "> hal_device_added_cb: udi '%s'\n", udi);
    dbus_error_init(&error);

#if OPTIMIZED
    /* get the fact from the HAL */
    properties = libhal_device_get_all_properties(plugin->hal_ctx, udi, &error);
#endif

    if (dbus_error_is_set(&error)) {
        g_print("Error getting data for HAL object %s. '%s': '%s'\n", udi, error.name, error.message);
        return;
    }

    /* see if the device has a capability that someone is interested in */
    printf("decorators: '%u'\n", g_slist_length(plugin->decorators));
    
    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;
#if OPTIMIZED
        if (property_has_capability(properties, dec->capability)) {
#else
        if (libhal_device_query_capability(plugin->hal_ctx, udi, dec->capability, &error)) {
#endif
            printf("device '%s' has capability '%s'\n", udi, dec->capability);
            match = TRUE;
            dec->devices = g_slist_prepend(dec->devices, g_strdup(udi));
            process_decoration(plugin, dec, TRUE, FALSE, udi);
        }
        else {
            printf("device '%s' doesn't have capability '%s'\n", udi, dec->capability);
        }
    }

#if OPTIMIZED
    libhal_free_property_set(properties);
#endif

    if (match)
        libhal_device_add_property_watch(ctx, udi, NULL);

    return;
}

static void
hal_device_removed_cb (LibHalContext *ctx,
        const char *udi)
{
    hal_plugin *plugin = (hal_plugin *) libhal_ctx_get_user_data(ctx);
    GSList *orig = NULL;
    GSList *e;

    printf( "> hal_device_removed_cb: udi '%s'\n", udi);
    
    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;
        if (has_udi(dec, udi)) {
            orig = g_slist_find_custom(dec->devices, udi, g_str_equal);
            if (orig) {
                dec->devices = g_slist_remove(dec->devices, orig->data);
                /* FIXME: free the UDI? */
            }
            else {
                printf( "Device was not found from the decorator list!\n");
            }
        }
    }

    /* g_free(orig->data); */
    /* FIXME: free the found element? */

    if (process_udi(plugin, FALSE, TRUE, udi))
        libhal_device_remove_property_watch(ctx, udi, NULL);

    return;
}

static gboolean process_modified_properties(gpointer data) 
{
    hal_plugin *plugin = (hal_plugin *) data;
    GSList *e = NULL;

    printf( "> process_modified_properties\n");

    for (e = plugin->modified_properties; e != NULL; e = g_slist_next(e)) {

        hal_modified_property *modified_property = e->data;
        process_udi(plugin, FALSE, FALSE, modified_property->udi);

        g_free(modified_property->udi);
        g_free(modified_property->key);
        g_free(modified_property);
        e->data = NULL;
    }

    g_slist_free(plugin->modified_properties);
    plugin->modified_properties = NULL;

    /* do not call again */
    return FALSE;
}

    static void
hal_property_modified_cb (LibHalContext *ctx,
        const char *udi,
        const char *key,
        dbus_bool_t is_removed,
        dbus_bool_t is_added)
{

    /* This function is called several times when a signal that contains
     * information of multiple HAL property modifications arrives.
     * Schedule a delayed processing of data in the idle loop. */

    hal_modified_property *modified_property = NULL;
    hal_plugin *plugin = (hal_plugin *) libhal_ctx_get_user_data(ctx);

    printf("> hal_property_modified_cb: udi '%s', key '%s', %s, %s\n",
              udi, key,
              is_removed ? "removed" : "not removed",
              is_added ? "added" : "not added");

    if (!plugin->modified_properties) {
        g_idle_add(process_modified_properties, plugin);
    }

    modified_property = g_new0(hal_modified_property, 1);
    
    modified_property->udi = g_strdup(udi);
    modified_property->key = g_strdup(key);
    modified_property->is_removed = is_removed;
    modified_property->is_added = is_added;

    /* keep the order (even if O(n)) :-P */
    plugin->modified_properties = g_slist_append(plugin->modified_properties,
            modified_property);

    return;
}

gboolean decorate(hal_plugin *plugin, const gchar *capability, hal_cb cb, void *user_data)
{
    decorator *dec = NULL;
    DBusError error;
    int n_devices = 0, i;
    char **devices = NULL;
    
    dbus_error_init(&error);

    if (!plugin)
        goto error;

    devices = libhal_find_device_by_capability(plugin->hal_ctx, capability, &n_devices, &error);

    /* create the decorator object */
    if ((dec = g_new0(decorator, 1)) == NULL)
        goto error;

    dec->cb = cb;
    dec->user_data = user_data;
    dec->capability = g_strdup(capability);

    for (i = 0; i < n_devices; i++) {
        dec->devices = g_slist_prepend(dec->devices, g_strdup(devices[i]));
        process_decoration(plugin, dec, FALSE, FALSE, devices[i]);
    }

    libhal_free_string_array(devices);

    /* put the object to decorator list */
    plugin->decorators = g_slist_prepend(plugin->decorators, dec);

    return TRUE;

error:

    return FALSE;
}

gboolean undecorate(hal_plugin *plugin, void *user_data) {
    /* identify the decorator by user data */
    GSList *e = NULL;

    for (e = plugin->decorators; e != NULL; e = g_slist_next(e)) {
        decorator *dec = e->data;
        if (dec->user_data == user_data) {
            plugin->decorators = g_slist_remove(plugin->decorators, dec);
            g_free(dec);
            return TRUE;
        }
    }
    return FALSE;
}

hal_plugin * init_hal(DBusConnection *c, int flag_hal, int flag_facts)
{
    DBusError error;
    hal_plugin *plugin = g_new0(hal_plugin, 1);

    DBG_HAL   = flag_hal;
    DBG_FACTS = flag_facts;

    printf( "Initializing the HAL plugin\n");
    
    if (!plugin) {
        return NULL;
    }
    plugin->hal_ctx = libhal_ctx_new();
    plugin->c = c;
    plugin->fs = ohm_fact_store_get_fact_store();

    /* TODO: error handling everywhere */
    dbus_error_init(&error);

    if (!libhal_ctx_set_dbus_connection(plugin->hal_ctx, c))
        goto error;

    /* start a watch on new devices */

    if (!libhal_ctx_set_device_added(plugin->hal_ctx, hal_device_added_cb))
        goto error;
    if (!libhal_ctx_set_device_removed(plugin->hal_ctx, hal_device_removed_cb))
        goto error;
    if (!libhal_ctx_set_device_new_capability(plugin->hal_ctx, hal_capability_added_cb))
        goto error;
    if (!libhal_ctx_set_device_lost_capability(plugin->hal_ctx, hal_capability_lost_cb))
        goto error;
    if (!libhal_ctx_set_device_property_modified(plugin->hal_ctx, hal_property_modified_cb))
        goto error;

    if (!libhal_ctx_set_user_data(plugin->hal_ctx, plugin))
        goto error;

    if (!libhal_ctx_init(plugin->hal_ctx, &error))
        goto error;

    return plugin;

error:

    if (dbus_error_is_set(&error)) {
        printf( "Error initializing the HAL plugin. '%s': '%s'\n",
                  error.name, error.message);
    }
    else {
        printf( "Error initializing the HAL plugin\n");
    }

    return NULL;
}

void deinit_hal(hal_plugin *plugin)
{
    /* FIXME: free the decorators */

    libhal_ctx_shutdown(plugin->hal_ctx, NULL);
    libhal_ctx_free(plugin->hal_ctx);
    return;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
