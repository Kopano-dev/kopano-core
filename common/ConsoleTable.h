/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef EC_CONSOLE_TABLE_H
#define EC_CONSOLE_TABLE_H

#include <kopano/zcdefs.h>
#include <string>
#include <vector>
#include <kopano/charset/convert.h>

namespace KC {

class KC_EXPORT ConsoleTable KC_FINAL {
public:
	ConsoleTable(size_t rows, size_t columns);
	_kc_hidden void Clear(void);
	void Resize(size_t rows, size_t columns);
	bool SetHeader(size_t col, const std::string& entry);
	void set_lead(const char *lead) { m_lead = lead; }
	bool AddColumn(size_t col, const std::string& entry);
	bool SetColumn(size_t row, size_t col, const std::string& entry);
	void PrintTable();
	void DumpTable();

private:
	size_t m_nRow = 0, m_iRows, m_iColumns;
	std::string m_lead = "\t";
	std::vector<std::wstring> m_vHeader;
	std::vector<std::vector<std::wstring> > m_vTable;
	std::vector<size_t> m_vMaxLengths;
	convert_context m_converter;
	bool bHaveHeader = false;

	_kc_hidden void PrintRow(const std::vector<std::wstring> &row);
	_kc_hidden void DumpRow(const std::vector<std::wstring> &row);
};

} /* namespace */

#endif
