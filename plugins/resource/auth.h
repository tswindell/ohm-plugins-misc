/*************************************************************************
Copyright (C) 2010 Nokia Corporation.

These OHM Modules are free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


#ifndef __OHM_RESOURCE_AUTH_H__
#define __OHM_RESOURCE_AUTH_H__

/* hack to avoid multiple includes */
typedef struct _OhmPlugin OhmPlugin;

typedef enum {
    auth_reject = 0,
    auth_accept
} auth_policy_t;

void auth_init(OhmPlugin *);
void auth_query(const char* klass, char** method, char** arg);
auth_policy_t auth_get_default_policy();
void auth_exit(OhmPlugin *plugin);

#endif /* __OHM_RESOURCE_AUTH_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
