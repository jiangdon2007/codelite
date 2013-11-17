//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//
// copyright            : (C) 2013 by Eran Ifrah
// file name            : sftp_settings.h
//
// -------------------------------------------------------------------------
// A
//              _____           _      _     _ _
//             /  __ \         | |    | |   (_) |
//             | /  \/ ___   __| | ___| |    _| |_ ___
//             | |    / _ \ / _  |/ _ \ |   | | __/ _ )
//             | \__/\ (_) | (_| |  __/ |___| | ||  __/
//              \____/\___/ \__,_|\___\_____/_|\__\___|
//
//                                                  F i l e
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

#ifndef SFTPSETTINGS_H
#define SFTPSETTINGS_H

#include "cl_config.h" // Base class: clConfigItem
#include "ssh_account_info.h"

class WXDLLIMPEXP_SDK SFTPSettings : public clConfigItem
{
    SSHAccountInfo::List_t m_accounts;

public:
    SFTPSettings();
    virtual ~SFTPSettings();

    void SetAccounts(const SSHAccountInfo::List_t& accounts) {
        this->m_accounts = accounts;
    }
    const SSHAccountInfo::List_t& GetAccounts() const {
        return m_accounts;
    }
    
    bool GetAccount(const wxString &name, SSHAccountInfo &account) const;
    static void Load(SFTPSettings& settings);
    static void Save(const SFTPSettings& settings);
    
public:
    virtual void FromJSON(const JSONElement& json);
    virtual JSONElement ToJSON() const;
};

#endif // SFTPSETTINGS_H