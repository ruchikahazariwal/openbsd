/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef IN_1_NSAP_PTR_23_H
#define IN_1_NSAP_PTR_23_H 1

/* $Id: nsap-ptr_23.h,v 1.4 2020/01/20 18:51:53 florian Exp $ */

/*!
 *  \brief Per RFC1348.  Obsoleted in RFC 1706 - use PTR instead. */

typedef struct dns_rdata_in_nsap_ptr {
	dns_rdatacommon_t	common;
	dns_name_t		owner;
} dns_rdata_in_nsap_ptr_t;

#endif /* IN_1_NSAP_PTR_23_H */
