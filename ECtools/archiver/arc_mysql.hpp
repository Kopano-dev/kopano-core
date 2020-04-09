/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/database.hpp>
#include <string>

namespace KC {

class ECConfig;

class KCMDatabaseMySQL final : public KDatabase {
public:
	ECRESULT		Connect(ECConfig *lpConfig);
	virtual const struct sSQLDatabase_t *GetDatabaseDefs() override;
};

} /* namespace */
