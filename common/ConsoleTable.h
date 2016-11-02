/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __CONSOLE_TABLE_H
#define __CONSOLE_TABLE_H

#include <kopano/zcdefs.h>
#include <string>
#include <vector>
#include <kopano/charset/convert.h>

class ConsoleTable _kc_final {
public:
	ConsoleTable(size_t rows, size_t columns);

	void Clear();
	void Resize(size_t rows, size_t columns);

	bool SetHeader(size_t col, const std::string& entry);

	bool AddColumn(size_t col, const std::string& entry);
	bool SetColumn(size_t row, size_t col, const std::string& entry);
	void PrintTable();
	void DumpTable();

private:
	size_t m_nRow;
	size_t m_iRows;
	size_t m_iColumns;
	std::vector<std::wstring> m_vHeader;
	std::vector<std::vector<std::wstring> > m_vTable;
	std::vector<size_t> m_vMaxLengths;
	convert_context m_converter;
	bool bHaveHeader;

	void PrintRow(const std::vector<std::wstring>& vRow);
	void DumpRow(const std::vector<std::wstring>& vRow);
};

#endif
