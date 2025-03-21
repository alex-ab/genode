/*
 * \brief  Dummy implementations of symbols needed by XPCOM
 * \author Norman Feske
 * \date   2020-10-09
 */

/*
 * Copyright (C) 2013-2017 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <stub_macros.h>

static bool const debug = true;


#include <nsProxyEventPrivate.h>

nsresult nsProxyObjectManager::Create(nsISupports*, nsID const&, void**) STOP
void nsProxyObjectManager::Shutdown() STOP


#include <xptinfo.h>

nsIInterfaceInfoManager *XPTI_GetInterfaceInfoManager() { return nullptr; }
void XPTI_FreeInterfaceInfoManager() { }
