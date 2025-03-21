/*
 * \brief  Dummy implementations of symbols needed by OpenSSL
 * \author Alexander Boetcher
 * \date   2026-06-07
 */

/*
 * Copyright (C) 2026 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/objects.h>
#include "internal/comp.h"
#include <openssl/err.h>
#include <openssl/async.h>
#include "crypto/cryptlib.h"
#include "internal/bio.h"
#include "internal/thread_once.h"
#include "comp_local.h"


const char compiler_flags[] =
  "-pipe -O2 -fno-strict-aliasing -fPIC -Wno-sign-compare -Werror-implicit-function-declaration";


COMP_METHOD * COMP_brotli_oneshot(void)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
}


COMP_METHOD * COMP_zstd_oneshot(void)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
}


const BIO_METHOD *BIO_f_brotli(void)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
	return NULL;
}


const BIO_METHOD *BIO_f_zstd(void)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
	return NULL;
}


void ossl_comp_brotli_cleanup(void)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
}


void ossl_comp_zstd_cleanup(void)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
}


int async_init(void)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
	return -1;
}


ASYNC_JOB *ASYNC_get_current_job(void)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
	return NULL;
}


int ASYNC_start_job(ASYNC_JOB **job, ASYNC_WAIT_CTX *wctx, int *ret,
                    int (*func)(void *), void *args, size_t size)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
	return -1;
}


void async_deinit(void)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
}


void ASYNC_WAIT_CTX_free(ASYNC_WAIT_CTX *ctx)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
}


int ASYNC_WAIT_CTX_get_status(ASYNC_WAIT_CTX *ctx)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
	return -1;
}


int ASYNC_WAIT_CTX_get_all_fds(ASYNC_WAIT_CTX *ctx, OSSL_ASYNC_FD *fd,
                               size_t *numfds)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
	return -1;
}


int ASYNC_WAIT_CTX_get_changed_fds(ASYNC_WAIT_CTX *ctx, OSSL_ASYNC_FD *addfd,
                                   size_t *numaddfds, OSSL_ASYNC_FD *delfd,
                                   size_t *numdelfds)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
	return -1;
}


ASYNC_WAIT_CTX *ASYNC_WAIT_CTX_new(void)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
	return NULL;
}


int ASYNC_WAIT_CTX_set_callback(ASYNC_WAIT_CTX *ctx,
                                ASYNC_callback_fn callback,
                                void *callback_arg)
{
	printf("%s not implemented\n", __func__);
	* (unsigned *)0 = 0xdead;
	return -1;
}
