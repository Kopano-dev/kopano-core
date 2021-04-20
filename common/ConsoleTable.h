/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <string>
#if __cplusplus >= 201700L
#	include <string_view>
#endif
#include <vector>

namespace KC {

class KC_EXPORT ConsoleTable KC_FINAL {
public:
	ConsoleTable(size_t rows, size_t columns);
	void Clear();
	void Resize(size_t rows, size_t columns);
	bool SetHeader(size_t col, const string_view &entry);
	void set_lead(const char *lead) { m_lead = lead; }
	bool AddColumn(size_t col, const string_view &entry);
	bool SetColumn(size_t row, size_t col, const string_view &entry);
	void PrintTable();
	void DumpTable();

private:
	size_t m_nRow = 0, m_iRows, m_iColumns;
	std::string m_lead = "\t";
	std::vector<std::wstring> m_vHeader;
	std::vector<std::vector<std::wstring> > m_vTable;
	std::vector<size_t> m_vMaxLengths;
	bool bHaveHeader = false;

	KC_HIDDEN void PrintRow(const std::vector<std::wstring> &row);
	KC_HIDDEN void DumpRow(const std::vector<std::wstring> &row);
};

} /* namespace */
