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

function initFolderSize(module){

	// Create a list of the subfolders using the tableWidget
	var tableWidgetElem = dhtml.addElement(dhtml.getElementById("foldersize_table"), "div", false, "tableWidgetContainer");
	tableWidgetElem.style.height = "200px";

	this.propNewTime_tableWidget = new TableWidget(tableWidgetElem, false);
	this.propNewTime_tableWidget.addColumn("name", _("Subfolder"), false, 1);
	this.propNewTime_tableWidget.addColumn("size", _("Size"), 75, 2);
	this.propNewTime_tableWidget.addColumn("totalsize", _("Total Size"), 75, 3);

	//module.setFolderSizeData();
}


function parseFolderSizeData(data){


	//var tableData = new Array();
	dhtml.getElementById("folder_name").innerHTML = data['name'];
	dhtml.getElementById("size_excl").innerHTML = data['size'];
	dhtml.getElementById("size_incl").innerHTML = data['totalsize'];
	this.propNewTime_tableWidget.generateTable(data['subfolders']);

}
