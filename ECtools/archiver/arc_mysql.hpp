/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/database.hpp>

namespace KC {

class Config;

class KCMDatabaseMySQL final : public KDatabase {
public:
	ECRESULT Connect(Config *);
	virtual const struct sSQLDatabase_t *GetDatabaseDefs() override;
};

} /* namespace */
