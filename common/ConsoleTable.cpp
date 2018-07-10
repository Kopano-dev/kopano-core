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

#include <kopano/platform.h>
#include "ConsoleTable.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace KC {

using std::cout;
using std::endl;

/** 
 * Creates a static string table in set sizes
 * 
 * @param[in] rows number of rows in the table (if exceeding, table will be printed and cleared)
 * @param[in] columns exact number of columns
 */
ConsoleTable::ConsoleTable(size_t rows, size_t columns) :
	m_iRows(rows), m_iColumns(columns)
{
	m_vTable.resize(rows);
	for (size_t i = 0; i < rows; ++i)
		m_vTable[i].resize(columns);
	m_vMaxLengths.resize(m_iColumns);
	m_vHeader.resize(m_iColumns);
}

/**
 * Removes all contents from the current table
 */
void ConsoleTable::Clear()
{
	// remove all data by using resize to 0 and back to original size
	m_vTable.resize(0);
	m_vTable.resize(m_iRows);
	for (size_t i = 0; i < m_iRows; ++i)
		m_vTable[i].resize(m_iColumns);
	m_vMaxLengths.clear();
	m_vHeader.clear();
	m_nRow = 0;
	bHaveHeader = false;
}

/** 
 * Removes all contents from the current table and makes the table size in the new given size
 * 
 * @param[in] rows guessed number of rows in the table
 * @param[in] columns exact number of columns
 */
void ConsoleTable::Resize(size_t rows, size_t columns)
{
	m_nRow = 0;
	bHaveHeader = false;

	m_iRows = rows;
	m_iColumns = columns;

	m_vTable.resize(rows);
	for (size_t i = 0; i < rows; ++i)
		m_vTable[i].resize(columns);
	m_vMaxLengths.resize(m_iColumns);
	m_vHeader.resize(m_iColumns);
}

/** 
 * Sets the header name for a column. This is optional, as not all tables have headers.
 * 
 * @param[in] col column numer to set header name for
 * @param[in] entry name of the header
 * @retval		true on success, false if offsets are out of range
 */
bool ConsoleTable::SetHeader(size_t col, const std::string &entry)
{
	size_t len;

	if (col >= m_iColumns)
		return false;
	m_vHeader[col] = m_converter.convert_to<std::wstring>(CHARSET_WCHAR, entry, entry.length(), CHARSET_CHAR);
	len = entry.length();
	if (len > m_vMaxLengths[col])
		m_vMaxLengths[col] = len;

	bHaveHeader = true;

	return true;
}

/**
 * Adds entry at column in the table at the given column. Row
 * increments automatically after setting last column of table.
 *
 * @param[in]	col		column offset of the table, starting at 0
 * @param[in]	entry	utf-8 string to set in the table in current terminal charset
 * @retval		true on success, false if offsets are out of range
 */
bool ConsoleTable::AddColumn(size_t col, const std::string &entry)
{
	if (col >= m_iColumns)
		return false;

	if (m_nRow >= m_iRows) {
		PrintTable();
		Clear();
	}

	SetColumn(m_nRow, col, entry);

	return true;
}

/**
 * Sets entry in the table at the given offsets
 *
 * @param[in]	row		row offset of the table, starting at 0
 * @param[in]	col		column offset of the table, starting at 0
 * @param[in]	entry	utf-8 string to set in the table in current terminal charset
 * @retval		true on success, false if offsets are out of range
 */
bool ConsoleTable::SetColumn(size_t row, size_t col, const std::string &entry)
{
	if (col >= m_iColumns || row >= m_iRows)
		return false;

	// we want to count number of printable characters, which is not possible using UTF-8
	m_vTable[row][col] = m_converter.convert_to<std::wstring>(CHARSET_WCHAR, entry, entry.length(), CHARSET_CHAR);
	auto len = m_vTable[row][col].length();
	if (len > m_vMaxLengths[col])
		m_vMaxLengths[col] = len;

	m_nRow = row;
	if (col+1 == m_iColumns)
		++m_nRow;

	return true;
}

/** 
 * Prints one row of a table
 * 
 * @param[in] vRow reference to the row wanted on the screen
 */
void ConsoleTable::PrintRow(const std::vector<std::wstring> &vRow)
{
	size_t nCol = 0;
	cout << m_lead;
	for (const auto &col : vRow) {
		// cout can't print wstring, and wcout is not allowed to mix with cout.
		printf("%ls\t", col.c_str());
		if (nCol+1 < m_iColumns) {
			auto longest = (m_vMaxLengths[nCol] /8) + 1;
			auto ntabs = longest - ((col.length() / 8) + 1);
			cout << std::string(ntabs, '\t');
		}
		++nCol;
	}
	cout << endl;
}

/** 
 * Dumps one row of a table as comma separated fields
 * 
 * @param[in] vRow reference to the row wanted on the screen
 */
void ConsoleTable::DumpRow(const std::vector<std::wstring> &vRow)
{
	size_t nCol = 0;
	for (const auto &col : vRow) {
		std::wstring temp = col;
		std::replace(temp.begin(), temp.end(), '\n', ' ');
		// cout can't print wstring, and wcout is not allowed to mix with cout.
		printf("%ls", temp.c_str());
		if (nCol + 1 < m_iColumns)
			printf(";");
		++nCol;
	}
	printf("\n");
}

/**
 * Prints the table on screen
 */
void ConsoleTable::PrintTable()
{
	if (bHaveHeader) {
		PrintRow(m_vHeader);
		size_t total = 0;

		for (size_t nCol = 0; nCol < m_iColumns; ++nCol)
			total += m_vMaxLengths[nCol];

		total += (m_iColumns -1) * 8;
		cout << m_lead << std::string(total, '-') << endl;
	}

	for (size_t nRow = 0; nRow < m_nRow; ++nRow)
		PrintRow(m_vTable[nRow]);
}

/**
 * Dumps the table as comma separated fields
 */
void ConsoleTable::DumpTable()
{
	if (bHaveHeader)
		DumpRow(m_vHeader);
	for (size_t nRow = 0; nRow < m_nRow; ++nRow)
		DumpRow(m_vTable[nRow]);
}

} /* namespace */
