/**
 * @file powerkeymenu-dbus-names.h
 * DBus Interface to the System UI DevLock plugin
 * <p>
 * This file is part of osso-systemui-devlock
 * <p>
 * Copyright (C) 2013 Pali Rohár <pali.rohar@gmail.com>
 *
 * These headers are free software; you can redistribute them
 * and/or modify them under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * These headers are distributed in the hope that they will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef _SYSTEMUI_DEVLOCK_DBUS_NAMES_H
#define _SYSTEMUI_DEVLOCK_DBUS_NAMES_H

#define SYSTEMUI_DEVLOCK_OPEN_REQ	"devlock_open"
#define SYSTEMUI_DEVLOCK_CLOSE_REQ	"devlock_close"

/** FIXME: Order may not be correct */
typedef enum {
	DEVLOCK_QUERY_ENABLE,
	DEVLOCK_QUERY_ENABLE_QUIET,
	DEVLOCK_QUERY_OPEN,
	DEVLOCK_QUERY_NOTE
} devlock_query_mode;

/** FIXME: Order may not be correct */
typedef enum {
	DEVLOCK_RESPONSE_LOCKED,
	DEVLOCK_RESPONSE_SHUTDOWN,
	DEVLOCK_RESPONSE_NOSHUTDOWN,
	DEVLOCK_RESPONSE_CORRECT,
	DEVLOCK_RESPONSE_INCORRECT,
	DEVLOCK_RESPONSE_CANCEL
} devlock_response_result;

/** FIXME: Order may not be correct */
typedef enum {
	DEVLOCK_REPLY_LOCKED,
	DEVLOCK_REPLY_VERIFY,
	DEVLOCK_REPLY_FAILED
} devlock_reply_status;

#endif /* _SYSTEMUI_DEVLOCK_DBUS_NAMES_H */
