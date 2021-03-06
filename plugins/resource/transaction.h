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


#ifndef __OHM_RESOURCE_TRANSACTION_H__
#define __OHM_RESOURCE_TRANSACTION_H__

#include <stdint.h>

#define NO_TRANSACTION 0

typedef void   (*transaction_callback_t)(uint32_t *, int, uint32_t, void *);


void transaction_init(OhmPlugin *);

uint32_t transaction_create(transaction_callback_t, void *);

int transaction_add_resource_set(uint32_t, uint32_t);

int transaction_ref(uint32_t);
int transaction_unref(uint32_t);



#endif	/* __OHM_RESOURCE_TRANSACTION_H__ */

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
